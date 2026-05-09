#ifndef IO_EXPORTAR_H
#define IO_EXPORTAR_H

#include <string>
#include "../algo/ruta/estado_ruta.h"

// Serializa el estado completo a un fichero JSON: datos del problema
// (clientes, paradas con coordenadas, camiones), solución (rutas,
// clientes_atendidos, escalares), clientes no servidos y coste total.
// El formato lo consume `viz/visualizar.py`.
//
// Devuelve true si escribe correctamente, false si no se puede abrir el path.
bool exportar_json(const EstadoRuta& estado,
                   const DatosProblema& datos,
                   double coste_total,
                   const string& path);

#endif
