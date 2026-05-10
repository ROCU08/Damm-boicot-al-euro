import { useEffect, useMemo, useRef } from "react";
import L from "leaflet";
import { CircleMarker, MapContainer, Polyline, TileLayer, Tooltip, useMap } from "react-leaflet";

import type { Coord, ResultadoFinal, RutaSolucion } from "../types";
import { colorCamion } from "../utils/colors";
import { esVentanaEstrecha, minutosToHHMM } from "../utils/time";

interface Props {
  resultado: ResultadoFinal;
  camionSel: number | null;
  paradaSel: number | null;
  onParadaClick: (paradaId: number) => void;
}

function coordToLatLng(c: Coord): [number, number] {
  // En el modelo: x = lon, y = lat
  return [c.y, c.x];
}

// Devuelve la lista de puntos de una ruta como [deposito, p1, p2, ..., deposito].
function puntosDeRuta(ruta: RutaSolucion, paradas: { pos: Coord }[], deposito: Coord): [number, number][] {
  const seq: [number, number][] = [coordToLatLng(deposito)];
  for (const v of ruta.visitas) {
    const p = paradas[v.parada_id];
    if (p) seq.push(coordToLatLng(p.pos));
  }
  seq.push(coordToLatLng(deposito));
  return seq;
}

// Auto-fit del viewport a todas las paradas + depósito.
function FitToBounds({ resultado }: { resultado: ResultadoFinal }) {
  const map = useMap();
  useEffect(() => {
    const pts: [number, number][] = [coordToLatLng(resultado.datos.deposito)];
    for (const p of resultado.datos.paradas) pts.push(coordToLatLng(p.pos));
    if (pts.length === 0) return;
    const bounds = L.latLngBounds(pts);
    map.fitBounds(bounds, { padding: [40, 40] });
  }, [map, resultado]);
  return null;
}

