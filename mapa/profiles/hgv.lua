-- Perfil OSRM para camiones / furgonetas (HGV).
--
-- Esta es una plantilla minimalista. La versión por defecto que usa s3 es la
-- /opt/car.lua que ya viene con la imagen ghcr.io/project-osrm/osrm-backend.
--
-- Cuando quieras endurecer las reglas para camión:
--   1. Copia el contenido de https://github.com/Project-OSRM/osrm-backend/blob/master/profiles/car.lua aquí.
--   2. En la sección `default_speeds`, reduce las velocidades de:
--        residential, living_street, service → 25 km/h
--        primary, secondary, tertiary       → 60-70 km/h
--   3. En la sección `barrier_whitelist`, retira `bollard` (los camiones no pasan por bolardos).
--   4. En `highway_whitelist`, NO incluyas `pedestrian`, `footway`, `cycleway`, `path`, `track`, `steps`.
--   5. En `setup()`, añade restricciones por `maxweight`, `maxheight`, `maxlength`, `maxwidth`
--      contra las dimensiones de tu vehículo medio (p.ej. peso 7.5 t, altura 3.5 m).
--   6. Honra `hgv=no`, `motor_vehicle=no`.
--
-- Hasta entonces, s3 ejecuta osrm-extract con /opt/car.lua que es el perfil
-- de coche estándar. Para áreas urbanas medias da resultados razonables, pero
-- NO impide rutas por calles peatonales o pasos prohibidos a camiones.

api_version = 4

-- Stub mínimo: si OSRM intenta cargar este archivo, lanza un error claro.
function setup()
  error("mapa/profiles/hgv.lua es solo plantilla. Edítalo con un perfil real o usa /opt/car.lua")
end
