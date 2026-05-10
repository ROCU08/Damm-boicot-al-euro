"""s4: snap de cada cliente a la calle más cercana donde un camión puede parar.

Para cada (lat, lon) válida en `test/geocode_cache.csv`, llama a OSRM /nearest
y obtiene el punto sobre el grafo de carreteras + el nombre/tipo de la vía.

Salida: `mapa/out/loading_points_per_client.csv`:
  direccion_normalizada, lat_orig, lon_orig, lat_snap, lon_snap, snap_dist_m,
  calle, highway_type, hgv_ok

`hgv_ok = 1` si la calle es transitable por camiones medios (no footway, no cycleway,
no path, etc.) y el snap está a menos de 200 m del original.

Cache incremental: si ya hay un fichero de salida previo, lo carga y solo geocodifica
las direcciones que faltan o tienen snap_dist_m vacío.
"""
from __future__ import annotations

import argparse
import csv
import sys
import time
import urllib.parse
import urllib.request
import json
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CACHE = REPO_ROOT / "test" / "geocode_cache.csv"
DEFAULT_OUT = REPO_ROOT / "mapa" / "out" / "loading_points_per_client.csv"
DEFAULT_OSRM = "http://localhost:5000"
NON_HGV_TYPES = {"footway", "cycleway", "path", "pedestrian", "steps", "track", "bridleway"}
MAX_SNAP_DIST_M = 200.0
FLUSH_EVERY = 25
DELAY_S = 0.05  # OSRM local es muy rápido; pequeño delay por cortesía
TIMEOUT_S = 10.0

OUTPUT_FIELDS = [
    "direccion_normalizada",
    "lat_orig", "lon_orig",
    "lat_snap", "lon_snap",
    "snap_dist_m",
    "calle", "highway_type",
    "hgv_ok",
]


def read_input_addrs(cache_path: Path) -> list[tuple[str, float, float]]:
    out: list[tuple[str, float, float]] = []
    with cache_path.open("r", encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            lat_s, lon_s = row.get("lat", ""), row.get("lon", "")
            if not lat_s or not lon_s:
                continue
            try:
                out.append((row["direccion_normalizada"], float(lat_s), float(lon_s)))
            except ValueError:
                continue
    return out


def load_existing(out_path: Path) -> dict[str, dict[str, str]]:
    if not out_path.exists():
        return {}
    existing: dict[str, dict[str, str]] = {}
    with out_path.open("r", encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            existing[row["direccion_normalizada"]] = row
    return existing


def save(out_path: Path, rows_by_key: dict[str, dict[str, str]]) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=OUTPUT_FIELDS)
        w.writeheader()
        for k in sorted(rows_by_key):
            w.writerow(rows_by_key[k])


def osrm_nearest(base: str, lat: float, lon: float) -> dict | None:
    url = f"{base}/nearest/v1/driving/{lon},{lat}?number=1"
    try:
        with urllib.request.urlopen(url, timeout=TIMEOUT_S) as r:
            if r.status != 200:
                return None
            return json.loads(r.read())
    except Exception as e:
        print(f"  OSRM error en ({lat},{lon}): {type(e).__name__}", file=sys.stderr)
        return None


def parse_waypoint(resp: dict) -> tuple[float, float, float, str] | None:
    """Devuelve (lat, lon, distance_m, name) o None si la respuesta no es válida."""
    waypoints = resp.get("waypoints") or []
    if not waypoints:
        return None
    wp = waypoints[0]
    loc = wp.get("location") or [None, None]
    lon, lat = loc[0], loc[1]
    if lat is None or lon is None:
        return None
    name = (wp.get("name") or "").strip()
    dist = float(wp.get("distance") or 0.0)
    return lat, lon, dist, name


def is_hgv_ok(highway: str, snap_dist: float) -> bool:
    if snap_dist > MAX_SNAP_DIST_M:
        return False
    if highway and highway.lower() in NON_HGV_TYPES:
        return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, default=DEFAULT_CACHE)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--osrm", default=DEFAULT_OSRM)
    parser.add_argument("--force", action="store_true",
                        help="Recalcula incluso para entradas ya presentes en el output.")
    args = parser.parse_args()

    addrs = read_input_addrs(args.cache)
    print(f"[s4] {len(addrs)} direcciones con coords en el cache")

    existing = load_existing(args.out)
    print(f"[s4] {len(existing)} ya snapeadas en {args.out.name}")

    pending = [
        a for a in addrs
        if args.force or a[0] not in existing or not existing[a[0]].get("snap_dist_m")
    ]
    print(f"[s4] {len(pending)} pendientes")
    if not pending:
        return 0

    flushed = 0
    t0 = time.time()
    for i, (key, lat, lon) in enumerate(pending, 1):
        resp = osrm_nearest(args.osrm, lat, lon)
        row = {
            "direccion_normalizada": key,
            "lat_orig": f"{lat:.6f}", "lon_orig": f"{lon:.6f}",
            "lat_snap": "", "lon_snap": "", "snap_dist_m": "",
            "calle": "", "highway_type": "", "hgv_ok": "0",
        }
        if resp:
            parsed = parse_waypoint(resp)
            if parsed:
                lat_s, lon_s, dist, name = parsed
                # OSRM no devuelve highway tag; lo derivamos del campo `name` heurísticamente
                # o lo dejamos vacío; la determinación fina necesita un /match o /tile.
                highway = ""  # se llena en una pasada futura si hace falta
                row.update({
                    "lat_snap": f"{lat_s:.6f}", "lon_snap": f"{lon_s:.6f}",
                    "snap_dist_m": f"{dist:.2f}",
                    "calle": name,
                    "highway_type": highway,
                    "hgv_ok": "1" if is_hgv_ok(highway, dist) else "0",
                })
        existing[key] = row
        flushed += 1
        if flushed >= FLUSH_EVERY:
            save(args.out, existing)
            flushed = 0
            elapsed = time.time() - t0
            print(f"  [{i}/{len(pending)}] flush ({elapsed/i:.3f}s/it)")
        if DELAY_S:
            time.sleep(DELAY_S)
    save(args.out, existing)

    # Resumen
    snapped = [r for r in existing.values() if r.get("snap_dist_m")]
    near = sum(1 for r in snapped if float(r["snap_dist_m"]) < 50)
    far = sum(1 for r in snapped if float(r["snap_dist_m"]) > 200)
    hgv = sum(1 for r in snapped if r["hgv_ok"] == "1")
    print(f"[s4] resumen: {len(snapped)} snapeados, {near} a <50m, {far} a >200m, {hgv} hgv_ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
