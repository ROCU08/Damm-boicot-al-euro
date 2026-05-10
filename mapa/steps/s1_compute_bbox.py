"""s1: calcula el bbox del área de operaciones a partir de geocode_cache.csv.

Lee todas las (lat, lon) válidas, calcula el bbox y le añade un margen del 5%.
Escribe `bbox.json` tanto en mapa/data/ (uso interno) como en mapa/out/ (consumible por C++/runtime).

Uso:
    python -m mapa.steps.s1_compute_bbox
"""
from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CACHE = REPO_ROOT / "test" / "geocode_cache.csv"
DEFAULT_DATA = REPO_ROOT / "mapa" / "data"
DEFAULT_OUT = REPO_ROOT / "mapa" / "out"
DEFAULT_MARGIN = 0.05


def read_coords(cache_path: Path) -> list[tuple[float, float]]:
    coords: list[tuple[float, float]] = []
    with cache_path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            lat_s, lon_s = row.get("lat", ""), row.get("lon", "")
            if not lat_s or not lon_s:
                continue
            try:
                coords.append((float(lat_s), float(lon_s)))
            except ValueError:
                continue
    return coords


def _percentile(values: list[float], q: float) -> float:
    """Percentil simple sin numpy (q en [0,100])."""
    if not values:
        raise ValueError("lista vacía")
    s = sorted(values)
    k = (len(s) - 1) * (q / 100.0)
    lo, hi = int(k), min(int(k) + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def compute_bbox(coords: list[tuple[float, float]], margin: float, pct: float) -> dict[str, float]:
    """Bbox basado en percentiles (descarta outliers de geocoding) + margen.

    pct = recorte por lado (p.ej. 1.0 → usa percentiles 1 y 99). Si pct=0, usa min/max puros.
    """
    if not coords:
        raise RuntimeError("No hay coordenadas válidas en el cache.")
    lats = [c[0] for c in coords]
    lons = [c[1] for c in coords]
    if pct > 0:
        lat_min, lat_max = _percentile(lats, pct), _percentile(lats, 100 - pct)
        lon_min, lon_max = _percentile(lons, pct), _percentile(lons, 100 - pct)
        n_inside = sum(1 for la, lo in coords if lat_min <= la <= lat_max and lon_min <= lo <= lon_max)
    else:
        lat_min, lat_max = min(lats), max(lats)
        lon_min, lon_max = min(lons), max(lons)
        n_inside = len(coords)
    dlat = (lat_max - lat_min) * margin
    dlon = (lon_max - lon_min) * margin
    return {
        "lat_min": round(lat_min - dlat, 6),
        "lat_max": round(lat_max + dlat, 6),
        "lon_min": round(lon_min - dlon, 6),
        "lon_max": round(lon_max + dlon, 6),
        "n_points_total": len(coords),
        "n_points_inside": n_inside,
        "outlier_pct": pct,
        "margin": margin,
    }


def write_bbox(bbox: dict[str, float], paths: list[Path]) -> None:
    for p in paths:
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(json.dumps(bbox, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, default=DEFAULT_CACHE)
    parser.add_argument("--data-dir", type=Path, default=DEFAULT_DATA)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--margin", type=float, default=DEFAULT_MARGIN)
    parser.add_argument("--outlier-pct", type=float, default=1.0,
                        help="Recorte percentil por lado (1.0 = descarta el 1%% extremo de cada lado).")
    args = parser.parse_args()

    print(f"[s1] leyendo {args.cache}")
    coords = read_coords(args.cache)
    print(f"[s1] {len(coords)} coordenadas válidas")

    bbox = compute_bbox(coords, args.margin, args.outlier_pct)
    print(f"[s1] bbox calculado (recorte {args.outlier_pct}% por lado):")
    print(f"     lat: [{bbox['lat_min']}, {bbox['lat_max']}]  ({bbox['lat_max']-bbox['lat_min']:.4f}°)")
    print(f"     lon: [{bbox['lon_min']}, {bbox['lon_max']}]  ({bbox['lon_max']-bbox['lon_min']:.4f}°)")
    print(f"     {bbox['n_points_inside']}/{bbox['n_points_total']} puntos dentro")

    out_files = [args.data_dir / "bbox.json", args.out_dir / "bbox.json"]
    write_bbox(bbox, out_files)
    for p in out_files:
        print(f"[s1] escrito {p}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
