"""Extrae los inputs del SA-distribución a partir de los outputs del SA-ruta.

Después de correr `damm --from-json datos_<dia>.json salida.json`, este módulo:

1. Cruza datos_<dia>.json (cliente interno ↔ Destino_ID externo) con el
   juego_<dia>.csv original para obtener los items de cada cliente.
2. Por cada ruta resultante en salida.json, construye un RutaInputDist con:
     - camion_id, n_palets (del JSON datos)
     - orden_ruta = clientes en el orden REAL de visita (concatenando las
       listas clientes_atendidos por visita)
     - items[] = expansión de los items de cada cliente, con la regla
       "barril = 4 ítems-caja idénticos" para ocupar 4 slots del SA-distr.

Uso como CLI (modo debug, escribe ficheros camion_<id>.json):
    python pipeline/items_por_camion.py \\
        --datos datos_lunes.json \\
        --ruta salida.json \\
        --juego test/juego_lunes.csv \\
        --output-dir dist_inputs/

Uso como librería:
    from pipeline.items_por_camion import extraer_inputs_dist
    inputs = extraer_inputs_dist("datos_lunes.json", "salida.json", "test/juego_lunes.csv")
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import sys
from dataclasses import asdict
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
ROOT = SCRIPT_DIR.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from pipeline.contratos import (  # noqa: E402
    ItemDist, RutaInputDist, SaParams, to_json,
)


UNIDADES_POR_CAJA = 12


def _expandir_items(uma: str, es_barril: bool, cantidad: int) -> int:
    """Devuelve el número de slots (ítems-caja) que ocupa una línea no-envase."""
    if cantidad <= 0:
        return 0
    if es_barril:
        return cantidad * 4
    if uma == "CAJ":
        return cantidad
    if uma == "UN":
        return int(math.ceil(cantidad / UNIDADES_POR_CAJA))
    return cantidad


def _construir_items_por_cliente(
    juego_path: Path,
    destino_to_interno: dict[str, int],
    material_to_uint: dict[str, int],
) -> dict[int, list[ItemDist]]:
    """Para cada cliente interno, lista de ItemDist (1 ítem = 1 slot del camión).

    Los Material_ID se enumeran globalmente al uint16 que necesita el SA-distr.
    `material_to_uint` se pasa por referencia y se mutará con los nuevos códigos.
    """
    out: dict[int, list[ItemDist]] = {cid: [] for cid in destino_to_interno.values()}
    next_uint = max(material_to_uint.values(), default=0) + 1

    with juego_path.open(encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            dest = row["Destino_ID"].strip()
            if dest not in destino_to_interno:
                continue
            ubic = row["Ubic_Almacen"].strip().upper()
            if ubic == "ENVASE":
                continue   # retornable, no se carga al inicio

            mat_codigo = row["Material_ID"].strip()
            uma = row["UMA"].strip().upper()
            es_barril = row["Es_Barril"].strip() == "1"
            try:
                cant = int(float(row["Cantidad"]))
            except ValueError:
                continue
            n_slots = _expandir_items(uma, es_barril, cant)
            if n_slots == 0:
                continue

            if mat_codigo not in material_to_uint:
                material_to_uint[mat_codigo] = next_uint
                next_uint += 1
            mat_uint = material_to_uint[mat_codigo]
            cli_id = destino_to_interno[dest]

            for _ in range(n_slots):
                out[cli_id].append(ItemDist(
                    material_id=mat_uint,
                    material_codigo=mat_codigo,
                    cliente_id=cli_id,
                    es_barril=es_barril,
                ))
    return out


def extraer_inputs_dist(
    datos_path: str | Path,
    ruta_path: str | Path,
    juego_path: str | Path,
    sa_params: SaParams | None = None,
) -> tuple[list[RutaInputDist], dict[str, int]]:
    """Devuelve (inputs_por_camion, material_to_uint).

    `material_to_uint` se devuelve para que el orquestador lo persista
    (lo necesita el frontend para mapear material_id ↔ código humano).
    """
    datos = json.loads(Path(datos_path).read_text(encoding="utf-8"))
    salida = json.loads(Path(ruta_path).read_text(encoding="utf-8"))

    # cliente interno (id) -> destino externo (Destino_ID string)
    destino_to_interno: dict[str, int] = {}
    for c in datos["clientes"]:
        ext = c.get("destino_id_externo", "")
        if ext:
            destino_to_interno[ext] = int(c["id"])

    material_to_uint: dict[str, int] = {}
    items_por_cliente = _construir_items_por_cliente(
        Path(juego_path), destino_to_interno, material_to_uint,
    )

    sa_params = sa_params or SaParams()
    inputs: list[RutaInputDist] = []

    for ruta in salida["solucion"]["rutas"]:
        camion_id = int(ruta["camion_id"])
        n_palets = int(datos["camiones"][camion_id].get("n_palets", 0))

        # Concatena los clientes_atendidos en el orden de visita.
        orden_ruta: list[int] = []
        for visita in ruta["clientes_atendidos"]:
            orden_ruta.extend(int(c) for c in visita)

        items: list[ItemDist] = []
        for cli_id in orden_ruta:
            items.extend(items_por_cliente.get(cli_id, []))

        inputs.append(RutaInputDist(
            camion_id=camion_id,
            n_palets=n_palets,
            orden_ruta=orden_ruta,
            items=items,
            sa_params=sa_params,
        ))

    return inputs, material_to_uint


# =============================================================================
# CLI
# =============================================================================

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--datos", type=Path, required=True,
                        help="datos_<dia>.json (output de cargar_csv.py)")
    parser.add_argument("--ruta", type=Path, required=True,
                        help="salida.json (output de damm)")
    parser.add_argument("--juego", type=Path, required=True,
                        help="test/juego_<dia>.csv (productos por línea)")
    parser.add_argument("--output-dir", type=Path, required=True,
                        help="Directorio donde escribir camion_<id>.json")
    parser.add_argument("--temp-ini", type=float, default=1000.0)
    parser.add_argument("--temp-fin", type=float, default=0.01)
    parser.add_argument("--cooling", type=float, default=0.95)
    parser.add_argument("--iters-temp", type=int, default=100)
    args = parser.parse_args()

    sa = SaParams(
        temp_ini=args.temp_ini, temp_fin=args.temp_fin,
        cooling=args.cooling, iters_temp=args.iters_temp,
    )

    inputs, material_to_uint = extraer_inputs_dist(
        args.datos, args.ruta, args.juego, sa,
    )

    args.output_dir.mkdir(parents=True, exist_ok=True)

    print(f"=== Inputs SA-distribución ===")
    print(f"  {len(inputs)} camiones procesados\n")
    for inp in inputs:
        out_path = args.output_dir / f"camion_{inp.camion_id}.json"
        to_json(inp, out_path)
        n_barriles = sum(1 for it in inp.items if it.es_barril) // 4  # 4 slots por barril
        n_cajas = sum(1 for it in inp.items if not it.es_barril)
        utilizacion = (len(inp.items) / max(inp.n_palets * 60, 1)) * 100
        print(f"  Camion {inp.camion_id}: {len(inp.items)} slots "
              f"({n_cajas} cajas + {n_barriles} barriles) "
              f"[{utilizacion:.0f}% de {inp.n_palets} palets] "
              f"-> {out_path.name}")

    # Persistir el mapping material para el frontend.
    map_path = args.output_dir / "material_id_map.json"
    map_path.write_text(json.dumps(material_to_uint, indent=2, ensure_ascii=False),
                        encoding="utf-8")
    print(f"\n  Mapping material codigo -> uint16: {map_path.name} "
          f"({len(material_to_uint)} materiales)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
