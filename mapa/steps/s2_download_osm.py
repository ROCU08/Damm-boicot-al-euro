"""s2: descarga el PBF de Catalunya y lo recorta al bbox del paso s1.

Pasos:
  1. Si no existe `mapa/data/cataluna-latest.osm.pbf`, lo descarga de Geofabrik (~700 MB).
  2. Usa el servicio `osmium` del docker-compose (build local desde Dockerfile.osmium)
     para recortar al bbox. La primera ejecución construye la imagen (~30s).
  3. Salida: `mapa/data/area.osm.pbf` (~50-150 MB).

Si prefieres no usar Docker para esto: `--use-native` con osmium-tool en PATH.

Uso:
    python -m mapa.steps.s2_download_osm
    python -m mapa.steps.s2_download_osm --use-native        # osmium nativo
    python -m mapa.steps.s2_download_osm --skip-download     # asume el PBF ya está
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_DATA = REPO_ROOT / "mapa" / "data"
DEFAULT_COMPOSE = REPO_ROOT / "mapa" / "docker-compose.yml"
GEOFABRIK_URL = "https://download.geofabrik.de/europe/spain/cataluna-latest.osm.pbf"
PBF_NAME = "cataluna-latest.osm.pbf"
AREA_NAME = "area.osm.pbf"


def download_pbf(url: str, dest: Path) -> None:
    if dest.exists():
        print(f"[s2] {dest.name} ya existe ({dest.stat().st_size/1024/1024:.1f} MB), no se redescarga")
        return
    print(f"[s2] descargando {url} → {dest} (esto puede tardar)")
    dest.parent.mkdir(parents=True, exist_ok=True)
    cmd = ["curl", "-L", "--fail", "--retry", "3", "-o", str(dest), url]
    res = subprocess.run(cmd)
    if res.returncode != 0:
        raise RuntimeError(f"curl falló (exit={res.returncode})")
    print(f"[s2] descargado: {dest.stat().st_size/1024/1024:.1f} MB")


def crop_with_docker(src: Path, dest: Path, bbox: dict[str, float], compose_file: Path) -> None:
    """Recorta usando el servicio 'osmium' del docker-compose (build local)."""
    bbox_str = f"{bbox['lon_min']},{bbox['lat_min']},{bbox['lon_max']},{bbox['lat_max']}"
    # Asegurar que la imagen está construida (idempotente; si ya existe, no rehace).
    build_cmd = ["docker", "compose", "-f", str(compose_file), "--profile", "tools", "build", "osmium"]
    print(f"[s2] {' '.join(build_cmd)}")
    res = subprocess.run(build_cmd)
    if res.returncode != 0:
        raise RuntimeError(f"docker compose build osmium falló (exit={res.returncode})")

    run_cmd = [
        "docker", "compose", "-f", str(compose_file), "--profile", "tools",
        "run", "--rm", "osmium",
        "extract", "--bbox", bbox_str, "--overwrite",
        "-o", f"/data/{dest.name}", f"/data/{src.name}",
    ]
    print(f"[s2] recortando con docker compose (bbox={bbox_str})...")
    res = subprocess.run(run_cmd)
    if res.returncode != 0:
        raise RuntimeError(f"osmium extract (docker compose) falló (exit={res.returncode})")


def crop_with_native(src: Path, dest: Path, bbox: dict[str, float]) -> None:
    bbox_str = f"{bbox['lon_min']},{bbox['lat_min']},{bbox['lon_max']},{bbox['lat_max']}"
    cmd = ["osmium", "extract", "--bbox", bbox_str, "--overwrite", "-o", str(dest), str(src)]
    print(f"[s2] recortando con osmium nativo (bbox={bbox_str})...")
    res = subprocess.run(cmd)
    if res.returncode != 0:
        raise RuntimeError(f"osmium extract falló (exit={res.returncode})")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-dir", type=Path, default=DEFAULT_DATA)
    parser.add_argument("--compose", type=Path, default=DEFAULT_COMPOSE)
    parser.add_argument("--url", default=GEOFABRIK_URL)
    parser.add_argument("--skip-download", action="store_true")
    parser.add_argument("--use-native", action="store_true",
                        help="Usa osmium nativo en PATH en lugar del contenedor.")
    args = parser.parse_args()

    bbox_path = args.data_dir / "bbox.json"
    if not bbox_path.exists():
        print(f"[s2] ERROR: no existe {bbox_path}. Ejecuta s1 primero.", file=sys.stderr)
        return 1
    bbox = json.loads(bbox_path.read_text(encoding="utf-8"))

    src = args.data_dir / PBF_NAME
    dest = args.data_dir / AREA_NAME

    if not args.skip_download:
        download_pbf(args.url, src)
    elif not src.exists():
        print(f"[s2] ERROR: --skip-download pero {src} no existe", file=sys.stderr)
        return 1

    if args.use_native:
        crop_with_native(src, dest, bbox)
    else:
        crop_with_docker(src, dest, bbox, args.compose)

    if not dest.exists():
        print(f"[s2] ERROR: no se generó {dest}", file=sys.stderr)
        return 1
    print(f"[s2] generado {dest} ({dest.stat().st_size/1024/1024:.1f} MB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
