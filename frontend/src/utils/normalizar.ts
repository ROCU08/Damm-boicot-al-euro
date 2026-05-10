// Normaliza la entrada del frontend a un único shape `ResultadoFinal`,
// aceptando varias formas de input:
//   - ResultadoFinal completo (orquestador): se usa tal cual.
//   - RutaSoloOutput (output de damm sin distribuciones): se completan
//     campos calculables (visitas con orden, KPIs, distribuciones vacías).
//   - Mezcla (RutaSoloOutput + lista de DistribucionOutput por camión):
//     no soportado todavía; al añadir el orquestador será irrelevante.

import type {
  DatosProblema,
  DistribucionOutput,
  KPIsGlobales,
  ResultadoFinal,
  RutaRaw,
  RutaSolucion,
  RutaSoloOutput,
  VisitaParada,
} from "../types";

function tieneShapeOrquestado(j: unknown): j is ResultadoFinal {
  if (!j || typeof j !== "object") return false;
  const obj = j as Record<string, unknown>;
  return Array.isArray(obj.rutas) && Array.isArray(obj.distribuciones) && obj.kpis !== undefined;
}

function tieneShapeRaw(j: unknown): j is RutaSoloOutput {
  if (!j || typeof j !== "object") return false;
  const obj = j as Record<string, unknown>;
  return obj.solucion !== undefined && obj.datos !== undefined;
}

function visitasDesdeRaw(raw: RutaRaw): VisitaParada[] {
  return raw.paradas.map((parada_id, i) => ({
    parada_id,
    clientes_atendidos: raw.clientes_atendidos[i] ?? [],
    hora_llegada_estimada: 0, // no calculado en el shape raw
    retraso_min: 0,
  }));
}

function kpisDesdeRutas(rutas: RutaSolucion[], camiones: { capacidad_volumen: number }[]): KPIsGlobales {
  const dist = rutas.reduce((s, r) => s + r.total_distancia, 0);
  const tiempo = 0; // sin matriz_tiempo aplicada al orden, no tenemos total
  const utilizacion: number[] = rutas.map((r, i) => {
    const cap = camiones[i]?.capacidad_volumen ?? 1;
    return cap > 0 ? r.total_pico_volumen / cap : 0;
  });
  return {
    coste_total: 0,
    distancia_total_m: dist,
    tiempo_total_min: tiempo,
    clientes_servidos: 0,
    clientes_no_servidos: [],
    utilizacion_por_camion: utilizacion,
  };
}

export function normalizar(input: unknown): ResultadoFinal | null {
  if (tieneShapeOrquestado(input)) {
    return input;
  }

  if (tieneShapeRaw(input)) {
    const datos = input.datos as DatosProblema;
    const rutas: RutaSolucion[] = input.solucion.rutas.map((r) => ({
      camion_id: r.camion_id,
      visitas: visitasDesdeRaw(r),
      total_distancia: r.total_distancia,
      total_carga_inicial: r.total_carga_inicial,
      total_pico_volumen: r.total_pico_volumen,
      total_retraso: r.total_retraso,
    }));
    const distribuciones: DistribucionOutput[] = [];

    const totalClientes = datos.clientes?.length ?? 0;
    const noServidos = input.solucion.no_servidos ?? [];
    const kpis = kpisDesdeRutas(rutas, datos.camiones);
    kpis.coste_total = input.coste_total ?? 0;
    kpis.clientes_servidos = totalClientes - noServidos.length;
    kpis.clientes_no_servidos = noServidos;

    return { datos, rutas, distribuciones, kpis, no_servidos: noServidos };
  }

  return null;
}
