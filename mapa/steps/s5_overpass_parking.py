"""s5: catálogo de zonas de carga/descarga públicas dentro del bbox de operaciones.

Query Overpass: parking de camiones, loading_dock, hgv:parking, service=loading.

Salida: `mapa/out/loading_points_public.csv`:
  osm_id, lat, lon, tipo, nombre, capacidad
"""
from __future__ import annotations

import argparse
import csv
import json
import sys
import urllib.parse
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BBOX = REPO_ROOT / "mapa" / "out" / "bbox.json"
DEFAULT_OUT = REPO_ROOT / "mapa" / "out" / "loading_points_public.csv"
OVERPASS_URL = "https://overpass-api.de/api/interpreter"
TIMEOUT_S = 90.0

OUTPUT_FIELDS = ["osm_id", "lat", "lon", "tipo", "nombre", "capacidad"]


def build_query(bbox: dict[str, float]) -> str:
    # Overpass quiere bbox como (south, west, north, east)
    bb = f"{bbox['lat_min']},{bbox['lon_min']},{bbox['lat_max']},{bbox['lon_max']}"
    return f"""
[out:json][timeout:60];
(
  node["amenity"="parking"]["parking"="truck"]({bb});
  way["amenity"="parking"]["parking"="truck"]({bb});
  node["amenity"="parking"]["hgv"="yes"]({bb});
  way["amenity"="parking"]["hgv"="yes"]({bb});
  node["amenity"="loading_dock"]({bb});
  way["amenity"="loading_dock"]({bb});
  node["service"="loading"]({bb});
  node["hgv:parking"="yes"]({bb});
);
out center tags;
""".strip()


def query_overpass(query: str) -> dict:
    data = urllib.parse.urlencode({"data": query}).encode("utf-8")
    req = urllib.request.Request(
        OVERPASS_URL,
        data=data,
        method="POST",
        headers={
            "User-Agent": "damm-mapa-pipeline/0.1 (overpass query)",
            "Content-Type": "application/x-www-form-urlencoded",
            "Accept": "application/json",
        },
    )
    print(f"[s5] enviando query Overpass ({len(query)} bytes)")
    with urllib.request.urlopen(req, timeout=TIMEOUT_S) as r:
        return json.loads(r.read())


def derive_tipo(tags: dict[str, str]) -> str:
    if tags.get("amenity") == "loading_dock":
        return "loading_dock"
    if tags.get("hgv:parking") == "yes":
        return "hgv_parking"
    if tags.get("amenity") == "parking" and tags.get("parking") == "truck":
        return "parking_truck"
    if tags.get("amenity") == "parking" and tags.get("hgv") == "yes":
        return "parking_hgv"
    if tags.get("service") == "loading":
        return "service_loading"
    return "otro"


def parse_elements(elements: list[dict]) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for el in elements:
        if el.get("type") == "node":
            lat, lon = el.get("lat"), el.get("lon")
        else:
            c = el.get("center") or {}
            lat, lon = c.get("lat"), c.get("lon")
        if lat is None or lon is None:
            continue
        tags = el.get("tags") or {}
        rows.append({
            "osm_id": f"{el.get('type', '')}/{el.get('id', '')}",
            "lat": f"{float(lat):.6f}",
            "lon": f"{float(lon):.6f}",
            "tipo": derive_tipo(tags),
            "nombre": (tags.get("name") or "").strip(),
            "capacidad": (tags.get("capacity") or "").strip(),
        })
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bbox", type=Path, default=DEFAULT_BBOX)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()

    if not args.bbox.exists():
        print(f"[s5] ERROR: no existe {args.bbox}; ejecuta s1 primero", file=sys.stderr)
        return 1
    bbox = json.loads(args.bbox.read_text(encoding="utf-8"))

    query = build_query(bbox)
    try:
        resp = query_overpass(query)
    except Exception as e:
        print(f"[s5] ERROR Overpass: {e}", file=sys.stderr)
        return 2

    rows = parse_elements(resp.get("elements") or [])
    print(f"[s5] {len(rows)} puntos encontrados")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=OUTPUT_FIELDS)
        w.writeheader()
        w.writerows(sorted(rows, key=lambda r: r["osm_id"]))

    by_tipo: dict[str, int] = {}
    for r in rows:
        by_tipo[r["tipo"]] = by_tipo.get(r["tipo"], 0) + 1
    for t, n in sorted(by_tipo.items()):
        print(f"  {t}: {n}")
    print(f"[s5] escrito {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
