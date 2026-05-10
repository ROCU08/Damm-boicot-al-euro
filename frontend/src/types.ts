// Tipos TypeScript que reflejan pipeline/contratos.py:ResultadoFinal.
// Las unidades se mantienen consistentes con el backend:
//   - Volumen: cajas-equivalentes (1 palet = 60 cajas)
//   - Distancia: metros
//   - Tiempo: minutos (desde 00:00 para horarios; duración para travel)

export interface Coord {
  x: number;   // longitud
  y: number;   // latitud
}

export interface Cliente {
  id: number;
  destino_id_externo: string;
  nombre: string;
  hora_ini: number;            // minutos desde 00:00
  hora_fin: number;
  volumen_recoger: number;     // cajas-equivalentes a entregar
  volumen_devolver: number;    // cajas-equivalentes a recoger (envases)
  paradas_cercanas: number[];
}

export interface Parada {
  id: number;
  pos: Coord;
  clientes_servidos: number[];
}

export type TipoVehiculo = "FURGONETA" | "CAMION";

export interface Camion {
  id: number;
  tipo: TipoVehiculo;
  n_palets: number;            // 1..8
  capacidad_volumen: number;   // cajas-equivalentes
  hora_inicio: number;
}

export interface DatosProblema {
  deposito: Coord;
  clientes: Cliente[];
  paradas: Parada[];
  camiones: Camion[];
  matriz_distancia: number[];
  matriz_tiempo: number[];
  dist_deposito: number[];
  tiempo_deposito: number[];
}

// -----------------------------------------------------------------------------
// Solución del SA-ruta. Hay dos shapes posibles según el productor:
//   - damm "raw": rutas[].paradas[]   (idx de paradas) + clientes_atendidos[][]
//   - orquestador: rutas[].visitas[]  (parada + clientes + horario calculado)
//
// El frontend acepta ambos y los normaliza a `RutaSolucion`.
// -----------------------------------------------------------------------------

export interface VisitaParada {
  parada_id: number;
  clientes_atendidos: number[];
  hora_llegada_estimada: number;
  retraso_min: number;
}

export interface RutaSolucion {
  camion_id: number;
  visitas: VisitaParada[];
  total_distancia: number;
  total_carga_inicial: number;
  total_pico_volumen: number;
  total_retraso: number;
  // Polilínea real OSRM: [[lat, lon], ...]. Si null/ausente el frontend
  // dibuja una línea recta entre paradas como fallback.
  polilinea_geo?: [number, number][] | null;
}

// Shape "raw" emitido por damm/exportar.cc
export interface RutaRaw {
  camion_id: number;
  paradas: number[];
  clientes_atendidos: number[][];
  total_distancia: number;
  total_carga_inicial: number;
  total_pico_volumen: number;
  total_retraso: number;
}

// -----------------------------------------------------------------------------
// Output del SA-distribución
// -----------------------------------------------------------------------------

export interface SlotOcupado {
  palet: number;
  piso: number;
  fila: number;
  col: number;
  material_id: number;
  material_codigo: string;
  cliente_id: number;
  es_barril: boolean;
}

export interface ComponentesFitness {
  fragmentacion_producto: number;
  fragmentacion_cliente: number;
  accesibilidad: number;
}

export interface DistribucionOutput {
  camion_id: number;
  n_palets: number;
  fitness: number;
  componentes: ComponentesFitness;
  layout: SlotOcupado[];
}

// -----------------------------------------------------------------------------
// KPIs y resultado final
// -----------------------------------------------------------------------------

export interface KPIsGlobales {
  coste_total: number;
  distancia_total_m: number;
  tiempo_total_min: number;
  clientes_servidos: number;
  clientes_no_servidos: number[];
  utilizacion_por_camion: number[];
}

export interface ResultadoFinal {
  turno?: number;              // 1 = mañana, 2 = tarde. Opcional por compat.
  datos: DatosProblema;
  rutas: RutaSolucion[];
  distribuciones: DistribucionOutput[];
  kpis: KPIsGlobales;
  no_servidos: number[];
}

// -----------------------------------------------------------------------------
// Shape "raw" sólo de SA-ruta (sin distribuciones), tal como lo exporta damm
// directamente. Útil para visualizar antes de tener el orquestador.
// -----------------------------------------------------------------------------

export interface RutaSoloOutput {
  coste_total: number;
  datos: DatosProblema;
  solucion: {
    rutas: RutaRaw[];
    no_servidos: number[];
  };
}
