#ifndef OP_RUTA_H
#define OP_RUTA_H

#include "../estado_ruta.h"

// =============================================================================
// Operadores de vecindad para SA. Estrategia A (feasibility-preserving):
// el movimiento solo se aplica si la solución resultante respeta capacidad y
// el invariante de no-doble-servicio (ningún cliente en clientes_atendidos
// de dos visitas distintas a la vez).
//
// REQUISITO: antes de usar operadores hay que llamar a `inicializar_estado`.
// Los operadores leen y mantienen `estado.atendido_ruta` (cache O(1) para
// detectar solapamiento y localizar clientes).
// =============================================================================

// --- Operadores que NO cambian QUÉ clientes se atienden globalmente ---

bool swap_intra(RutaCamion& ruta, int i, int j,
                const DatosProblema& datos,
                const Camion& camion,
                double minutos_por_volumen);

// PRECONDICIÓN: ruta_a y ruta_b deben ser objetos distintos.
bool relocate(EstadoRuta& estado,
              int ruta_a_idx, int i,
              int ruta_b_idx, int j,
              const DatosProblema& datos,
              double minutos_por_volumen);

// --- Operadores que SÍ cambian QUÉ clientes se atienden ---

bool swap_parada(EstadoRuta& estado, int ruta_idx, int i,
                 int parada_nueva,
                 const vector<int>& clientes_a_atender,
                 const DatosProblema& datos,
                 double minutos_por_volumen);

bool insertar_parada(EstadoRuta& estado, int ruta_idx, int j,
                     int parada_nueva,
                     const vector<int>& clientes_a_atender,
                     const DatosProblema& datos,
                     double minutos_por_volumen);

bool eliminar_parada(EstadoRuta& estado, int ruta_idx, int i,
                     const DatosProblema& datos,
                     double minutos_por_volumen);

// Mueve un cliente ya atendido a otra visita existente. La visita destino debe
// tener al cliente en clientes_servidos de su parada. Localiza la posición
// actual mediante el cache `atendido_ruta`. Rechaza si:
//   - el cliente no está atendido en ningún sitio
//   - la parada destino no sirve al cliente
//   - tras el movimiento se viola capacidad en alguna ruta afectada
bool mover_cliente(EstadoRuta& estado, int cliente_id,
                   int ruta_dst, int pos_dst,
                   const DatosProblema& datos,
                   double minutos_por_volumen);

// --- Helpers para callers ---

// Devuelve el subconjunto de clientes_servidos de parada_id que NO están
// actualmente atendidos por ninguna visita (consulta directa al cache).
vector<int> clientes_no_cubiertos_de(const EstadoRuta& estado,
                                     const DatosProblema& datos,
                                     int parada_id);

#endif
