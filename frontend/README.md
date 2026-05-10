# Damm Smart Truck — Frontend

Visualización de los resultados del pipeline VRP (SA-ruta + SA-distribución).
Stack: **Vite + React + TypeScript + react-leaflet + @react-three/fiber**.

## Setup

```bash
cd frontend
npm install
npm run dev          # arranca dev server en http://localhost:5173
```

## Build de producción

```bash
npm run build        # genera dist/ con HTML estático listo para servir
npm run preview      # sirve dist/ localmente para probar
```

## Cómo usarlo

1. Arranca el dev server con `npm run dev`.
2. Pulsa **Seleccionar JSON** y carga uno de:
   - **`resultado_<dia>.json`** — output del orquestador. Vista completa: rutas + distribución 3D por camión + KPIs.
   - **`salida.json`** — output directo de `damm` (sin distribuciones). Solo vista de mapa, sin 3D.

## Lo que verás

### Sidebar izquierdo — lista de camiones
Una tarjeta por camión con:
- Color de la ruta (consistente con el mapa)
- Nº de paradas, clientes y distancia
- Barra de utilización (cajas-equivalentes / capacidad)
- Indicadores de overflow ⚠ y retraso ⏰

Click para filtrar el mapa a ese camión.

### Vista principal — Mapa de rutas
- Polilíneas por camión (color = camión).
- 🏭 Depósito en el centro (Damm El Prat por defecto).
- Cada parada usada lleva:
  - 🟠 Anillo naranja sólido si **algún cliente tiene ventana horaria estrecha** (< 2h).
  - 🔵 Anillo azul punteado si **algún cliente devuelve envases** (volumen_devolver > 0).
  - Hover: tooltip con clientes, ventanas, volumen.
  - Click: detalle completo en el sidebar derecho.

### Vista principal — Distribución 3D
Visible cuando el JSON incluye distribuciones (modo orquestador):
- Caja por slot del camión (PISOS×FILAS×COLS = 6×5×2 por palet).
- Color del slot = cliente destinatario (mismo que el marcador del mapa).
- Barriles tienen aspecto metálico, cajas mate.
- Slider **Tiempo en ruta**: arrastra para ver cómo se vacía el camión a medida que entrega clientes en orden.

### Sidebar derecho
- KPIs globales (coste, distancia total, % cobertura)
- KPIs del camión seleccionado
- Detalle de la parada clickada
- Mini-Gantt de los horarios estimados

## Pipeline completo (recordatorio)

```
test/juego_<dia>.csv
  + mapa/out/cliente_index.csv
  + mapa/out/dist_matrix.csv
  + mapa/out/time_matrix.csv
  + test/Horarios Entrega.csv
        │
        ▼   pipeline/cargar_csv.py
   datos_<dia>.json
        │
        ▼   ./damm --from-json datos_<dia>.json salida.json
   salida.json (rutas)
        │
        ▼   pipeline/items_por_camion.py
   dist_inputs/camion_*.json
        │
        ▼   ./damm-dist (uno por camión, en paralelo)
   dist_outputs/camion_*.json
        │
        ▼   pipeline/orquestador.py (próximamente)
   resultado_<dia>.json   ◀── lo que carga este frontend
```
