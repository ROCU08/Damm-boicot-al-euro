"""Muestra de rutas reales de un día con sus paradas y zonas de carga públicas.

Toma N transportes (camiones) de un juego_*.csv, recorta a max_stops paradas,
pide a OSRM la polyline real respetando calles y sentidos, y plotea:
  - Polylines (una por ruta, color distinto)
  - Paradas / locales de pedido (puntos coloreados igual que su ruta)
  - Zonas públicas de carga OSM (cuadrados azules)

Salida: mapa/out/viz_sample_routes.png
"""
from __future__ import annotations

import argparse
import csv
import json
import sys
import urllib.request
from pathlib import Path

import contextily as cx
import matplotlib.pyplot as plt
from matplotlib import colormaps

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TEST_DIR = REPO_ROOT / "test"
DEFAULT_OUT_DIR = REPO_ROOT / "mapa" / "out"
DEFAULT_JUEGO = "juego_lunes.csv"
DEFAULT_OSRM = "http://localhost:5000"
TIMEOUT_S = 30.0


def collect_routes(juego_path: Path, max_stops: int) -> dict[str, list[tuple[str, float, float]]]:
    """Devuelve {Transporte: [(Destino_ID, lat, lon), ...]} en orden de aparición."""
    routes: dict[str, dict[str, tuple[float, float]]] = {}
    order: dict[str, list[str]] = {}
    with juego_path.open("r", encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            t = (row.get("Transporte") or "").strip()
            cid = (row.get("Destino_ID") or "").strip()
            coords = (row.get("Coordenadas") or "").strip()
            if not t or not cid or not coords or "," not in coords:
                continue
            try:
                lat_s, lon_s = coords.split(",", 1)
                lat, lon = float(lat_s), float(lon_s)
            except ValueError:
                continue
            if cid not in routes.setdefault(t, {}):
                routes[t][cid] = (lat, lon)
                order.setdefault(t, []).append(cid)
    out: dict[str, list[tuple[str, float, float]]] = {}
    for t, cids in order.items():
        cids = cids[:max_stops]
        out[t] = [(cid, *routes[t][cid]) for cid in cids]
    return out


def fetch_polyline(osrm: str, stops: list[tuple[str, float, float]]) -> list[tuple[float, float]]:
    """Llama OSRM /route con geometries=geojson y devuelve [(lon, lat), ...]."""
    coords_str = ";".join(f"{lon:.6f},{lat:.6f}" for _, lat, lon in stops)
    url = f"{osrm}/route/v1/driving/{coords_str}?overview=full&geometries=geojson"
    try:
        with urllib.request.urlopen(url, timeout=TIMEOUT_S) as r:
            data = json.loads(r.read())
    except Exception as e:
        print(f"  OSRM error: {type(e).__name__}: {e}", file=sys.stderr)
        return []
    if data.get("code") != "Ok" or not data.get("routes"):
        return []
    geom = data["routes"][0].get("geometry") or {}
    return geom.get("coordinates") or []  # [[lon,lat], ...]


def read_public(path: Path) -> list[tuple[float, float]]:
    out: list[tuple[float, float]] = []
    if not path.exists():
        return out
    with path.open("r", encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            try:
                out.append((float(row["lat"]), float(row["lon"])))
            except (ValueError, KeyError):
                continue
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--juego", type=Path, default=DEFAULT_TEST_DIR / DEFAULT_JUEGO)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    parser.add_argument("--osrm", default=DEFAULT_OSRM)
    parser.add_argument("--n-routes", type=int, default=20, help="Número de rutas a muestrear.")
    parser.add_argument("--max-stops", type=int, default=20, help="Máximo paradas por ruta.")
    parser.add_argument("--png", type=Path, default=None,
                        help="Por defecto: out_dir/viz_sample_routes_<juego>.png")
    parser.add_argument("--dpi", type=int, default=200)
    parser.add_argument("--figsize", type=float, nargs=2, default=(15, 14))
    parser.add_argument("--zoom-bbox", type=float, nargs=4, metavar=("LAT_MIN", "LON_MIN", "LAT_MAX", "LON_MAX"),
                        help="Recorta a esta vista en lugar del bbox del área completa.")
    args = parser.parse_args()

    routes = collect_routes(args.juego, args.max_stops)
    print(f"[viz] {args.juego.name}: {len(routes)} transportes únicos")

    # Selección: top-N por número de paradas (las rutas más sustanciosas se ven mejor)
    sorted_routes = sorted(routes.items(), key=lambda kv: -len(kv[1]))[: args.n_routes]
    print(f"[viz] muestreando {len(sorted_routes)} rutas ({sum(len(s) for _, s in sorted_routes)} paradas totales)")

    fig, ax = plt.subplots(figsize=tuple(args.figsize))
    cmap = colormaps["tab20"]

    all_lats: list[float] = []
    all_lons: list[float] = []

    for i, (t, stops) in enumerate(sorted_routes):
        color = cmap(i % 20)
        polyline = fetch_polyline(args.osrm, stops)
        if polyline:
            xs = [p[0] for p in polyline]
            ys = [p[1] for p in polyline]
            ax.plot(xs, ys, color=color, linewidth=2.4, alpha=0.85, zorder=2,
                    solid_joinstyle="round", solid_capstyle="round")
            all_lons.extend(xs)
            all_lats.extend(ys)
        # Paradas (color de la ruta)
        sl = [s[1] for s in stops]
        slo = [s[2] for s in stops]
        ax.scatter(slo, sl, s=36, color=color, edgecolors="black", linewidths=0.6,
                   zorder=4, label=f"T{t} ({len(stops)} paradas)")
        all_lats.extend(sl)
        all_lons.extend(slo)

    # Zonas públicas (todas, contexto)
    publics = read_public(args.out_dir / "loading_points_public.csv")
    if publics:
        plats, plons = zip(*publics)
        ax.scatter(plons, plats, s=140, c="#1f77b4", marker="s", edgecolors="white",
                   linewidths=1.0, alpha=0.95, zorder=5,
                   label=f"Zona carga pública OSM ({len(publics)})")
        all_lats.extend(plats)
        all_lons.extend(plons)

    if args.zoom_bbox:
        lat_min, lon_min, lat_max, lon_max = args.zoom_bbox
        ax.set_xlim(lon_min, lon_max)
        ax.set_ylim(lat_min, lat_max)
    else:
        bbox_path = args.out_dir / "bbox.json"
        if bbox_path.exists():
            bb = json.loads(bbox_path.read_text(encoding="utf-8"))
            ax.set_xlim(bb["lon_min"], bb["lon_max"])
            ax.set_ylim(bb["lat_min"], bb["lat_max"])
        else:
            pad = 0.015
            ax.set_xlim(min(all_lons) - pad, max(all_lons) + pad)
            ax.set_ylim(min(all_lats) - pad, max(all_lats) + pad)

    cx.add_basemap(ax, source=cx.providers.OpenStreetMap.Mapnik, crs="EPSG:4326",
                   attribution_size=8)
    ax.set_xlabel("Longitud")
    ax.set_ylabel("Latitud")
    ax.set_title(f"Muestra de {len(sorted_routes)} rutas reales — {args.juego.name}\n"
                 f"Cada color = un Transporte (camión); cuadrados azules = zonas de carga públicas")
    ax.legend(loc="upper left", framealpha=0.92, fontsize=7, ncol=2)
    ax.set_aspect("equal", adjustable="datalim")
    plt.tight_layout()

    png = args.png or (args.out_dir / f"viz_sample_routes_{args.juego.stem}.png")
    png.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(png, dpi=args.dpi, bbox_inches="tight")
    print(f"[viz] guardado {png} ({png.stat().st_size/1024:.1f} KB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
