#ifndef CARGAR_JSON_H
#define CARGAR_JSON_H

#include "../algo/ruta/estado_ruta.h"
#include <string>

// Carga un JSON DatosProblema producido por pipeline/cargar_csv.py.
// Lanza std::runtime_error si el archivo no existe, no se parsea, o
// faltan campos obligatorios. El formato debe coincidir con
// pipeline/contratos.py:DatosProblema (cajas-equivalentes, m, min).
//
// Ignora campos extra (nombre, destino_id_externo, id de cliente/parada)
// para tolerar evoluciones del esquema sin romper el parser.
//
// Devuelve true en éxito, false si el archivo no se pudo abrir.
bool cargar_datos_problema(const std::string& path, DatosProblema& out);

#endif
