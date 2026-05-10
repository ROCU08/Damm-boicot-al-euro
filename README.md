# Damm-boicot-al-euro

Solver VRP para el reto **Damm Smart Truck** (Interhack BCN 2026).
Optimiza rutas + distribución de palets en dos fases con Simulated Annealing.

## Pipeline

```
   test/juego_<dia>.csv                 (pedidos por línea de producto)
   test/Horarios Entrega.csv            (ventanas horarias por cliente×día)
   mapa/out/cliente_index.csv           (Destino_ID → idx, lat, lon)
   mapa/out/dist_matrix.csv             (matriz real OSRM, metros)
   mapa/out/time_matrix.csv             (matriz real OSRM, segundos)
            │
            ▼   pipeline/cargar_csv.py
       datos_<dia>.json   (DatosProblema)
            │
            ▼   ./damm --from-json datos.json salida.json   ← SA-ruta
       salida_ruta.json   (rutas + clientes_atendidos por visita)
            │
            ▼   pipeline/items_por_camion.py
       dist_inputs/camion_*.json   (1 por camión, items expandidos)
            │
            ▼   ./damm-dist  ×N en paralelo                 ← SA-distribución
       dist_outputs/camion_*.json  (layout 3D por camión)
            │
            ▼   merge en pipeline/orquestador.py
       resultado.json   ◄── lo que carga el frontend
```

## Quickstart end-to-end

Necesitas: g++ ≥ 11 (C++17), `make`, Python 3.10+, Node 18+.

```bash
# 1. Compilar los dos binarios (siempre arrancar limpio si vienes de otra rama):
make clean && make damm damm-dist

# 2. Correr el pipeline completo para un día (CSV → resultado.json):
make pipeline JUEGO=test/juego_lunes.csv OUT_DIR=out_lunes/

# 3. Lanzar el frontend y cargar out_lunes/resultado.json:
cd frontend && npm install && npm run dev
```

El target `pipeline`:
1. `pipeline/cargar_csv.py` cruza CSVs → `out_lunes/datos.json`.
2. `./damm --from-json` produce `out_lunes/salida_ruta.json`.
3. `pipeline/items_por_camion.py` expande barriles ×4 y produce `dist_inputs/camion_*.json`.
4. `ThreadPoolExecutor` lanza `./damm-dist` en paralelo, uno por camión, en `dist_outputs/`.
5. `pipeline/orquestador.py` calcula horarios por visita y junta todo en `out_lunes/resultado.json`.

## Convenciones

- **Volumen** en cajas-equivalentes (1 palet = 60 cajas).
  - `1 caja CAJ` → 1 unidad
  - `N unidades sueltas UN` → `ceil(N/12)` cajas
  - `1 barril BRL` → 4 cajas (es_barril=true; en SA-distr ocupan 4 slots vía expansión)
  - Envases retornables → cuentan en `volumen_devolver`, no se cargan al inicio
- **Distancia** en metros, **tiempo** en minutos (origen 00:00 para horarios).
- **Flota fija** por defecto: 2 furgonetas (3 palets) + 3 camiones medianos (6 palets) + 1 grande (8) = 1920 cajas. Configurable con `FLOTA=n_furgo,n_cam6,n_cam8`.
- **Depósito**: Damm El Prat (`41.317, 2.085`) por defecto. Configurable con `--deposito-lat/--deposito-lon` en el orquestador.

## Estructura del repo

```
codi/                          # C++17
  main.cc                      # damm: CLI + driver SA-ruta (sintético / --from-json)
  distribucio_main.cc          # damm-dist: driver SA-distribución (single-TU)
  algo/
    sa/sa.cc                   # template SA genérico
    ruta/                      # SA-ruta: estado, heurística, operadores, vecinos, greedy
    distribucio/               # SA-distribución: estado 4D, heurística, operadores, greedy
    mapa/                      # loader OSRM matrices (no usado por damm; opcional)
  io/
    cargar_json.{h,cc}         # parser JSON ad-hoc (compose-by-include via json_parser.h)
    cargar_dist.h              # loader RutaInputDist (header-only)
    exportar.{h,cc}            # serializa EstadoRuta (SA-ruta) a JSON
    exportar_dist.h            # serializa Solucion (SA-distr) a JSON (header-only)
    json_parser.h              # parser JSON minimalista, header-only

pipeline/                      # Python
  contratos.py                 # dataclasses con esquemas JSON
  cargar_csv.py                # CSVs → DatosProblema JSON
  items_por_camion.py          # salida_ruta + juego.csv → dist_inputs/
  horarios.py                  # cálculo hora_llegada_estimada y retraso_min
  orquestador.py               # end-to-end con multiprocessing

frontend/                      # Vite + React + TS + leaflet + R3F
  src/
    types.ts, utils/, components/

mapa/                          # pipeline OSRM previo (cliente_index, matrices)
test/                          # juegos CSV de la semana + Horarios Entrega + ZM040
viz/                           # plots matplotlib de experimentos
```

## Comandos útiles

