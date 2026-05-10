"""Cargador CSV -> DatosProblema JSON.

Cruza:
  - test/juego_<dia>.csv               (pedidos: clientes, productos, cantidades)
  - mapa/out/cliente_index.csv         (Destino_ID -> idx, lat_snap, lon_snap)
  - mapa/out/dist_matrix.csv           (matriz NxN en metros, indexada por idx)
  - mapa/out/time_matrix.csv           (matriz NxN en segundos, indexada por idx)
  - test/Horarios Entrega.csv          (ventanas horarias por (Deudor, Día semana))

Reglas de conversión (cajas-equivalentes):
  - Líneas con Ubic_Almacen == 'ENVASE'   -> volumen_devolver del cliente (1 por unidad)
  - Líneas con Es_Barril == 1             -> Cantidad * 4 cajas-equivalentes (es_barril=true)
  - Líneas con UMA == 'CAJ', Es_Barril=0  -> Cantidad cajas
  - Líneas con UMA == 'UN',  Es_Barril=0  -> ceil(Cantidad / 12) cajas
  - Otras (PAL, etc., raras)              -> Cantidad cajas (asumimos ya son palet/caja)

Clientes presentes en el CSV pero ausentes del cliente_index se descartan con warning.

Uso:
    python pipeline/cargar_csv.py \
        --juego test/juego_lunes.csv \
        --dia-semana 1 \
        --output datos_lunes.json

    python pipeline/cargar_csv.py --juego test/juego_lunes.csv  \
        --flota 2,3,1   # 2 furgonetas + 3 cam6 + 1 cam8
"""

from __future__ import annotations

import argparse
import csv
import math
import sys
from collections import defaultdict
from dataclasses import asdict
from pathlib import Path

# Permite ejecutar el script directamente: añade la raíz al sys.path
SCRIPT_DIR = Path(__file__).resolve().parent
ROOT = SCRIPT_DIR.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from pipeline.contratos import (  # noqa: E402
    Camion, Cliente, Coord, DatosProblema, Parada,
    CAJAS_POR_PALET, TIPO_CAMION, TIPO_FURGONETA, to_json,
)


# =============================================================================
# Constantes / parámetros por defecto
# =============================================================================

UNIDADES_POR_CAJA = 12
JORNADA_INI_MIN = 8 * 60          # 08:00
JORNADA_FIN_MIN = 18 * 60         # 18:00

# Damm El Prat (factoría / centro logístico)
DEPOSITO_DEFAULT_LAT = 41.317
DEPOSITO_DEFAULT_LON = 2.085

# Velocidad para Haversine depósito ↔ paradas (km/h). Empleamos un valor
# urbano realista; la matriz interna ya está calculada con OSRM, esto solo
# afecta las aristas depósito↔parada que añadimos al grafo.
VELOCIDAD_KMH_DEPOSITO = 30.0

# Mapeo Día (de la columna Dia del CSV o flag CLI) -> Día semana de Horarios Entrega.csv
DIA_NOMBRE_A_NUM = {
    "Lunes": 1, "Martes": 2, "Miercoles": 3, "Miércoles": 3,
    "Jueves": 4, "Viernes": 5,
}

# Flota fija por defecto: tuplas (n_furgonetas, n_camion6, n_camion8).
# Capacidad total: 2*180 + 3*360 + 1*480 = 1920 cajas-equivalentes.
FLOTA_DEFAULT = (2, 3, 1)
PALETS_FURGONETA = 3
PALETS_CAMION_MEDIANO = 6
PALETS_CAMION_GRANDE = 8


# =============================================================================
# Utilidades
# =============================================================================