export default function MapaRutas({ resultado, camionSel, paradaSel, onParadaClick }: Props) {
  const { datos, rutas } = resultado;

  // Conjunto de paradas activas en cualquier ruta
  const paradasUsadas = useMemo(() => {
    const s = new Set<number>();
    for (const r of rutas) for (const v of r.visitas) s.add(v.parada_id);
    return s;
  }, [rutas]);

  // Mapa parada -> {camion, indiceVisita} para el color y la etiqueta
  const paradaInfo = useMemo(() => {
    const m = new Map<number, { camion: number; orden: number; clientes: number[] }>();
    for (const r of rutas) {
      r.visitas.forEach((v, i) => {
        m.set(v.parada_id, {
          camion: r.camion_id,
          orden: i + 1,
          clientes: v.clientes_atendidos,
        });
      });
    }
    return m;
  }, [rutas]);

  const center: [number, number] = coordToLatLng(datos.deposito);

  return (
    <MapContainer center={center} zoom={11} className="leaflet-container">
      <TileLayer
        attribution='&copy; OpenStreetMap'
        url="https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png"
      />
      <FitToBounds resultado={resultado} />

      {/* Polilíneas de cada ruta */}
      {rutas.map((r) => {
        const visible = camionSel === null || camionSel === r.camion_id;
        if (!visible) return null;
        const pts = puntosDeRuta(r, datos.paradas, datos.deposito);
        return (
          <Polyline
            key={r.camion_id}
            positions={pts}
            pathOptions={{
              color: colorCamion(r.camion_id),
              weight: 3,
              opacity: camionSel === null ? 0.55 : 0.85,
            }}
          />
        );
      })}

      {/* Depósito */}
      <CircleMarker
        center={center}
        radius={9}
        pathOptions={{ color: "#1a1a1a", fillColor: "var(--accent)" as string, fillOpacity: 1, weight: 2 }}
      >
        <Tooltip permanent direction="top" offset={[0, -8]} className="leaflet-tooltip-deposito">
          🏭 Depósito
        </Tooltip>
      </CircleMarker>

      {/* Paradas */}
      {datos.paradas.map((p) => {
        const info = paradaInfo.get(p.id);
        const usada = paradasUsadas.has(p.id);
        const matchCamion = camionSel === null || (info && info.camion === camionSel);
        if (!matchCamion) return null;

        const color = info ? colorCamion(info.camion) : "#444";
        const radius = usada ? 7 : 4;

        // Detección de iconos: si algún cliente atendido tiene ventana estrecha
        // o devuelve productos, dibujamos un anillo extra de color.
        let estrecha = false;
        let devuelve = false;
        if (info) {
          for (const cid of info.clientes) {
            const c = datos.clientes[cid];
            if (!c) continue;
            if (esVentanaEstrecha(c.hora_ini, c.hora_fin)) estrecha = true;
            if (c.volumen_devolver > 0) devuelve = true;
          }
        }

        const isSel = paradaSel === p.id;

        return (
          <CircleMarker
            key={p.id}
            center={coordToLatLng(p.pos)}
            radius={radius + (isSel ? 3 : 0)}
            pathOptions={{
              color: usada ? "#1a1a1a" : "#888",
              fillColor: color,
              fillOpacity: usada ? 0.95 : 0.4,
              weight: isSel ? 3 : 1.5,
            }}
            eventHandlers={{
              click: () => onParadaClick(p.id),
            }}
          >
            {usada && (
              <Tooltip direction="top" offset={[0, -6]} sticky>
                <div style={{ fontSize: 12, lineHeight: 1.4 }}>
                  <strong>Parada {p.id}</strong>
                  {info && (
                    <>
                      <br />Camión {info.camion} · visita #{info.orden}
                      <br />{info.clientes.length} clientes
                      {info.clientes.slice(0, 3).map((cid) => {
                        const c = datos.clientes[cid];
                        if (!c) return null;
                        return (
                          <div key={cid} style={{ color: "#aaa" }}>
                            · {c.nombre || `cli ${cid}`} ({minutosToHHMM(c.hora_ini)}–{minutosToHHMM(c.hora_fin)})
                          </div>
                        );
                      })}
                      {info.clientes.length > 3 && (
                        <div style={{ color: "#888" }}>· … +{info.clientes.length - 3} más</div>
                      )}
                    </>
                  )}
                  {(estrecha || devuelve) && (
                    <div style={{ marginTop: 4 }}>
                      {estrecha && <span title="Ventana estrecha"> ⏰ </span>}
                      {devuelve && <span title="Devuelve envases"> ↩ </span>}
                    </div>
                  )}
                </div>
              </Tooltip>
            )}
          </CircleMarker>
        );
      })}

      {/* Anillo extra para iconos: dibujamos un segundo CircleMarker más grande,
          sin relleno, en color naranja (estrecha) o azul (devuelve). */}
      {datos.paradas.map((p) => {
        const info = paradaInfo.get(p.id);
        if (!info) return null;
        const matchCamion = camionSel === null || info.camion === camionSel;
        if (!matchCamion) return null;

        let estrecha = false;
        let devuelve = false;
        for (const cid of info.clientes) {
          const c = datos.clientes[cid];
          if (!c) continue;
          if (esVentanaEstrecha(c.hora_ini, c.hora_fin)) estrecha = true;
          if (c.volumen_devolver > 0) devuelve = true;
        }
        const anillos: JSX.Element[] = [];
        if (estrecha) {
          anillos.push(
            <CircleMarker
              key={`${p.id}-ring-w`}
              center={coordToLatLng(p.pos)}
              radius={11}
              pathOptions={{ color: "#f0a500", fillOpacity: 0, weight: 2, opacity: 0.9 }}
              interactive={false}
            />,
          );
        }
        if (devuelve) {
          anillos.push(
            <CircleMarker
              key={`${p.id}-ring-d`}
              center={coordToLatLng(p.pos)}
              radius={estrecha ? 14 : 11}
              pathOptions={{ color: "#6fb1f3", fillOpacity: 0, weight: 2, opacity: 0.9, dashArray: "3,3" }}
              interactive={false}
            />,
          );
        }
        return <>{anillos}</>;
      })}
    </MapContainer>
  );
}
