import { useMemo, useState } from "react";

import type { ResultadoFinal } from "./types";
import { normalizar } from "./utils/normalizar";

import Cargador from "./components/Cargador";
import SidebarCamiones from "./components/SidebarCamiones";
import SidebarDetalle from "./components/SidebarDetalle";
import MapaRutas from "./components/MapaRutas";
import Camion3D from "./components/Camion3D";
import GanttHorarios from "./components/GanttHorarios";

type Vista = "mapa" | "3d";

export default function App() {
  const [resultado, setResultado] = useState<ResultadoFinal | null>(null);
  const [errorCarga, setErrorCarga] = useState<string | null>(null);
  const [camionSel, setCamionSel] = useState<number | null>(null);
  const [paradaSel, setParadaSel] = useState<number | null>(null);
  const [vista, setVista] = useState<Vista>("mapa");

  const distribucionSel = useMemo(() => {
    if (!resultado || camionSel === null) return null;
    return resultado.distribuciones.find((d) => d.camion_id === camionSel) ?? null;
  }, [resultado, camionSel]);

  function handleArchivo(j: unknown) {
    const r = normalizar(j);
    if (!r) {
      setErrorCarga("El JSON no tiene un shape reconocible (ResultadoFinal ni RutaSoloOutput).");
      return;
    }
    setErrorCarga(null);
    setResultado(r);
    setCamionSel(r.rutas[0]?.camion_id ?? null);
    setParadaSel(null);
  }

  if (!resultado) {
    return <Cargador onArchivo={handleArchivo} error={errorCarga} />;
  }

  return (
    <div className="app">
      <header className="app__topbar">
        <h1>
          <span>DAMM</span> · Smart Truck — visualización
        </h1>
        <button
          onClick={() => { setResultado(null); setErrorCarga(null); }}
          style={{ background: "transparent", color: "var(--text-muted)", border: "1px solid var(--border)", padding: "4px 10px", borderRadius: 4, cursor: "pointer" }}
        >
          Cargar otro JSON
        </button>
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
