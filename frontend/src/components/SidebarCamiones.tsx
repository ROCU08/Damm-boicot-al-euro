import type { ResultadoFinal } from "../types";
import { colorCamion } from "../utils/colors";
import { formatDistancia } from "../utils/time";

interface Props {
  resultado: ResultadoFinal;
  camionSel: number | null;
  onSelect: (camionId: number) => void;
}

export default function SidebarCamiones({ resultado, camionSel, onSelect }: Props) {
  const { datos, rutas } = resultado;

  return (
    <div>
      <div className="section-title">Camiones ({rutas.length})</div>
      {rutas.map((ruta) => {
        const camion = datos.camiones[ruta.camion_id];
        const cap = camion?.capacidad_volumen ?? 1;
        const pico = ruta.total_pico_volumen;
        const utilPct = (pico / Math.max(cap, 1)) * 100;
        const overflow = pico > cap;
        const nVisitas = ruta.visitas.length;
        const nClientes = ruta.visitas.reduce((s, v) => s + v.clientes_atendidos.length, 0);
        const isActive = camionSel === ruta.camion_id;

        return (
          <div
            key={ruta.camion_id}
            className={`camion-card ${isActive ? "is-active" : ""}`}
            style={{ borderLeftColor: colorCamion(ruta.camion_id) }}
            onClick={() => onSelect(ruta.camion_id)}
          >
            <div className="camion-card__head">
              <span className="camion-card__title">
                {camion?.tipo === "FURGONETA" ? "Furgo" : "Camión"} {ruta.camion_id}
              </span>
              <span className="camion-card__sub">
                {camion?.n_palets ?? "?"} palets
              </span>
            </div>
            <div className="camion-card__sub" style={{ marginTop: 4 }}>
              {nVisitas} paradas · {nClientes} clientes · {formatDistancia(ruta.total_distancia)}
            </div>
            <div className="camion-card__bar">
              <div
                className={`camion-card__bar-fill ${overflow ? "is-overflow" : ""}`}
                style={{ width: `${Math.min(utilPct, 100)}%` }}
              />
            </div>
            <div className="camion-card__sub" style={{ marginTop: 4, fontSize: 11 }}>
              {pico.toFixed(0)} / {cap.toFixed(0)} cajas ({utilPct.toFixed(0)}%)
              {overflow && <span style={{ color: "var(--danger)", marginLeft: 6 }}>⚠ overflow</span>}
              {ruta.total_retraso > 0 && (
                <span style={{ color: "var(--warn)", marginLeft: 6 }}>
                  ⏰ {ruta.total_retraso.toFixed(0)}min retraso
                </span>
              )}
            </div>
          </div>
        );
      })}

      {resultado.no_servidos.length > 0 && (
        <>
          <div className="section-title">Sin servir ({resultado.no_servidos.length})</div>
          <div style={{ fontSize: 12, color: "var(--text-muted)" }}>
            {resultado.no_servidos.slice(0, 12).map((id) => {
              const c = datos.clientes[id];
              return (
                <div key={id} style={{ marginBottom: 2 }}>
                  · {c?.nombre || `cliente ${id}`}
                </div>
              );
            })}
            {resultado.no_servidos.length > 12 && (
              <div>· … ({resultado.no_servidos.length - 12} más)</div>
            )}
          </div>
        </>
      )}
    </div>
  );
}
