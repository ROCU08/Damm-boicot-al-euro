import type { ResultadoFinal } from "../types";
import { colorCliente } from "../utils/colors";
import { esVentanaEstrecha, formatDistancia, minutosToHHMM } from "../utils/time";

interface Props {
  resultado: ResultadoFinal;
  camionSel: number | null;
  paradaSel: number | null;
}

export default function SidebarDetalle({ resultado, camionSel, paradaSel }: Props) {
  const { datos, rutas, kpis } = resultado;
  const ruta = camionSel !== null ? rutas.find((r) => r.camion_id === camionSel) : null;
  const camion = camionSel !== null ? datos.camiones[camionSel] : null;

  return (
    <div>
      <div className="section-title">KPIs globales</div>
      <div className="kpi"><span className="kpi__label">Coste total</span><span className="kpi__value">{kpis.coste_total.toFixed(0)}</span></div>
      <div className="kpi"><span className="kpi__label">Distancia total</span><span className="kpi__value">{formatDistancia(kpis.distancia_total_m)}</span></div>
      <div className="kpi"><span className="kpi__label">Clientes servidos</span><span className="kpi__value">{kpis.clientes_servidos} / {datos.clientes.length}</span></div>
      <div className="kpi"><span className="kpi__label">Sin servir</span><span className="kpi__value" style={{ color: kpis.clientes_no_servidos.length ? "var(--danger)" : "inherit" }}>{kpis.clientes_no_servidos.length}</span></div>
      <div className="kpi"><span className="kpi__label">Camiones activos</span><span className="kpi__value">{rutas.filter((r) => r.visitas.length > 0).length} / {datos.camiones.length}</span></div>

      {ruta && camion && (
        <>
          <div className="section-title">Camión {ruta.camion_id} ({camion.tipo === "FURGONETA" ? "furgo" : "camión"})</div>
          <div className="kpi"><span className="kpi__label">Palets</span><span className="kpi__value">{camion.n_palets}</span></div>
          <div className="kpi"><span className="kpi__label">Distancia ruta</span><span className="kpi__value">{formatDistancia(ruta.total_distancia)}</span></div>
          <div className="kpi"><span className="kpi__label">Carga inicial</span><span className="kpi__value">{ruta.total_carga_inicial.toFixed(0)} cajas</span></div>
          <div className="kpi"><span className="kpi__label">Pico volumen</span><span className="kpi__value">{ruta.total_pico_volumen.toFixed(0)} / {camion.capacidad_volumen.toFixed(0)} cajas</span></div>
          <div className="kpi"><span className="kpi__label">Retraso total</span><span className="kpi__value" style={{ color: ruta.total_retraso > 0 ? "var(--warn)" : "inherit" }}>{ruta.total_retraso.toFixed(0)} min</span></div>
        </>
      )}

      {ruta && paradaSel !== null && (
        <ParadaDetalle resultado={resultado} paradaId={paradaSel} />
      )}
    </div>
  );
}

function ParadaDetalle({ resultado, paradaId }: { resultado: ResultadoFinal; paradaId: number }) {
  const { datos, rutas } = resultado;
  const parada = datos.paradas[paradaId];
  if (!parada) return null;

  // ¿Qué visita corresponde a esta parada?
  let visita: { camion_id: number; orden: number; clientes: number[]; hora_llegada: number; retraso: number } | null = null;
  for (const r of rutas) {
    const idx = r.visitas.findIndex((v) => v.parada_id === paradaId);
    if (idx >= 0) {
      const v = r.visitas[idx];
      visita = {
        camion_id: r.camion_id,
        orden: idx + 1,
        clientes: v.clientes_atendidos,
        hora_llegada: v.hora_llegada_estimada,
        retraso: v.retraso_min,
      };
      break;
    }
  }

  return (
    <div className="parada-detalle" style={{ marginTop: 16 }}>
      <div className="section-title">Parada seleccionada</div>
      <h3>Parada {paradaId}</h3>
      {visita ? (
        <div className="meta">
          Camión {visita.camion_id} · visita #{visita.orden}
          {visita.hora_llegada > 0 && ` · llega ${minutosToHHMM(visita.hora_llegada)}`}
          {visita.retraso > 0 && <span style={{ color: "var(--warn)" }}> · retraso {visita.retraso.toFixed(0)}min</span>}
        </div>
      ) : (
        <div className="meta">No usada en ninguna ruta</div>
      )}
      <div style={{ marginTop: 8 }}>
        {(visita ? visita.clientes : parada.clientes_servidos).map((cid) => {
          const c = datos.clientes[cid];
          if (!c) return null;
          const estrecha = esVentanaEstrecha(c.hora_ini, c.hora_fin);
          const devuelve = c.volumen_devolver > 0;
          return (
            <div key={cid} className="cliente-row">
              <span className="cliente-row__dot" style={{ background: colorCliente(cid) }} />
              <span className="cliente-row__name" title={c.nombre}>
                {c.nombre || `Cliente ${cid}`}
              </span>
              <span className="cliente-row__chips">
                <span className="chip">{c.volumen_recoger.toFixed(0)} cj</span>
                {estrecha && (
                  <span className="chip warn" title={`${minutosToHHMM(c.hora_ini)}–${minutosToHHMM(c.hora_fin)}`}>
                    ⏰ {minutosToHHMM(c.hora_ini)}
                  </span>
                )}
                {devuelve && (
                  <span className="chip info" title={`Devuelve ${c.volumen_devolver.toFixed(0)} cajas`}>
                    ↩ {c.volumen_devolver.toFixed(0)}
                  </span>
                )}
              </span>
            </div>
          );
        })}
      </div>
    </div>
  );
}
