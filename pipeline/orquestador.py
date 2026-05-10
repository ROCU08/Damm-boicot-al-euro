"""Orquestador end-to-end del pipeline VRP de Damm.

   juego_<dia>.csv
        │   pipeline/cargar_csv.py
        ▼
   datos_<dia>.json   ──►   ./damm --from-json   ──►   salida_ruta.json
                                                          │
                                                          ▼   pipeline/items_por_camion.py
                                                  dist_inputs/camion_*.json
                                                          │
                                                          ▼   ./damm-dist  (en paralelo, 1 por camión)
                                                  dist_outputs/camion_*.json
                                                          │
                                                          ▼   merge
                                                    resultado.json   ◄── lo que carga el frontend

Uso típico:
    python pipeline/orquestador.py --juego test/juego_lunes.csv --out-dir out_lunes/

Por defecto los workers son N = #cpu_count - 1, capado a número de camiones.
"""

from __future__ import annotations

import argparse
import json
import multiprocessing
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
ROOT = SCRIPT_DIR.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from pipeline.cargar_csv import (  # noqa: E402
    FLOTA_DEFAULT, construir_datos_problema, parse_flota, detectar_dia_num,
    DEPOSITO_DEFAULT_LAT, DEPOSITO_DEFAULT_LON,
)
from pipeline.contratos import to_json  # noqa: E402
from pipeline.items_por_camion import extraer_inputs_dist  # noqa: E402
from pipeline.horarios import (  # noqa: E402
    MINUTOS_POR_VOLUMEN_DEFAULT, visitas_con_horarios,
)


# =============================================================================
# Localización de binarios
# =============================================================================

def _find_binary(nombres: list[str]) -> Path:
    """Busca el primer binario existente en la raíz del proyecto.
    En Windows preferimos `.exe`. Lanza FileNotFoundError si no se encuentra.
    """
    for n in nombres:
        candidato = ROOT / n
        if candidato.exists():
            return candidato
    raise FileNotFoundError(
        f"No se encuentra ninguno de {nombres} en {ROOT}. "
        "Compila con `make damm damm-dist` antes de orquestar."
    )


def _binario_damm() -> Path:
    return _find_binary(["damm.exe", "damm"])


def _binario_damm_dist() -> Path:
    return _find_binary(["damm-dist.exe", "damm-dist"])


# =============================================================================
# Pasos del pipeline
# =============================================================================

def paso1_cargar_csv(args, out_datos: Path) -> Path:
    """Genera datos_<dia>.json a partir del juego CSV."""
    dia_num = detectar_dia_num(args.juego, args.dia_semana)
    print(f"\n=== [1/5] Cargando CSV → {out_datos.name} (día semana = {dia_num}) ===")
    datos = construir_datos_problema(
        juego_path=args.juego,
        cliente_index_path=args.cliente_index,
        dist_matrix_path=args.dist_matrix,
        time_matrix_path=args.time_matrix,
        horarios_path=args.horarios,
        dia_num=dia_num,
        deposito_lat=args.deposito_lat,
        deposito_lon=args.deposito_lon,
        flota=args.flota,
    )
    to_json(datos, out_datos)
    return out_datos


def paso2_correr_damm(datos_path: Path, out_ruta: Path, seed: int,
                      greedy: str, vecino: str) -> Path:
    """Invoca ./damm --from-json datos.json output.json [seed] [greedy] [vecino]."""
    binario = _binario_damm()
    print(f"\n=== [2/5] SA-ruta: {binario.name} → {out_ruta.name} ===")
    t0 = time.time()
    cmd = [str(binario), "--from-json", str(datos_path), str(out_ruta),
           str(seed), greedy, vecino]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        print(proc.stdout, file=sys.stderr)
        print(proc.stderr, file=sys.stderr)
        raise RuntimeError(f"damm falló (rc={proc.returncode})")
    # Imprimimos un resumen recortado del stdout para no inundar.
    for line in proc.stdout.splitlines():
        if line.strip():
            print(f"  {line}")
    print(f"  -> {time.time() - t0:.1f}s")
    return out_ruta


def paso3_extraer_items(datos_path: Path, ruta_path: Path, juego_path: Path,
                        dist_inputs_dir: Path) -> tuple[list[Path], dict[str, int]]:
    """Construye dist_inputs/camion_*.json + material_id_map.json.
    Devuelve (lista de paths de inputs por camión, mapping material -> uint16).
    """
    print(f"\n=== [3/5] Items por camión → {dist_inputs_dir.name}/ ===")
    dist_inputs_dir.mkdir(parents=True, exist_ok=True)

    inputs, mat_map = extraer_inputs_dist(datos_path, ruta_path, juego_path)
    paths: list[Path] = []
    for inp in inputs:
        p = dist_inputs_dir / f"camion_{inp.camion_id}.json"
        to_json(inp, p)
        paths.append(p)
        n_barriles = sum(1 for it in inp.items if it.es_barril) // 4
        n_cajas = sum(1 for it in inp.items if not it.es_barril)
        cap = max(inp.n_palets * 60, 1)
        print(f"  Camión {inp.camion_id}: {len(inp.items)} slots "
              f"({n_cajas} cajas + {n_barriles} barriles) "
              f"[{len(inp.items) / cap * 100:.0f}% de capacidad]")

    map_path = dist_inputs_dir / "material_id_map.json"
    map_path.write_text(json.dumps(mat_map, indent=2, ensure_ascii=False), encoding="utf-8")
    return paths, mat_map


