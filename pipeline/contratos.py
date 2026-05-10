"""Esquemas JSON intercambiados entre los componentes del pipeline.

Capas:
  Datos crudos (CSVs)            ──┐
                                   ▼
  cargar_csv.py  ───────────►  DatosProblema  (datos_<dia>.json)
                                   │
                                   ▼
  damm --from-json datos.json  ──► RutaSolucion (ruta_<dia>.json)
                                   │
              ┌────────────────────┼─────────────────┐
              ▼                    ▼                 ▼
  RutaInputDist (camion 0)   RutaInputDist (1)  RutaInputDist (N)
              │                    │                 │
              ▼                    ▼                 ▼
        damm-dist             damm-dist          damm-dist        (en paralelo)
              │                    │                 │
              └────────────┬───────┴────┬────────────┘
                           ▼            ▼
                  DistribucionOutput[]  (N JSONs)
                           │
                           ▼
              orquestador → ResultadoFinal (resultado_<dia>.json)
                           │
                           ▼
                       Frontend

Convenciones de unidades (cajas-equivalentes):
  - 1 caja CAJ            -> 1 unidad
  - N unidades sueltas UN -> ceil(N/12) cajas
  - 1 barril BRL          -> 4 cajas (es_barril=true)
  - 1 envase retornable   -> 1 unidad de "volumen_devolver"
  - capacidad camión      -> n_palets * 60 cajas (60 = PISOS*FILAS*COLS)

Distancias en metros, tiempos en minutos (desde medianoche para horarios,
duración para travel times).
"""

from __future__ import annotations

import json
from dataclasses import dataclass, asdict, field
from pathlib import Path
from typing import Any


# =============================================================================
# Constantes del modelo (deben coincidir con codi/algo/distribucio/estado_distr.cc)
# =============================================================================

PISOS_POR_PALET = 6
FILAS_POR_PISO = 5
COLS_POR_FILA = 2
CAJAS_POR_PALET = PISOS_POR_PALET * FILAS_POR_PISO * COLS_POR_FILA  # 60

# Tipos de vehículo (deben coincidir con TipoVehiculo en C++)
TIPO_FURGONETA = "FURGONETA"   # 1-3 palets
TIPO_CAMION = "CAMION"         # 4-8 palets


# =============================================================================
# DatosProblema — entrada del SA-ruta
# =============================================================================

@dataclass
class Coord:
    x: float  # longitud (lon) en grados decimales
    y: float  # latitud (lat) en grados decimales


@dataclass
class Cliente:
    id: int                          # enumerado interno (0..N-1)
    destino_id_externo: str          # Destino_ID original del CSV (informativo)
    nombre: str                      # informativo
    hora_ini: int                    # minutos desde 00:00
    hora_fin: int                    # minutos desde 00:00
    volumen_recoger: float           # cajas-equivalentes
    volumen_devolver: float          # cajas-equivalentes (envases retornables)
    paradas_cercanas: list[int]      # ids de paradas que pueden servir a este cliente


@dataclass
class Parada:
    id: int                          # enumerado interno (0..M-1)
    pos: Coord
    clientes_servidos: list[int]


@dataclass
class Camion:
    id: int
    tipo: str                        # TIPO_FURGONETA | TIPO_CAMION
    n_palets: int                    # 1..8 (parámetro físico)
    capacidad_volumen: float         # = n_palets * 60 cajas
    hora_inicio: int                 # minutos desde 00:00


@dataclass
class DatosProblema:
    deposito: Coord
    clientes: list[Cliente]
    paradas: list[Parada]
    camiones: list[Camion]
    matriz_distancia: list[float]    # M*M, row-major (metros)
    matriz_tiempo: list[float]       # M*M, row-major (minutos)
    dist_deposito: list[float]       # M (metros)
    tiempo_deposito: list[float]     # M (minutos)


# =============================================================================
# Items para SA-distribución (1 por camión)
# =============================================================================

@dataclass
class ItemDist:
    """Un slot del camión 3D. Los barriles del CSV se expanden a 4 ítems
    idénticos (es_barril=true) para que ocupen 4 slots; la fragmentación
    del fitness los junta naturalmente.
    """
    material_id: int                 # uint16 enumerado
    material_codigo: str             # informativo (ej. "ED13", "VO30")
    cliente_id: int                  # mismo enumerado que en DatosProblema.clientes
    es_barril: bool


@dataclass
class SaParams:
    temp_ini: float = 1000.0
    temp_fin: float = 0.01
    cooling: float = 0.95
    iters_temp: int = 100


@dataclass
class RutaInputDist:
    """Lo que el orquestador escribe a un fichero por camión, y damm-dist lee."""
    camion_id: int
    n_palets: int
    orden_ruta: list[int]            # cliente_ids en orden de visita
    items: list[ItemDist]
    sa_params: SaParams = field(default_factory=SaParams)


# =============================================================================
# Output del SA-distribución (1 por camión)
# =============================================================================

@dataclass
class SlotOcupado:
    palet: int
    piso: int
    fila: int
    col: int
    material_id: int
    material_codigo: str
    cliente_id: int
    es_barril: bool


@dataclass
class ComponentesFitness:
    fragmentacion_producto: float
    fragmentacion_cliente: float
    accesibilidad: float


@dataclass
class DistribucionOutput:
    camion_id: int
    n_palets: int
    fitness: float
    componentes: ComponentesFitness
    layout: list[SlotOcupado]


# =============================================================================
# Resultado final — lo que consume el frontend
# =============================================================================

@dataclass
class VisitaParada:
    """Una visita en una ruta: parada + clientes atendidos en ella."""
    parada_id: int
    clientes_atendidos: list[int]
    hora_llegada_estimada: int       # minutos desde 00:00
    retraso_min: float               # 0 si dentro de ventana


@dataclass
class RutaSolucion:
    camion_id: int
    visitas: list[VisitaParada]
    total_distancia: float           # metros
    total_carga_inicial: float       # cajas-equivalentes (volumen al salir del depósito)
    total_pico_volumen: float        # pico de carga durante la ruta
    total_retraso: float             # minutos


@dataclass
class KPIsGlobales:
    coste_total: float
    distancia_total_m: float
    tiempo_total_min: float
    clientes_servidos: int
    clientes_no_servidos: list[int]
    utilizacion_por_camion: list[float]  # pico/capacidad por camión


@dataclass
class ResultadoFinal:
    datos: DatosProblema
    rutas: list[RutaSolucion]
    distribuciones: list[DistribucionOutput]
    kpis: KPIsGlobales
    no_servidos: list[int]           # clientes que no quedaron en ninguna ruta


# =============================================================================
# Serialización
# =============================================================================

def to_json(obj: Any, path: Path | str, *, indent: int = 2) -> None:
    """Vuelca cualquier dataclass de este módulo a JSON."""
    if hasattr(obj, "__dataclass_fields__"):
        data = asdict(obj)
    else:
        data = obj
    Path(path).write_text(json.dumps(data, indent=indent, ensure_ascii=False), encoding="utf-8")


def from_json_file(path: Path | str) -> dict:
    """Lee un JSON como dict; no rehidrata a dataclasses (innecesario para el orquestador)."""
    return json.loads(Path(path).read_text(encoding="utf-8"))
