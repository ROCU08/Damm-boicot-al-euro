import { useMemo, useState } from "react";
import { Canvas } from "@react-three/fiber";
import { OrbitControls, Text } from "@react-three/drei";

import type { DistribucionOutput, SlotOcupado } from "../types";
import { colorCliente } from "../utils/colors";

interface Props {
  distribucion: DistribucionOutput;
  ordenRuta: number[];   // cliente_ids en orden de visita
}

// Dimensiones de un palet en unidades del mundo 3D.
// PISOS=6 (Y), FILAS=5 (Z), COLS=2 (X). Cada slot 1×1×1.
const PISOS = 6;
const FILAS = 5;
const COLS = 2;
const SLOT_SIZE = 1;
const PALET_GAP = 0.6;     // espacio entre palets en X
const PALET_BASE_HEIGHT = 0.15;

// Posición 3D de un slot relativa al origen del camión.
// `pisoEf` es el piso EFECTIVO tras aplicar gravedad (slots que han caído
// para llenar huecos de cajas ya entregadas). En step=0 coincide con s.piso.
function posSlot(s: SlotOcupado, pisoEf: number): [number, number, number] {
  const x = s.palet * (COLS + PALET_GAP) + s.col + 0.5;
  const y = PALET_BASE_HEIGHT + pisoEf + 0.5;
  const z = s.fila + 0.5;
  return [x, y, z];
}

interface SlotProps {
  slot: SlotOcupado;
  pisoEf: number;
}

function Slot({ slot, pisoEf }: SlotProps) {
  const color = colorCliente(slot.cliente_id);
  return (
    <mesh position={posSlot(slot, pisoEf)} castShadow receiveShadow>
      <boxGeometry args={[SLOT_SIZE * 0.92, SLOT_SIZE * 0.92, SLOT_SIZE * 0.92]} />
      <meshStandardMaterial
        color={color}
        roughness={slot.es_barril ? 0.3 : 0.7}
        metalness={slot.es_barril ? 0.6 : 0.05}
      />
    </mesh>
  );
}

interface PaletBaseProps {
  paletIdx: number;
  resaltado: boolean;
}

function PaletBase({ paletIdx, resaltado }: PaletBaseProps) {
  const x = paletIdx * (COLS + PALET_GAP) + COLS / 2;
  return (
    <group position={[x, PALET_BASE_HEIGHT / 2, FILAS / 2]}>
      <mesh receiveShadow>
        <boxGeometry args={[COLS, PALET_BASE_HEIGHT, FILAS]} />
        <meshStandardMaterial
          color={resaltado ? "#3a3f4b" : "#23272f"}
          roughness={0.95}
        />
      </mesh>
      <Text
        position={[0, PALET_BASE_HEIGHT * 1.5 + 0.05, FILAS / 2 + 0.15]}
        fontSize={0.4}
        color="#888"
        anchorX="center"
        anchorY="bottom"
      >
        Palet {paletIdx}
      </Text>
    </group>
  );
}