def _correr_damm_dist(args_tuple: tuple[Path, Path, Path, int, bool]) -> tuple[int, str]:
    """Worker: invoca un damm-dist por camión. Devuelve (camion_id, log_resumen)."""
    binario, input_path, output_path, seed, verbose = args_tuple
    cmd = [str(binario), str(input_path), str(output_path), str(seed)]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    cid = int(input_path.stem.split("_")[-1])
    if proc.returncode != 0:
        return cid, f"❌ camion {cid} falló (rc={proc.returncode}):\n{proc.stderr}"
    if verbose:
        return cid, proc.stdout
    # Resumen de 1 línea para no inundar.
    fitness_ini = fitness_fin = mejora = secs = "?"
    for line in proc.stdout.splitlines():
        line = line.strip()
        if line.startswith("fitness inicial"):
            fitness_ini = line.split("=")[-1].strip()
        elif line.startswith("fitness final"):
            fitness_fin = line.split("=")[-1].strip()
        elif line.startswith("mejora"):
            mejora = line.split("=")[-1].strip()
        elif line.startswith("tiempo SA"):
            secs = line.split("=")[-1].strip()
    return cid, (f"  Camión {cid}: fitness {fitness_ini} → {fitness_fin} "
                 f"({mejora}) en {secs}")


def paso4_correr_dist_paralelo(input_paths: list[Path], dist_outputs_dir: Path,
                               seed: int, workers: int, verbose: bool) -> list[Path]:
    """Lanza damm-dist por cada input en paralelo con ThreadPoolExecutor.
    (Threads, no procesos, porque cada worker bloquea en subprocess.run.)
    """
    print(f"\n=== [4/5] SA-distribución × {len(input_paths)} en paralelo "
          f"(workers={workers}) → {dist_outputs_dir.name}/ ===")
    dist_outputs_dir.mkdir(parents=True, exist_ok=True)
    binario = _binario_damm_dist()

    output_paths: list[Path] = []
    args_list = []
    for inp in input_paths:
        out_path = dist_outputs_dir / inp.name
        output_paths.append(out_path)
        args_list.append((binario, inp, out_path, seed, verbose))

    t0 = time.time()
    with ThreadPoolExecutor(max_workers=workers) as ex:
        for cid, msg in ex.map(_correr_damm_dist, args_list):
            print(msg)
    print(f"  -> total {time.time() - t0:.1f}s")
    return output_paths


def paso5_merge(datos_path: Path, ruta_path: Path,
                dist_output_paths: list[Path],
                resultado_path: Path,
                minutos_por_volumen: float) -> Path:
    """Combina los outputs en un único ResultadoFinal JSON listo para el frontend."""
    print(f"\n=== [5/5] Merge → {resultado_path.name} ===")
    datos = json.loads(datos_path.read_text(encoding="utf-8"))
    salida = json.loads(ruta_path.read_text(encoding="utf-8"))

    # Rutas enriquecidas con visitas/horarios
    rutas_out = []
    for r in salida["solucion"]["rutas"]:
        visitas = visitas_con_horarios(r, datos, minutos_por_volumen)
        rutas_out.append({
            "camion_id": int(r["camion_id"]),
            "visitas": visitas,
            "total_distancia": float(r["total_distancia"]),
            "total_carga_inicial": float(r["total_carga_inicial"]),
            "total_pico_volumen": float(r["total_pico_volumen"]),
            "total_retraso": float(r["total_retraso"]),
        })

    # Distribuciones (una por camión)
    distribuciones = []
    for p in dist_output_paths:
        if p.exists():
            distribuciones.append(json.loads(p.read_text(encoding="utf-8")))

    # KPIs agregados
    no_servidos = list(salida["solucion"].get("no_servidos", []))
    total_clientes = len(datos["clientes"])
    distancia_total = sum(r["total_distancia"] for r in rutas_out)
    # tiempo_total: suma de hora_llegada_final - hora_inicio_camion por ruta
    tiempo_total = 0.0
    for r in rutas_out:
        camion = datos["camiones"][r["camion_id"]]
        if r["visitas"]:
            ultima = r["visitas"][-1]
            tiempo_total += ultima["hora_llegada_estimada"] - camion["hora_inicio"]
    utilizacion = []
    for r in rutas_out:
        camion = datos["camiones"][r["camion_id"]]
        cap = max(camion["capacidad_volumen"], 1)
        utilizacion.append(r["total_pico_volumen"] / cap)

    kpis = {
        "coste_total": float(salida.get("coste_total", 0.0)),
        "distancia_total_m": distancia_total,
        "tiempo_total_min": float(tiempo_total),
        "clientes_servidos": total_clientes - len(no_servidos),
        "clientes_no_servidos": no_servidos,
        "utilizacion_por_camion": utilizacion,
    }

    resultado = {
        "datos": datos,
        "rutas": rutas_out,
        "distribuciones": distribuciones,
        "kpis": kpis,
        "no_servidos": no_servidos,
    }
    resultado_path.write_text(json.dumps(resultado, indent=2, ensure_ascii=False), encoding="utf-8")

    # Sumario
    print(f"  rutas:           {len(rutas_out)}")
    print(f"  distribuciones:  {len(distribuciones)}/{len(rutas_out)}")
    print(f"  cobertura:       {kpis['clientes_servidos']}/{total_clientes} clientes")
    print(f"  distancia total: {distancia_total/1000:.1f} km")
    print(f"  tamaño JSON:     {resultado_path.stat().st_size / 1024:.0f} KB")
    return resultado_path


