import { useRef } from "react";

interface Props {
  onArchivo: (j: unknown) => void;
  error: string | null;
}

export default function Cargador({ onArchivo, error }: Props) {
  const inputRef = useRef<HTMLInputElement>(null);

  function handleChange(e: React.ChangeEvent<HTMLInputElement>) {
    const file = e.target.files?.[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = () => {
      try {
        const j = JSON.parse(reader.result as string);
        onArchivo(j);
      } catch (err) {
        console.error(err);
        onArchivo({ __error: String(err) });
      }
    };
    reader.readAsText(file);
  }

  return (
    <div className="cargador">
      <h2><span style={{ color: "var(--accent)" }}>DAMM</span> · Smart Truck</h2>
      <p>
        Arrastra o selecciona el JSON producido por el orquestador
        (<code>resultado_&lt;dia&gt;.json</code>) o directamente la salida de
        <code> damm</code> (<code>salida.json</code>).
      </p>
      <label>
        Seleccionar JSON
        <input ref={inputRef} type="file" accept=".json,application/json" onChange={handleChange} />
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
