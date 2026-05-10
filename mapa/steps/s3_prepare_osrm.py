"""s3: prepara los datos OSRM (extract + partition + customize) y deja el daemon listo.

Pasos:
  1. Verifica que existe `mapa/data/area.osm.pbf` (de s2).
  2. Corre `osrm-extract` con el perfil indicado (por defecto /opt/car.lua dentro del contenedor).
  3. Corre `osrm-partition` y `osrm-customize` para el algoritmo MLD.
  4. Levanta el daemon con `docker compose up -d` y comprueba el endpoint /route.

Usage:
    python -m mapa.steps.s3_prepare_osrm
    python -m mapa.steps.s3_prepare_osrm --profile mapa/profiles/hgv.lua    # cuando hgv.lua sea real
    python -m mapa.steps.s3_prepare_osrm --skip-prep                        # asume área.osrm ya preparado
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path

import urllib.request

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_DATA = REPO_ROOT / "mapa" / "data"
DEFAULT_COMPOSE = REPO_ROOT / "mapa" / "docker-compose.yml"
OSRM_IMAGE = "ghcr.io/project-osrm/osrm-backend:latest"
DEFAULT_PROFILE = "/opt/car.lua"  # bundled in the image
HEALTH_URL = "http://localhost:5000/route/v1/driving/2.225,41.560;2.250,41.600?overview=false"


def run_osrm_step(step: str, data_dir: Path, args: list[str]) -> None:
    """Corre un comando osrm-* dentro del contenedor montando data_dir en /data."""
    cmd = [
        "docker", "run", "--rm",
        "-v", f"{data_dir}:/data",
        OSRM_IMAGE,
        f"osrm-{step}",
    ] + args
    print(f"[s3] {' '.join(cmd)}")
    res = subprocess.run(cmd)
    if res.returncode != 0:
        raise RuntimeError(f"osrm-{step} falló (exit={res.returncode})")


def prepare_osrm(data_dir: Path, profile: str) -> None:
    pbf = data_dir / "area.osm.pbf"
    if not pbf.exists():
        raise RuntimeError(f"no existe {pbf}; ejecuta s2 primero")

    if profile.startswith("/"):
        # perfil bundled en el contenedor (p.ej. /opt/car.lua)
        run_osrm_step("extract", data_dir, ["-p", profile, "/data/area.osm.pbf"])
    else:
        # perfil local: monta su directorio en /profile
        profile_path = (REPO_ROOT / profile).resolve()
        if not profile_path.exists():
            raise RuntimeError(f"perfil no encontrado: {profile_path}")
        cmd = [
            "docker", "run", "--rm",
            "-v", f"{data_dir}:/data",
            "-v", f"{profile_path.parent}:/profile",
            OSRM_IMAGE,
            "osrm-extract", "-p", f"/profile/{profile_path.name}", "/data/area.osm.pbf",
        ]
        print(f"[s3] {' '.join(cmd)}")
        res = subprocess.run(cmd)
        if res.returncode != 0:
            raise RuntimeError(f"osrm-extract falló (exit={res.returncode})")

    run_osrm_step("partition", data_dir, ["/data/area.osrm"])
    run_osrm_step("customize", data_dir, ["/data/area.osrm"])


def start_daemon(compose_file: Path) -> None:
    cmd = ["docker", "compose", "-f", str(compose_file), "up", "-d"]
    print(f"[s3] {' '.join(cmd)}")
    res = subprocess.run(cmd)
    if res.returncode != 0:
        raise RuntimeError(f"docker compose up falló (exit={res.returncode})")


def wait_for_healthy(url: str, timeout_s: int = 60) -> None:
    print(f"[s3] esperando OSRM en {url} ...")
    t0 = time.time()
    while time.time() - t0 < timeout_s:
        try:
            with urllib.request.urlopen(url, timeout=3) as r:
                if r.status == 200:
                    print(f"[s3] OSRM responde OK tras {time.time()-t0:.1f}s")
                    return
        except Exception:
            pass
        time.sleep(2)
    raise RuntimeError(f"OSRM no respondió en {timeout_s}s en {url}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-dir", type=Path, default=DEFAULT_DATA)
    parser.add_argument("--compose", type=Path, default=DEFAULT_COMPOSE)
    parser.add_argument("--profile", default=DEFAULT_PROFILE,
                        help=f"Ruta al perfil .lua. Default: {DEFAULT_PROFILE} (dentro del contenedor).")
    parser.add_argument("--skip-prep", action="store_true",
                        help="Asume que area.osrm ya está preparado, solo levanta el daemon.")
    parser.add_argument("--no-daemon", action="store_true",
                        help="No levanta el daemon (úsalo si lo gestionas a mano).")
    args = parser.parse_args()

    if not args.skip_prep:
        prepare_osrm(args.data_dir, args.profile)

    if not args.no_daemon:
        start_daemon(args.compose)
        wait_for_healthy(HEALTH_URL)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
