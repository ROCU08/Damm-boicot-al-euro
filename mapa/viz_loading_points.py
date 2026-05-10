"""Mapa con todos los puntos de carga/descarga del área de operaciones.

Dos capas:
  - Snap por cliente (loading_points_per_client.csv) → naranja
  - Zonas públicas OSM-tagged (loading_points_public.csv) → azul

Salida: mapa/out/viz_loading_points.png
"""
from __future__ import annotations

import argparse
import csv
from pathlib import Path

import contextily as cx
import matplotlib.pyplot as plt

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT_DIR = REPO_ROOT / "mapa" / "out"


def read_snap_points(path: Path) -> list[tuple[float, float, bool]]:
    """Devuelve (lat, lon, hgv_ok) por cliente snapeado."""
    out: list[tuple[float, float, bool]] = []
    with path.open("r", encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            lat_s = row.get("lat_snap") or row.get("lat_orig", "")
            lon_s = row.get("lon_snap") or row.get("lon_orig", "")
            if not lat_s or not lon_s:
                continue
            try:
                out.append((float(lat_s), float(lon_s), row.get("hgv_ok") == "1"))
            except ValueError:
                continue
    return out


def read_public(path: Path) -> list[tuple[float, float, str]]:
    out: list[tuple[float, float, str]] = []
    with path.open("r", encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            try:
                out.append((float(row["lat"]), float(row["lon"]), row.get("tipo", "")))
            except (ValueError, KeyError):
                continue
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    parser.add_argument("--png", type=Path, default=DEFAULT_OUT_DIR / "viz_loading_points.png")
    parser.add_argument("--dpi", type=int, default=200)
    parser.add_argument("--figsize", type=float, nargs=2, default=(14, 14))
    args = parser.parse_args()

    snaps = read_snap_points(args.out_dir / "loading_points_per_client.csv")
    publics = read_public(args.out_dir / "loading_points_public.csv")
    print(f"[viz] {len(snaps)} snap points, {len(publics)} public points")

    fig, ax = plt.subplots(figsize=tuple(args.figsize))

    # Snap clientes (separamos hgv_ok vs no)
    snap_ok = [(la, lo) for la, lo, ok in snaps if ok]
    snap_no = [(la, lo) for la, lo, ok in snaps if not ok]
    if snap_ok:
        lats, lons = zip(*snap_ok)
        ax.scatter(lons, lats, s=22, c="#ff7f0e", edgecolors="white", linewidths=0.4,
                   alpha=0.85, label=f"Cliente snap (hgv_ok, {len(snap_ok)})", zorder=3)
    if snap_no:
        lats, lons = zip(*snap_no)
        ax.scatter(lons, lats, s=22, c="#999999", edgecolors="white", linewidths=0.4,
                   alpha=0.7, label=f"Cliente snap (problemático, {len(snap_no)})", zorder=3)

    # Zonas públicas
    if publics:
        lats, lons = zip(*[(la, lo) for la, lo, _ in publics])
        ax.scatter(lons, lats, s=120, c="#1f77b4", marker="s", edgecolors="white",
                   linewidths=1.0, alpha=0.95,
                   label=f"Zona carga pública OSM ({len(publics)})", zorder=4)

    # Encuadre con margen
    all_lats = [la for la, _, _ in snaps] + [la for la, _, _ in publics]
    all_lons = [lo for _, lo, _ in snaps] + [lo for _, lo, _ in publics]
    if not all_lats:
        print("[viz] sin datos, abortando")
        return 1
    pad = 0.02
    ax.set_xlim(min(all_lons) - pad, max(all_lons) + pad)
    ax.set_ylim(min(all_lats) - pad, max(all_lats) + pad)

    cx.add_basemap(ax, source=cx.providers.OpenStreetMap.Mapnik, crs="EPSG:4326",
                   attribution_size=8)

    ax.set_xlabel("Longitud")
    ax.set_ylabel("Latitud")
    ax.set_title(f"Puntos de carga/descarga — área Vallès+Osona\n"
                 f"{len(snaps)} clientes (snap a calle) + {len(publics)} zonas públicas OSM")
    ax.legend(loc="upper left", framealpha=0.92, fontsize=10)
    ax.set_aspect("equal", adjustable="datalim")
    plt.tight_layout()

    args.png.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(args.png, dpi=args.dpi, bbox_inches="tight")
    print(f"[viz] guardado {args.png} ({args.png.stat().st_size/1024:.1f} KB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