def haversine_m(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    """Distancia en metros entre dos coordenadas geográficas."""
    R = 6_371_000.0
    p1 = math.radians(lat1)
    p2 = math.radians(lat2)
    dp = math.radians(lat2 - lat1)
    dl = math.radians(lon2 - lon1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * R * math.asin(math.sqrt(a))


def parse_hora_a_minutos(s: str) -> int | None:
    """Parsea 'HH:MM:SS AM/PM' o 'HH:MM:SS' o 'HH:MM' a minutos desde 00:00.
    Devuelve None si no es parseable o si la cadena está vacía.
    """
    s = (s or "").strip()
    if not s:
        return None
    s_norm = s.upper().replace(" ", "")
    am_pm = None
    if s_norm.endswith("AM"):
        am_pm = "AM"
        s_norm = s_norm[:-2]
    elif s_norm.endswith("PM"):
        am_pm = "PM"
        s_norm = s_norm[:-2]
    parts = s_norm.split(":")
    try:
        h = int(parts[0])
        m = int(parts[1]) if len(parts) > 1 else 0
    except (ValueError, IndexError):
        return None
    if am_pm == "AM" and h == 12:
        h = 0
    elif am_pm == "PM" and h != 12:
        h += 12
    return h * 60 + m


def parse_int_cantidad(s: str) -> int:
    s = (s or "").strip()
    if not s:
        return 0
    try:
        return int(float(s))
    except ValueError:
        return 0


# =============================================================================
# Carga de fuentes
# =============================================================================

def cargar_cliente_index(path: Path) -> dict[str, dict]:
    """Devuelve mapping Destino_ID (string) -> {idx, lat, lon}."""
    out: dict[str, dict] = {}
    with path.open(encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            out[row["Destino_ID"].strip()] = {
                "idx": int(row["idx"]),
                "lat": float(row["lat"]),
                "lon": float(row["lon"]),
            }
    return out


def cargar_matriz(path: Path) -> list[list[float]]:
    """Lee una matriz NxN cuya primera columna es el índice de fila."""
    with path.open(encoding="utf-8", newline="") as f:
        reader = csv.reader(f)
        next(reader)  # cabecera (índices de columna)
        rows: list[list[float]] = []
        for line in reader:
            # primera celda es el índice de fila → la saltamos
            rows.append([float(x) if x else 0.0 for x in line[1:]])
    return rows


def cargar_horarios(path: Path) -> dict[tuple[str, int], dict]:
    """Indexa Horarios Entrega por (Deudor, Día semana) -> {ini, fin, cierre}.
    Solo guarda Turno=1 (la primera ventana del día).
    """
    out: dict[tuple[str, int], dict] = {}
    with path.open(encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            try:
                dia_num = int(row["Día semana"])
            except (ValueError, KeyError):
                continue
            deudor = row["Deudor"].strip()
            turno = row.get("Turno", "").strip()
            key = (deudor, dia_num)
            # preferir Turno=1 si ya hay otro
            if key in out and turno != "1":
                continue
            out[key] = {
                "ini": parse_hora_a_minutos(row.get("Horario inicia a", "")),
                "fin": parse_hora_a_minutos(row.get("Horario termina a", "")),
                "cierre": row.get("Cierre Si/No", "").strip().upper(),
            }
    return out


def ventana_horaria(horario: dict | None) -> tuple[int, int]:
    """Aplica las reglas: cierre, ventana inválida o ausente -> jornada completa."""
    if not horario:
        return JORNADA_INI_MIN, JORNADA_FIN_MIN
    if horario.get("cierre") == "X":
        return JORNADA_INI_MIN, JORNADA_FIN_MIN
    ini = horario.get("ini")
    fin = horario.get("fin")
    if ini is None or fin is None or ini >= fin:
        return JORNADA_INI_MIN, JORNADA_FIN_MIN
    return ini, fin


# =============================================================================
# Agregación de pedidos por cliente
# =============================================================================

def linea_a_cajas(uma: str, es_barril: bool, cantidad: int) -> float:
    """Convierte una línea de producto (no-envase) a cajas-equivalentes."""
    if es_barril:
        return cantidad * 4.0
    if uma == "CAJ":
        return float(cantidad)
    if uma == "UN":
        # cualquier cosa más pequeña que una caja ocupa una caja entera
        return float(math.ceil(cantidad / UNIDADES_POR_CAJA))
    # PAL u otros raros: tratar como cajas directas
    return float(cantidad)


def agregar_pedidos(juego_path: Path) -> tuple[dict[str, dict], list[tuple[str, ...]]]:
    """Lee juego_<dia>.csv y agrupa por Destino_ID.

    Devuelve:
      - clientes_raw: dict Destino_ID -> {
            'nombre': str,
            'volumen_recoger': float,           # cajas-equivalentes
            'volumen_devolver': float,          # cajas-equivalentes (envases)
            'lineas_carga': [                   # para SA-dist (no-envases)
                (Material_ID, UMA, Es_Barril, Cantidad), ...
            ],
        }
      - filas: lista cruda de filas (informativa)
    """
    clientes_raw: dict[str, dict] = {}
    filas_raw: list[tuple[str, ...]] = []

    with juego_path.open(encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            dest = row["Destino_ID"].strip()
            if not dest:
                continue
            mat = row["Material_ID"].strip()
            uma = row["UMA"].strip().upper()
            es_barril = row["Es_Barril"].strip() == "1"
            ubic = row["Ubic_Almacen"].strip().upper()
            cant = parse_int_cantidad(row["Cantidad"])
            nombre = row["Cliente_Nombre"].strip()

            entry = clientes_raw.setdefault(dest, {
                "nombre": nombre,
                "volumen_recoger": 0.0,
                "volumen_devolver": 0.0,
                "lineas_carga": [],
            })
            if not entry["nombre"] and nombre:
                entry["nombre"] = nombre

            if ubic == "ENVASE":
                # Retornable: sólo cuenta para volumen_devolver del SA-ruta.
                entry["volumen_devolver"] += float(cant)
                continue

            cajas_eq = linea_a_cajas(uma, es_barril, cant)
            entry["volumen_recoger"] += cajas_eq
            entry["lineas_carga"].append((mat, uma, es_barril, cant))
            filas_raw.append((dest, mat, uma, str(es_barril), str(cant)))

    return clientes_raw, filas_raw


# =============================================================================
# Construcción de la flota
# =============================================================================

def construir_flota(n_furgo: int, n_cam6: int, n_cam8: int) -> list[Camion]:
    flota: list[Camion] = []
    cid = 0
    for _ in range(n_furgo):
        flota.append(Camion(
            id=cid, tipo=TIPO_FURGONETA, n_palets=PALETS_FURGONETA,
            capacidad_volumen=PALETS_FURGONETA * CAJAS_POR_PALET,
            hora_inicio=JORNADA_INI_MIN,
        ))
        cid += 1
    for _ in range(n_cam6):
        flota.append(Camion(
            id=cid, tipo=TIPO_CAMION, n_palets=PALETS_CAMION_MEDIANO,
            capacidad_volumen=PALETS_CAMION_MEDIANO * CAJAS_POR_PALET,
            hora_inicio=JORNADA_INI_MIN,
        ))
        cid += 1
    for _ in range(n_cam8):
        flota.append(Camion(
            id=cid, tipo=TIPO_CAMION, n_palets=PALETS_CAMION_GRANDE,
            capacidad_volumen=PALETS_CAMION_GRANDE * CAJAS_POR_PALET,
            hora_inicio=JORNADA_INI_MIN,
        ))
        cid += 1
    return flota


# =============================================================================
# Construcción del DatosProblema
# =============================================================================

def construir_datos_problema(
    juego_path: Path,
    cliente_index_path: Path,
    dist_matrix_path: Path,
    time_matrix_path: Path,
    horarios_path: Path,
    dia_num: int,
    deposito_lat: float,
    deposito_lon: float,
    flota: tuple[int, int, int],
) -> DatosProblema:
    print(f"[1/5] Leyendo cliente_index: {cliente_index_path}")
    idx_by_dest = cargar_cliente_index(cliente_index_path)
    print(f"      {len(idx_by_dest)} clientes catalogados")

    print(f"[2/5] Agregando pedidos: {juego_path}")
    clientes_raw, _ = agregar_pedidos(juego_path)
    print(f"      {len(clientes_raw)} Destino_ID distintos en el juego")

    # Filtrar clientes presentes en el index. Warning para los descartados.
    descartados: list[str] = []
    seleccionados: list[tuple[str, dict]] = []
    for dest, info in clientes_raw.items():
        if dest not in idx_by_dest:
            descartados.append(dest)
        else:
            seleccionados.append((dest, info))

    if descartados:
        print(f"      ⚠ Descartados {len(descartados)} clientes ausentes del cliente_index:")
        for d in descartados[:10]:
            print(f"         - {d}")
        if len(descartados) > 10:
            print(f"         ... ({len(descartados) - 10} más)")

    print(f"      {len(seleccionados)} clientes seleccionados")

    print(f"[3/5] Leyendo Horarios Entrega: {horarios_path}")
    horarios = cargar_horarios(horarios_path)
    print(f"      {len(horarios)} entradas (Deudor, día)")

    # Construir Cliente y Parada (1 cliente = 1 parada en este modelo simple).
    clientes: list[Cliente] = []
    paradas: list[Parada] = []
    indices_globales: list[int] = []  # idx en el cliente_index, en orden
    sin_horario = 0

    for cid, (dest, info) in enumerate(seleccionados):
        meta = idx_by_dest[dest]
        hor = horarios.get((dest, dia_num))
        if hor is None:
            sin_horario += 1
        ini, fin = ventana_horaria(hor)

        clientes.append(Cliente(
            id=cid,
            destino_id_externo=dest,
            nombre=info["nombre"],
            hora_ini=ini,
            hora_fin=fin,
            volumen_recoger=info["volumen_recoger"],
            volumen_devolver=info["volumen_devolver"],
            paradas_cercanas=[cid],   # 1 cliente -> 1 parada propia
        ))
        paradas.append(Parada(
            id=cid,
            pos=Coord(x=meta["lon"], y=meta["lat"]),
            clientes_servidos=[cid],
        ))
        indices_globales.append(meta["idx"])

    if sin_horario:
        print(f"      ⚠ {sin_horario} clientes sin horario para día {dia_num} → ventana 08:00–18:00")

    # Submatrices distancia / tiempo a partir de las matrices completas.
    print(f"[4/5] Cargando matrices (puede tardar): {dist_matrix_path.name}, {time_matrix_path.name}")
    dist_full = cargar_matriz(dist_matrix_path)
    time_full = cargar_matriz(time_matrix_path)
    M = len(clientes)
    matriz_distancia: list[float] = [0.0] * (M * M)
    matriz_tiempo: list[float] = [0.0] * (M * M)
    for i, gi in enumerate(indices_globales):
        for j, gj in enumerate(indices_globales):
            if i == j:
                continue
            matriz_distancia[i * M + j] = dist_full[gi][gj]
            # time_matrix viene en segundos → minutos
            matriz_tiempo[i * M + j] = time_full[gi][gj] / 60.0

    # Aristas depósito ↔ parada (Haversine + velocidad constante).
    print(f"[5/5] Calculando aristas depósito ({deposito_lat}, {deposito_lon}) → paradas")
    dist_deposito = [0.0] * M
    tiempo_deposito = [0.0] * M
    for i, p in enumerate(paradas):
        d_m = haversine_m(deposito_lat, deposito_lon, p.pos.y, p.pos.x)
        dist_deposito[i] = d_m
        # m → min (vel km/h): minutos = (d_m / 1000) / vel_kmh * 60
        tiempo_deposito[i] = (d_m / 1000.0) / VELOCIDAD_KMH_DEPOSITO * 60.0

    camiones = construir_flota(*flota)

    # Resumen capacidad vs demanda
    cap_total = sum(c.capacidad_volumen for c in camiones)
    demanda = sum(c.volumen_recoger for c in clientes)
    devolver = sum(c.volumen_devolver for c in clientes)
    print()
    print(f"=== Resumen del problema ===")
    print(f"  Clientes:   {len(clientes)}")
    print(f"  Paradas:    {len(paradas)}")
    print(f"  Camiones:   {len(camiones)} ({flota[0]} furgo + {flota[1]} cam6 + {flota[2]} cam8)")
    print(f"  Capacidad:  {cap_total:.0f} cajas-equivalentes")
    print(f"  Demanda:    {demanda:.0f} cajas a entregar")
    print(f"  Retorno:    {devolver:.0f} cajas-equivalentes a recoger")
    if cap_total < demanda:
        print(f"  ⚠ Capacidad < demanda. Aumenta la flota o algún cliente quedará fuera.")
    print()

    return DatosProblema(
        deposito=Coord(x=deposito_lon, y=deposito_lat),
        clientes=clientes,
        paradas=paradas,
        camiones=camiones,
        matriz_distancia=matriz_distancia,
        matriz_tiempo=matriz_tiempo,
        dist_deposito=dist_deposito,
        tiempo_deposito=tiempo_deposito,
    )


# =============================================================================
# CLI
# =============================================================================

def parse_flota(s: str) -> tuple[int, int, int]:
    parts = s.split(",")
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("flota debe ser 'n_furgo,n_cam6,n_cam8'")
    try:
        return int(parts[0]), int(parts[1]), int(parts[2])
    except ValueError:
        raise argparse.ArgumentTypeError("flota debe ser tres enteros")


def detectar_dia_num(juego_path: Path, override: int | None) -> int:
    if override is not None:
        return override
    nombre = juego_path.stem.lower()  # 'juego_lunes' -> 'juego_lunes'
    for k, v in DIA_NOMBRE_A_NUM.items():
        if k.lower() in nombre:
            return v
    raise SystemExit(f"No se pudo inferir el día desde {juego_path.name}; usa --dia-semana")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--juego", type=Path, required=True,
                        help="Ruta al juego_<dia>.csv")
    parser.add_argument("--cliente-index", type=Path,
                        default=ROOT / "mapa" / "out" / "cliente_index.csv")
    parser.add_argument("--dist-matrix", type=Path,
                        default=ROOT / "mapa" / "out" / "dist_matrix.csv")
    parser.add_argument("--time-matrix", type=Path,
                        default=ROOT / "mapa" / "out" / "time_matrix.csv")
    parser.add_argument("--horarios", type=Path,
                        default=ROOT / "test" / "Horarios Entrega.csv")
    parser.add_argument("--dia-semana", type=int, default=None,
                        help="1=Lunes ... 5=Viernes. Si no se da, se infiere del nombre del fichero.")
    parser.add_argument("--deposito-lat", type=float, default=DEPOSITO_DEFAULT_LAT)
    parser.add_argument("--deposito-lon", type=float, default=DEPOSITO_DEFAULT_LON)
    parser.add_argument("--flota", type=parse_flota, default=FLOTA_DEFAULT,
                        help="'n_furgonetas,n_cam6,n_cam8' (default: 2,3,1)")
    parser.add_argument("--output", type=Path, required=True,
                        help="Ruta del JSON DatosProblema a generar")
    args = parser.parse_args()

    dia_num = detectar_dia_num(args.juego, args.dia_semana)
    print(f"Día: {dia_num} (1=Lun ... 5=Vie)")

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

    to_json(datos, args.output)
    print(f"Escrito {args.output} ({args.output.stat().st_size / 1024:.1f} KB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
