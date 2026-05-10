"""s6: matrices distancia + tiempo entre todos los Destino_ID con coordenadas.

Pasos:
  1. Reúne todos los Destino_ID únicos con coords de los CSV juego_*.csv.
  2. Asigna un índice 0..N-1 estable (ordenado por Destino_ID).
  3. Llama a OSRM /table en chunks (max-table-size=5000 por defecto del daemon).
  4. Escribe `mapa/out/cliente_index.csv`, `dist_matrix.csv`, `time_matrix.csv`.

`-1` en una celda significa que OSRM no encontró ruta entre ese par de puntos.

Ejecutalo cuando ya tengas:
  - mapa/out/bbox.json (s1)
  - OSRM corriendo en localhost:5000 (s3)
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import sys
import time
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_TEST_DIR = REPO_ROOT / "test"
DEFAULT_OUT_DIR = REPO_ROOT / "mapa" / "out"
DEFAULT_OSRM = "http://localhost:5000"
DEFAULT_CHUNK = 100  # OSRM /table acepta hasta max-table-size; 100×100 = 10000 entradas, seguro
TIMEOUT_S = 60.0
DELAY_S = 0.05


def collect_destinos(test_dir: Path) -> dict[str, tuple[float, float]]:
    """Lee todos los juego_*.csv y devuelve {Destino_ID: (lat, lon)} ordenado por Destino_ID."""
    out: dict[str, tuple[float, float]] = {}
    for path in sorted(test_dir.glob("juego_*.csv")):
        with path.open("r", encoding="utf-8", newline="") as f:
            for row in csv.DictReader(f):
                coords = (row.get("Coordenadas") or "").strip()
                cid = (row.get("Destino_ID") or "").strip()
                if not coords or not cid or "," not in coords:
                    continue
                if cid in out:
                    continue
                lat_s, lon_s = coords.split(",", 1)
                try:
                    out[cid] = (float(lat_s), float(lon_s))
                except ValueError:
                    continue
    return dict(sorted(out.items()))


def write_index(path: Path, destinos: dict[str, tuple[float, float]]) -> list[tuple[str, float, float]]:
    items = [(cid, lat, lon) for cid, (lat, lon) in destinos.items()]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as f:
        w = csv.writer(f)
        w.writerow(["idx", "Destino_ID", "lat", "lon"])
        for i, (cid, lat, lon) in enumerate(items):
            w.writerow([i, cid, f"{lat:.6f}", f"{lon:.6f}"])
    return items


def osrm_table(base: str, sources: list[tuple[float, float]], destinations: list[tuple[float, float]]) -> dict:
    """Llama /table con annotations=duration,distance. coords = lon,lat;lon,lat;..."""
    all_coords = sources + destinations
    coords_str = ";".join(f"{lon:.6f},{lat:.6f}" for lat, lon in all_coords)
    src_idx = ";".join(str(i) for i in range(len(sources)))
    dst_idx = ";".join(str(i + len(sources)) for i in range(len(destinations)))
    url = (
        f"{base}/table/v1/driving/{coords_str}"
        f"?annotations=duration,distance&sources={src_idx}&destinations={dst_idx}"
    )
    with urllib.request.urlopen(url, timeout=TIMEOUT_S) as r:
        return json.loads(r.read())


def init_matrix(n: int) -> list[list[float]]:
    return [[-1.0] * n for _ in range(n)]


def write_matrix(path: Path, m: list[list[float]], fmt: str = "{:.1f}") -> None:
    n = len(m)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as f:
        w = csv.writer(f)
        w.writerow([""] + list(range(n)))  # cabecera = índices destino
        for i, row in enumerate(m):
            w.writerow([i] + [fmt.format(v) if v >= 0 else "-1" for v in row])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test-dir", type=Path, default=DEFAULT_TEST_DIR)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    parser.add_argument("--osrm", default=DEFAULT_OSRM)
    parser.add_argument("--chunk", type=int, default=DEFAULT_CHUNK)
    args = parser.parse_args()

    destinos = collect_destinos(args.test_dir)
    n = len(destinos)
    print(f"[s6] {n} Destino_ID únicos con coords")
    if n == 0:
        return 1

    index_path = args.out_dir / "cliente_index.csv"
    items = write_index(index_path, destinos)
    print(f"[s6] escrito {index_path}")

    coords = [(lat, lon) for _, lat, lon in items]
    dist_m = init_matrix(n)
    time_m = init_matrix(n)

    n_chunks_per_side = math.ceil(n / args.chunk)
    total_calls = n_chunks_per_side * n_chunks_per_side
    print(f"[s6] {total_calls} llamadas a OSRM /table (chunks de {args.chunk})")

    t0 = time.time()
    call = 0
    for ci in range(n_chunks_per_side):
        i0, i1 = ci * args.chunk, min((ci + 1) * args.chunk, n)
        sources = coords[i0:i1]
        for cj in range(n_chunks_per_side):
            j0, j1 = cj * args.chunk, min((cj + 1) * args.chunk, n)
            dests = coords[j0:j1]
            call += 1
            try:
                resp = osrm_table(args.osrm, sources, dests)
            except Exception as e:
                print(f"  [{call}/{total_calls}] error: {type(e).__name__}: {e}", file=sys.stderr)
                continue
            durations = resp.get("durations") or []
            distances = resp.get("distances") or []
            for li in range(len(sources)):
                for lj in range(len(dests)):
                    d = distances[li][lj] if li < len(distances) and lj < len(distances[li]) else None
                    t = durations[li][lj] if li < len(durations) and lj < len(durations[li]) else None
                    dist_m[i0 + li][j0 + lj] = float(d) if d is not None else -1.0
                    time_m[i0 + li][j0 + lj] = float(t) if t is not None else -1.0
            elapsed = time.time() - t0
            print(f"  [{call}/{total_calls}] OK ({elapsed/call:.2f}s/call)")
            if DELAY_S:
                time.sleep(DELAY_S)

    write_matrix(args.out_dir / "dist_matrix.csv", dist_m, fmt="{:.1f}")
    write_matrix(args.out_dir / "time_matrix.csv", time_m, fmt="{:.1f}")

    # Sanity stats
    flat = [v for row in dist_m for v in row if v >= 0]
    if flat:
        print(f"[s6] dist (m): n={len(flat)} mean={sum(flat)/len(flat):.0f} "
              f"min={min(flat):.0f} max={max(flat):.0f}")
    flat = [v for row in time_m for v in row if v >= 0]
    if flat:
        print(f"[s6] time (s): n={len(flat)} mean={sum(flat)/len(flat):.0f} "
              f"min={min(flat):.0f} max={max(flat):.0f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