```bash
# Solo el SA-ruta sintético (mantiene compat con la firma original)
make run SCENARIO=medio SEED=99

# Solo el SA-ruta sobre datos reales (asume que ya existe datos_lunes.json)
make run-json DATOS=out_lunes/datos.json OUTPUT=out_lunes/salida_ruta.json

# Solo el cargador CSV (sin correr SA)
python3 pipeline/cargar_csv.py --juego test/juego_lunes.csv --output datos_lunes.json

# Pipeline completo para varios días en secuencia
for d in lunes martes miercoles jueves viernes; do
    make pipeline JUEGO=test/juego_$d.csv OUT_DIR=out_$d/
done

# Limpieza
make clean        # binarios + .o + .d + JSONs raíz
make clean-all    # también borra viz/output/
```

## Scripts disponibles

El proyecto incluye varios scripts automáticos para diferentes flujos de trabajo:

### `run.sh` – Compilación rápida + ejecución básica
Compila el solver y ejecuta una prueba rápida con visualización.

```bash
./run.sh
```

**Flujo:**
1. Compila con `g++ -std=c++14 -O2`
2. Ejecuta el SA
3. Visualiza resultados con `visualizar.py`

**Cuándo usarlo:** Desarrollo rápido y pruebas locales (Linux/macOS).

---

### `start_all.sh` – Pipeline completo para todos los días (Linux/macOS)
Ejecuta el pipeline completo secuencialmente para cada día de la semana (lunes, martes, miércoles, jueves, viernes, pruebas_SA).

```bash
./start_all.sh
```

**Flujo por día:**
1. Limpia y recompila los binarios
2. Ejecuta `make pipeline` para generar `out_<día>/resultado.json`
3. Lanza `npm run dev` en el frontend

**Nota:** El script se pausa en cada día porque `npm run dev` ejecuta un servidor bloqueante. Presiona `Ctrl+C` para pasar al siguiente día.

**Cuándo usarlo:** Procesar todos los días de una semana automáticamente.

---

### `start_all.bat` – Pipeline para un día específico (Windows)
Versión Windows que ejecuta el pipeline completo para un único día especificado por número.

```cmd
start_all.bat <numero>
```

**Parámetros:**
- `1` → lunes
- `2` → martes
- `3` → miércoles
- `4` → jueves
- `5` → viernes

**Ejemplo:**
```cmd
start_all.bat 1
```

**Flujo:**
1. Limpia y recompila los binarios
2. Ejecuta `make pipeline` para el día seleccionado
3. Abre `npm run dev` en una nueva ventana del frontend

**Cuándo usarlo:** Procesar un día específico en Windows.

---

### `visualizar.py` – Visualización 3D de palets
Genera gráficos 3D de la distribución de ítems en cada palet desde un archivo JSON de resultado.

```bash
python3 visualizar.py [archivo.json]
```

**Parámetros:**
- `archivo.json` (opcional): Ruta al archivo de resultado. Por defecto: `resultado.json`

**Salida:**
- Ventana interactiva con subplots 3D (uno por palet)
- Código de color por cliente para identificar patrones

**Ejemplo:**
```bash
python3 visualizar.py out_lunes/resultado.json
```

**Cuándo usarlo:** Inspeccionar visualmente cómo se distribuyen los ítems en los palets tras la optimización.

---

### `analizar_resultados.py` – Análisis experimental
Analiza resultados de experimentos y genera gráficos estadísticos (comparación de operadores, convergencia, etc.).

```bash
python3 analizar_resultados.py [archivo_csv]
```

**Requisitos:**
- Entrada: CSV con columnas `experimento`, `config`, `fitness_fin`, etc.
- Ubicación esperada: `viz/output/experimentos.csv`

**Salida:**
- Gráficos boxplot, histogramas y trazas de convergencia
- Guardados en `viz/output/`

**Cuándo usarlo:** Análisis comparativo de configuraciones de SA y rendimiento de operadores.

---

## Reglas de modelado importantes

- **1 cliente = 1 parada** en el cargador actual (cada cliente tiene su propia parada en el mismo punto). El SA-ruta soporta varios clientes por parada (set cover) pero la fuente actual no agrupa direcciones; cuando lo hagamos vivirá en `cargar_csv.py` usando `loading_points_per_client.csv`.
- **Barril = 4 ítems-caja idénticos** se hace en el orquestador (`pipeline/items_por_camion.py`), no en el SA-distr. Eso evita modificar el modelo `Solucion` y permite que la heurística de fragmentación los junte espacialmente.
- **Reproducibilidad**: el template SA en `codi/algo/sa/sa.cc` usa `random_device` interno → no es reproducible aunque pases `--seed`. Para el hackathon es aceptable; documentado para el futuro.

## Outputs intermedios para debug

Cada paso del orquestador escribe su intermedio en `OUT_DIR/`. Útil para:
- Inspeccionar `datos.json` antes de invocar el SA → comprobar agregación de cajas, ventanas horarias, flota dimensionada.
- Inspeccionar `salida_ruta.json` antes de la distribución → ver clientes_no_servidos, retrasos.
- Inspeccionar un `dist_outputs/camion_<id>.json` aislado → entender por qué la fragmentación es alta.
