# mapa/ — EEDD estática del área de entregas

Pipeline para construir, una sola vez, la estructura de datos de mapa que consume el SA en C++.

## Salidas (en `mapa/out/`)

Estos ficheros son la **EEDD** que C++ carga desde `codi/algo/mapa/`:

| Fichero | Qué es |
|---|---|
| `bbox.json` | Área de operaciones (recorte percentil + 5% margen). |
| `loading_points_per_client.csv` | Por cada cliente con coords: punto sobre el grafo de calles donde puede parar el camión + flag `hgv_ok`. |
| `loading_points_public.csv` | Catálogo OSM-tagged de zonas de carga, parking de camión, loading docks dentro del bbox. |
| `cliente_index.csv` | Mapping `Destino_ID ↔ idx` estable de las matrices. |
| `dist_matrix.csv` | Matriz N×N en metros (driving), `-1` si no alcanzable. |
| `time_matrix.csv` | Matriz N×N en segundos (driving), `-1` si no alcanzable. |

`mapa/data/` (gitignored) contiene descargas grandes y datos OSRM preparados.

## Requisitos

- Docker + docker compose plugin.
- Python 3.10+ con las deps de `mapa/requirements.txt` (`pip install -r mapa/requirements.txt`).
- ~2-3 GB libres en disco para el PBF + datos OSRM.

Imágenes que se usan:
- `ghcr.io/project-osrm/osrm-backend:latest` — pull directo (sirve para osrm-extract/partition/customize y para el daemon).
- `damm-osmium:local` — se construye localmente desde `Dockerfile.osmium` la primera vez que ejecutas s2 (debian-slim + apt install osmium-tool, ~30 s).

```bash
docker pull ghcr.io/project-osrm/osrm-backend:latest
docker compose -f mapa/docker-compose.yml --profile tools build osmium
```

## Pipeline (6 pasos)

```bash
# Todo de cero (descarga PBF + procesa todo, ~30 min según red):
python -m mapa.build_eedd

# Paso a paso:
python -m mapa.steps.s1_compute_bbox       # bbox del área (sin red)
python -m mapa.steps.s2_download_osm       # baja PBF Catalunya (700 MB) + recorta a bbox
python -m mapa.steps.s3_prepare_osrm       # osrm-extract/partition/customize + daemon
python -m mapa.steps.s4_snap_clients       # snap por cliente (necesita OSRM up)
python -m mapa.steps.s5_overpass_parking   # query Overpass (necesita internet)
python -m mapa.steps.s6_compute_matrices   # matriz N×N (necesita OSRM up)
```

Los pasos son resumibles: cada uno tiene cache incremental y/o flags para saltarse subpasos.

## Levantar/parar OSRM manualmente

```bash
docker compose -f mapa/docker-compose.yml up -d        # arranca
docker compose -f mapa/docker-compose.yml logs -f osrm # logs
docker compose -f mapa/docker-compose.yml down         # para
```

Comprobar que responde:
```bash
curl 'http://localhost:5000/route/v1/driving/2.225,41.560;2.250,41.600?overview=false'
```

## Perfil HGV

Por defecto usa `/opt/car.lua` (bundled en la imagen OSRM). Da rutas razonables para suburbano. Para reglas estrictas de camión (peso/altura, evitar peatonales), edita `mapa/profiles/hgv.lua` siguiendo las indicaciones del fichero, y luego:

```bash
python -m mapa.steps.s3_prepare_osrm --profile mapa/profiles/hgv.lua
```

## API runtime (C++)

`codi/algo/mapa/mapa.{h,cc}` provee la clase `mapa::Mapa`:

```cpp
mapa::Mapa m;
m.cargar("mapa/out");
float metros   = m.dist_m(cliente_a, cliente_b);
float segundos = m.time_s(cliente_a, cliente_b);
const auto& snap = m.snap_cliente(cliente_a);
```

## Out of scope (por ahora)

- Restricciones por vehículo (peso/altura/longitud) en el grafo.
- Ventanas horarias en el matching de rutas.
- Re-cálculo dinámico (la EEDD se regenera offline cuando cambien las direcciones).
