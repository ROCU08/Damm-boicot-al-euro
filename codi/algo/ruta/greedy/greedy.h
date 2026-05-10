#ifndef GREEDY_H
#define GREEDY_H

#include "../estado_ruta.h"

// =============================================================================
// Generadores de estado inicial para el SA.
// Ambos producen un EstadoRuta con cachés y `atendido_ruta` ya inicializados,
// listo para pasarle a simulated_annealing(...).
// =============================================================================

// Greedy 1: "Pocas paradas".
// Pasos:
//   1) Set cover greedy global: en cada iteración elige la parada que cubre
//      más clientes aún sin cubrir.
//   2) Bin packing: distribuye las paradas seleccionadas entre los camiones,
//      asignando cada una al camión con más capacidad libre (ordenadas por
//      volumen total decreciente).
//   3) Nearest neighbor: reordena cada ruta visitando siempre la parada más
//      cercana a la actual.
//
// Características:
//   - Cada parada aparece como mucho en una ruta.
//   - Optimizado para usar el mínimo número de paradas.
//   - Puede producir camiones desbalanceados si el set cover concentra
//     paradas grandes en pocas posiciones.
EstadoRuta greedy_set_cover(const DatosProblema& datos, double minutos_por_volumen);

// Greedy 2: "Cargas equilibradas".
// Pasos:
//   1) Reparte clientes entre camiones por volumen (bin packing por cliente):
//      ordenados por volumen_recoger desc, cada cliente va al camión con más
//      libre.
//   2) Por cada camión, set cover greedy LIMITADO a sus clientes asignados
//      (busca paradas que cubran exclusivamente este subconjunto).
//   3) Nearest neighbor por ruta.
//
// Características:
//   - Camiones más equilibrados en carga inicial.
//   - Una misma parada puede aparecer en varias rutas si sirve clientes
//     asignados a camiones distintos (clientes_atendidos disjuntos: invariante
//     respetado, pero geométricamente se duplica el desplazamiento).
//   - Puede usar más paradas globales que el greedy 1.
EstadoRuta greedy_por_cliente(const DatosProblema& datos, double minutos_por_volumen);

#endif