export default function Camion3D({ distribucion, ordenRuta }: Props) {
  // Step en el slider de "tiempo en la ruta": 0..ordenRuta.length.
  // En step=0: camión lleno (todo highlight). En step=k: clientes con índice <k ya entregados (atenuados).
  const [step, setStep] = useState(0);

  const ordenIdx = useMemo(() => {
    const m = new Map<number, number>();
    ordenRuta.forEach((cid, i) => m.set(cid, i));
    return m;
  }, [ordenRuta]);

  // Slots por cliente para mostrar resumen.
  const slotsTotal = distribucion.layout.length;
  const barrilesTotal = distribucion.layout.filter((s) => s.es_barril).length / 4; // 4 slots por barril
  const cajasTotal = distribucion.layout.filter((s) => !s.es_barril).length;
  const capacidad = distribucion.n_palets * PISOS * FILAS * COLS;

  // Aplica "gravedad" en el step actual: por cada columna vertical (palet, fila, col),
  // los slots cuyo cliente aún no ha sido entregado se reapilan desde piso 0
  // sin huecos. Los entregados se ocultan (no se renderizan).
  const slotsVisibles = useMemo(() => {
    function aunEnCamion(cid: number): boolean {
      const idx = ordenIdx.get(cid);
      if (idx === undefined) return true;  // cliente fuera del orden → dejar visible
      return idx >= step;
    }

    const porColumna = new Map<string, SlotOcupado[]>();
    for (const s of distribucion.layout) {
      const key = `${s.palet}-${s.fila}-${s.col}`;
      const arr = porColumna.get(key);
      if (arr) arr.push(s);
      else porColumna.set(key, [s]);
    }

    const out: { slot: SlotOcupado; pisoEf: number }[] = [];
    for (const arr of porColumna.values()) {
      arr.sort((a, b) => a.piso - b.piso);
      let pisoEf = 0;
      for (const s of arr) {
        if (!aunEnCamion(s.cliente_id)) continue;  // entregado → no renderizar
        out.push({ slot: s, pisoEf });
        pisoEf += 1;
      }
    }
    return out;
  }, [distribucion.layout, ordenIdx, step]);

  return (
    <div style={{ width: "100%", height: "100%", position: "relative" }}>
      <Canvas
        className="canvas-3d"
        shadows
        camera={{ position: [16, 12, 16], fov: 35 }}
      >
        <ambientLight intensity={0.6} />
        <directionalLight position={[10, 18, 10]} intensity={1.2} castShadow />

        {/* Suelo */}
        <mesh rotation={[-Math.PI / 2, 0, 0]} position={[0, 0, 0]} receiveShadow>
          <planeGeometry args={[60, 60]} />
          <meshStandardMaterial color="#0e1115" />
        </mesh>

        {/* Bases de palets */}
        {Array.from({ length: distribucion.n_palets }).map((_, i) => (
          <PaletBase key={i} paletIdx={i} resaltado={false} />
        ))}

        {/* Slots ocupados (con gravedad aplicada según step) */}
        {slotsVisibles.map(({ slot, pisoEf }, i) => (
          <Slot key={i} slot={slot} pisoEf={pisoEf} />
        ))}

        {/* Etiquetas de eje */}
        <Text position={[-1, 0.2, FILAS / 2]} fontSize={0.35} color="#666">
          fila →
        </Text>

        <OrbitControls makeDefault />
      </Canvas>

      {/* Controles */}
      <div style={{
        position: "absolute", bottom: 12, left: 12, right: 12, display: "flex", gap: 10, alignItems: "center",
        background: "rgba(22, 26, 33, 0.92)", borderRadius: 6, padding: "8px 12px",
      }}>
        <span style={{ fontSize: 12, color: "var(--text-muted)" }}>Tiempo en ruta:</span>
        <span style={{ fontSize: 12, color: "var(--text)", minWidth: 80 }}>
          {step === 0 ? "Salida (camión lleno)" :
           step >= ordenRuta.length ? "Final (vacío)" :
           `Tras entrega ${step}`}
        </span>
        <input
          type="range"
          min={0}
          max={ordenRuta.length}
          step={1}
          value={step}
          onChange={(e) => setStep(Number(e.target.value))}
          style={{ flex: 1 }}
          disabled={ordenRuta.length === 0}
        />
        <span style={{ fontSize: 12, color: "var(--text-muted)", whiteSpace: "nowrap" }}>
          {slotsTotal}/{capacidad} slots ({Math.round(slotsTotal / capacidad * 100)}%) ·
          {" "}{cajasTotal} cajas + {barrilesTotal.toFixed(0)} barriles
        </span>
      </div>

      <div className="canvas-3d__hint">arrastrar = rotar · rueda = zoom · click derecho = pan</div>
    </div>
  );
}
