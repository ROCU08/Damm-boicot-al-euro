// Conversión "minutos desde 00:00" -> "HH:MM" para etiquetas legibles.
export function minutosToHHMM(min: number): string {
  if (!Number.isFinite(min)) return "—";
  const m = Math.max(0, Math.round(min));
  const h = Math.floor(m / 60);
  const r = m % 60;
  return `${h.toString().padStart(2, "0")}:${r.toString().padStart(2, "0")}`;
}

// Devuelve "1h 23min" / "23min" / "12s" según magnitud de minutos.
export function formatDuracion(min: number): string {
  if (!Number.isFinite(min) || min <= 0) return "0min";
  if (min < 1) return `${Math.round(min * 60)}s`;
  const h = Math.floor(min / 60);
  const m = Math.round(min - h * 60);
  if (h === 0) return `${m}min`;
  return `${h}h ${m}min`;
}

// Para distancias en metros: "1.4 km" / "850 m"
export function formatDistancia(m: number): string {
  if (!Number.isFinite(m) || m <= 0) return "0 m";
  if (m < 1000) return `${Math.round(m)} m`;
  return `${(m / 1000).toFixed(1)} km`;
}

// Detección "ventana horaria estrecha" (< 2h) para los iconos del mapa.
export const VENTANA_ESTRECHA_MIN = 2 * 60;

export function esVentanaEstrecha(hora_ini: number, hora_fin: number): boolean {
  return hora_fin - hora_ini < VENTANA_ESTRECHA_MIN;
}
