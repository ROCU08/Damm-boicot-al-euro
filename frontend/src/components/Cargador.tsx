import { useRef } from "react";

export interface ArchivoCargado {
  name: string;
  json: unknown;
}

interface Props {
  onArchivos: (archivos: ArchivoCargado[]) => void;
  error: string | null;
}

export default function Cargador({ onArchivos, error }: Props) {
  const inputRef = useRef<HTMLInputElement>(null);

  function handleChange(e: React.ChangeEvent<HTMLInputElement>) {
    const files = Array.from(e.target.files ?? []);
    if (files.length === 0) return;

    const lecturas = files.map(
      (file) =>
        new Promise<ArchivoCargado>((resolve, reject) => {
          const reader = new FileReader();
          reader.onload = () => {
            try {
              resolve({ name: file.name, json: JSON.parse(reader.result as string) });
            } catch (err) {
              reject(err);
            }
          };
          reader.onerror = () => reject(reader.error);
          reader.readAsText(file);
        }),
    );

    Promise.all(lecturas)
      .then(onArchivos)
      .catch((err) => {
        console.error(err);
        onArchivos([{ name: "__error", json: { __error: String(err) } }]);
      });
  }

  return (
    <div className="cargador">
      <h2><span style={{ color: "var(--accent)" }}>Damm-Livering</span></h2>
      <p>
        Arrastra o selecciona los JSON producidos por el orquestador
        (<code>resultado_turno1.json</code> y <code>resultado_turno2.json</code>),
        o directamente la salida de <code>damm</code> (<code>salida.json</code>).
      </p>
      <p style={{ fontSize: "0.85em", color: "var(--text-muted)" }}>
        Puedes seleccionar los 2 turnos a la vez (Ctrl/Shift + click) para verlos
        con un selector mañana/tarde.
      </p>
      <label>
        Seleccionar JSON(s)
        <input
          ref={inputRef}
          type="file"
          accept=".json,application/json"
          multiple
          onChange={handleChange}
        />
      </label>
      <small>
        Acepta <code>ResultadoFinal</code> (rutas + distribuciones + KPIs) o
        <code> RutaSoloOutput</code> (solo SA-ruta, vista 3D no disponible).
      </small>
      {error && (
        <div style={{ color: "var(--danger)", marginTop: 12 }}>
          ⚠ {error}
        </div>
      )}
    </div>
  );
}
