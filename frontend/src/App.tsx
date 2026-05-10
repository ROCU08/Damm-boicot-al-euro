import { useMemo, useState } from "react";

import type { ResultadoFinal } from "./types";
import { normalizar } from "./utils/normalizar";

import Cargador, { type ArchivoCargado } from "./components/Cargador";
import SidebarCamiones from "./components/SidebarCamiones";
import SidebarDetalle from "./components/SidebarDetalle";
import MapaRutas from "./components/MapaRutas";
import Camion3D from "./components/Camion3D";
import GanttHorarios from "./components/GanttHorarios";

type Vista = "mapa" | "3d";
type TurnosState = Record<number, ResultadoFinal>;

const ETIQUETA_TURNO: Record<number, string> = {
  1: "Turno mañana",
  2: "Turno tarde",
};

function detectarTurno(name: string, json: unknown): number | undefined {
  const obj = json as { turno?: unknown } | null;
  if (obj && typeof obj.turno === "number") return obj.turno;
  const m = name.match(/turno\s*(\d+)/i);
  if (m) return parseInt(m[1], 10);
  return undefined;
}

export default function App() {
  const [turnos, setTurnos] = useState<TurnosState>({});
  const [turnoActivo, setTurnoActivo] = useState<number | null>(null);
  const [errorCarga, setErrorCarga] = useState<string | null>(null);
  const [camionSel, setCamionSel] = useState<number | null>(null);
  const [paradaSel, setParadaSel] = useState<number | null>(null);
  const [vista, setVista] = useState<Vista>("mapa");

  const resultado = turnoActivo !== null ? turnos[turnoActivo] ?? null : null;

  const distribucionSel = useMemo(() => {
    if (!resultado || camionSel === null) return null;
    return resultado.distribuciones.find((d) => d.camion_id === camionSel) ?? null;
  }, [resultado, camionSel]);

  const turnosOrdenados = useMemo(
    () => Object.keys(turnos).map(Number).sort((a, b) => a - b),
    [turnos],
  );

  function aplicarTurnoActivo(state: TurnosState, turno: number) {
    const r = state[turno];
    setTurnoActivo(turno);
    setCamionSel(r.rutas[0]?.camion_id ?? null);
    setParadaSel(null);
  }

  function handleArchivos(archivos: ArchivoCargado[]) {
    const nuevos: TurnosState = { ...turnos };
    const errores: string[] = [];
    let asignacionAuto = 0; // contador para fallback cuando no detectamos turno

    for (const { name, json } of archivos) {
      if (json && typeof json === "object" && "__error" in (json as object)) {
        errores.push(`${name}: ${(json as { __error: string }).__error}`);
        continue;
      }
      const r = normalizar(json);
      if (!r) {
        errores.push(`${name}: shape no reconocible`);
        continue;
      }
      let t = detectarTurno(name, json);
      if (typeof t !== "number") {
        // Sin pista de turno (e.g. salida.json antiguo) → ocupa el siguiente hueco libre.
        asignacionAuto += 1;
        t = Object.keys(nuevos).length === 0 ? 1 : Math.max(0, ...Object.keys(nuevos).map(Number)) + 1;
      }
      nuevos[t] = r;
    }

    if (Object.keys(nuevos).length === 0) {
      setErrorCarga(errores.join("; ") || "Ningún JSON válido cargado.");
      return;
    }
    setErrorCarga(errores.length ? errores.join("; ") : null);
    setTurnos(nuevos);
    const claves = Object.keys(nuevos).map(Number).sort((a, b) => a - b);
    aplicarTurnoActivo(nuevos, claves[0]);
  }

  function reset() {
    setTurnos({});
    setTurnoActivo(null);
    setCamionSel(null);
    setParadaSel(null);
    setErrorCarga(null);
  }

  if (!resultado) {
    return <Cargador onArchivos={handleArchivos} error={errorCarga} />;
  }

  return (
    <div className="app">
      <header className="app__topbar">
        <h1>
          <span>Damm-Livering</span> — visualización
        </h1>

        {turnosOrdenados.length > 1 && (
          <div className="turno-toggle" style={{ display: "flex", gap: 4, marginLeft: 16 }}>
            {turnosOrdenados.map((t) => (
              <button
                key={t}
                onClick={() => aplicarTurnoActivo(turnos, t)}
                className={turnoActivo === t ? "is-active" : ""}
                style={{
                  background: turnoActivo === t ? "var(--accent)" : "transparent",
                  color: turnoActivo === t ? "var(--bg)" : "var(--text)",
                  border: "1px solid var(--border)",
                  padding: "4px 12px",
                  borderRadius: 4,
                  cursor: "pointer",
                  fontWeight: turnoActivo === t ? 600 : 400,
                }}
              >
                {ETIQUETA_TURNO[t] ?? `Turno ${t}`}
              </button>
            ))}
          </div>
        )}

        {turnosOrdenados.length === 1 && turnoActivo !== null && (
          <span style={{ marginLeft: 16, color: "var(--text-muted)", fontSize: "0.9em" }}>
            {ETIQUETA_TURNO[turnoActivo] ?? `Turno ${turnoActivo}`}
          </span>
        )}

        <div style={{ marginLeft: "auto" }}>
          <button
            onClick={reset}
            style={{ background: "transparent", color: "var(--text-muted)", border: "1px solid var(--border)", padding: "4px 10px", borderRadius: 4, cursor: "pointer" }}
          >
            Cargar otro JSON
          </button>
        </div>
      </header>

      <aside className="app__sidebar-l">
        <SidebarCamiones
          resultado={resultado}
          camionSel={camionSel}
          onSelect={(id) => { setCamionSel(id); setParadaSel(null); }}
        />
      </aside>

      <main className="app__main">
        <div className="view-toggle">
          <button
            className={vista === "mapa" ? "is-active" : ""}
            onClick={() => setVista("mapa")}
          >
            Mapa
          </button>
          <button
            className={vista === "3d" ? "is-active" : ""}
            onClick={() => setVista("3d")}
            disabled={!distribucionSel}
            title={distribucionSel ? "" : "Este JSON no incluye distribución 3D"}
          >
            Distribución 3D
          </button>
        </div>

        {vista === "mapa" && (
          <MapaRutas
            resultado={resultado}
            camionSel={camionSel}
            paradaSel={paradaSel}
            onParadaClick={setParadaSel}
          />
        )}

        {vista === "3d" && distribucionSel && (
          <Camion3D
            distribucion={distribucionSel}
            ordenRuta={resultado.rutas.find((r) => r.camion_id === camionSel)?.visitas
              .flatMap((v) => v.clientes_atendidos) ?? []}
          />
        )}
      </main>

      <aside className="app__sidebar-r">
        <SidebarDetalle
          resultado={resultado}
          camionSel={camionSel}
          paradaSel={paradaSel}
        />
        <GanttHorarios resultado={resultado} camionSel={camionSel} />
      </aside>
    </div>
  );
}
