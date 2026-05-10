import { useMemo } from "react";

import type { ResultadoFinal } from "../types";
import { colorCamion } from "../utils/colors";
import { minutosToHHMM } from "../utils/time";

interface Props {
  resultado: ResultadoFinal;
  camionSel: number | null;
}

const HORA_INI = 6 * 60;    // 06:00
const HORA_FIN = 22 * 60;   // 22:00
const RANGO = HORA_FIN - HORA_INI;

// Convierte "minutos desde 00:00" a porcentaje del eje horizontal del Gantt.
function minToPct(m: number): number {
  return Math.max(0, Math.min(100, ((m - HORA_INI) / RANGO) * 100));
}

export default function GanttHorarios({ resultado, camionSel }: Props) {
  const { datos, rutas } = resultado;

  // Para cada visita, derivamos un segmento [llegada, llegada+stop] con stop ~5min.
  // Si hora_llegada_estimada=0 (no calculada), asumimos un reparto uniforme entre
  // hora_inicio del camión y hora_fin más tarde.
  const filas = useMemo(() => rutas.map((r) => {
    const camion = datos.camiones[r.camion_id];
    const tIni = camion?.hora_inicio ?? 8 * 60;
    const segs: { ini: number; fin: number; cliente: number; retraso: number }[] = [];
    let cursor = tIni;
    for (const v of r.visitas) {
      const ini = v.hora_llegada_estimada > 0 ? v.hora_llegada_estimada : cursor;
      const fin = ini + 5;   // stop estimado de 5min
      for (const cid of v.clientes_atendidos) {
        segs.push({ ini, fin, cliente: cid, retraso: v.retraso_min });
      }
      cursor = fin + 10;     // 10min entre paradas si no hay timing real
    }
    return { camion_id: r.camion_id, segs };
  }), [rutas, datos]);

  return (
    <>
      <div className="section-title" style={{ marginTop: 16 }}>
        Horarios (Gantt)
      </div>
      <div className="gantt">
        {filas.map(({ camion_id, segs }) => {
          if (camionSel !== null && camionSel !== camion_id) return null;
          return (
            <div key={camion_id} className="gantt__row">
              <span className="gantt__label">Cam {camion_id}</span>
              <div className="gantt__track">
                {segs.map((s, i) => {
                  const left = minToPct(s.ini);
                  const width = Math.max(0.5, minToPct(s.fin) - left);
                  return (
                    <div
                      key={i}
                      className="gantt__seg"
                      title={`${minutosToHHMM(s.ini)} cliente ${s.cliente}`}
                      style={{
                        left: `${left}%`,
                        width: `${width}%`,
                        background: s.retraso > 0 ? "var(--warn)" : colorCamion(camion_id),
                      }}
                    />
                  );
                })}
              </div>
            </div>
          );
        })}
        <div className="gantt__axis">
          <span></span>
          <div className="gantt__axis-ticks">
            {[6, 9, 12, 15, 18, 21].map((h) => (
              <span key={h}>{`${h}:00`}</span>
            ))}
          </div>
        </div>
      </div>
    </>
  );
}
