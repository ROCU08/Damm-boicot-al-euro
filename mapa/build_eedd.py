"""Orquestador del pipeline: corre s1..s6 en orden.

Cada paso ya es resumible (cache incremental). Podés saltarte pasos con --from-step.

Ejemplos:
    python -m mapa.build_eedd                              # corre todo de cero
    python -m mapa.build_eedd --from-step 4                # asume s1-s3 ya hechos y OSRM corriendo
    python -m mapa.build_eedd --skip-step 2                # no re-descarga el PBF
"""
from __future__ import annotations

import argparse
import importlib
import sys
from typing import Iterable

STEPS = [
    ("s1_compute_bbox", "bbox del área"),
    ("s2_download_osm", "descarga + recorte OSM"),
    ("s3_prepare_osrm", "preparación OSRM + daemon"),
    ("s4_snap_clients", "snap clientes a vías"),
    ("s5_overpass_parking", "catálogo zonas carga públicas"),
    ("s6_compute_matrices", "matrices distancia + tiempo"),
]


def run_step(modname: str, step_args: list[str]) -> int:
    mod = importlib.import_module(f"mapa.steps.{modname}")
    print(f"\n{'='*70}\n>>> {modname}\n{'='*70}")
    sys.argv = [modname] + step_args
    return mod.main()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--from-step", type=int, default=1, help="Empieza en este paso (1..6).")
    parser.add_argument("--to-step", type=int, default=6, help="Termina en este paso (1..6).")
    parser.add_argument("--skip-step", type=int, action="append", default=[],
                        help="Salta uno o más pasos (pass varias veces).")
    args, extra = parser.parse_known_args()

    for i, (mod, desc) in enumerate(STEPS, 1):
        if i < args.from_step or i > args.to_step or i in args.skip_step:
            print(f"-- saltando paso {i}: {desc}")
            continue
        rc = run_step(mod, extra)
        if rc != 0:
            print(f"!! paso {i} ({mod}) falló (exit={rc}); abortando", file=sys.stderr)
            return rc
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