# =============================================================================
# CLI
# =============================================================================

def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--juego", type=Path, required=True,
                        help="test/juego_<dia>.csv")
    parser.add_argument("--out-dir", type=Path, required=True,
                        help="Directorio donde escribir todos los intermedios + resultado.json")

    # Cargador
    parser.add_argument("--cliente-index", type=Path,
                        default=ROOT / "mapa" / "out" / "cliente_index.csv")
    parser.add_argument("--dist-matrix", type=Path,
                        default=ROOT / "mapa" / "out" / "dist_matrix.csv")
    parser.add_argument("--time-matrix", type=Path,
                        default=ROOT / "mapa" / "out" / "time_matrix.csv")
    parser.add_argument("--horarios", type=Path,
                        default=ROOT / "test" / "Horarios Entrega.csv")
    parser.add_argument("--dia-semana", type=int, default=None)
    parser.add_argument("--deposito-lat", type=float, default=DEPOSITO_DEFAULT_LAT)
    parser.add_argument("--deposito-lon", type=float, default=DEPOSITO_DEFAULT_LON)
    parser.add_argument("--flota", type=parse_flota, default=FLOTA_DEFAULT,
                        help="'n_furgo,n_cam6,n_cam8' (default: 2,3,1)")

    # SA-ruta
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--greedy", default="set_cover", choices=["set_cover", "cliente"])
    parser.add_argument("--vecino", default="completo",
                        choices=["intra", "intra_inter", "paradas", "completo"])

    # SA-dist + paralelización
    parser.add_argument("--workers", type=int, default=0,
                        help="0 = auto (cpu_count - 1, capado a #camiones)")
    parser.add_argument("--verbose-dist", action="store_true",
                        help="Muestra el stdout completo de cada damm-dist")

    # Otros
    parser.add_argument("--minutos-por-volumen", type=float, default=MINUTOS_POR_VOLUMEN_DEFAULT,
                        help="Tiempo de servicio por unidad de volumen (default: 0.5 min/caja)")

    args = parser.parse_args()

    if not args.juego.exists():
        print(f"❌ Juego no encontrado: {args.juego}", file=sys.stderr)
        return 1

    args.out_dir.mkdir(parents=True, exist_ok=True)
    datos_path = args.out_dir / "datos.json"
    ruta_path = args.out_dir / "salida_ruta.json"
    dist_inputs_dir = args.out_dir / "dist_inputs"
    dist_outputs_dir = args.out_dir / "dist_outputs"
    resultado_path = args.out_dir / "resultado.json"

    t_total = time.time()

    # 1. CSV → DatosProblema JSON
    paso1_cargar_csv(args, datos_path)

    # 2. SA-ruta
    paso2_correr_damm(datos_path, ruta_path, args.seed, args.greedy, args.vecino)

    # 3. Items por camión
    input_paths, _ = paso3_extraer_items(datos_path, ruta_path, args.juego, dist_inputs_dir)

    # 4. SA-distribución × N camiones, paralelo
    workers = args.workers if args.workers > 0 else max(1, min(len(input_paths),
                                                                (multiprocessing.cpu_count() or 2) - 1))
    output_paths = paso4_correr_dist_paralelo(
        input_paths, dist_outputs_dir, args.seed, workers, args.verbose_dist,
    )

    # 5. Merge
    paso5_merge(datos_path, ruta_path, output_paths, resultado_path,
                args.minutos_por_volumen)

    print(f"\n✅ Pipeline completo en {time.time() - t_total:.1f}s")
    print(f"   Resultado: {resultado_path}")
    print(f"   Cárgalo en el frontend (npm run dev en frontend/) para visualizar.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
