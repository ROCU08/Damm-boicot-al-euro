#ifndef VECINO_H
#define VECINO_H

#include <random>
#include "../estado_ruta.h"

// Contexto compartido por los generadores de vecinos. El caller construye uno
// y lo pasa a la variante elegida envolviéndola en una lambda para encajar en
// la firma `function<State(const State&)>` que espera simulated_annealing.
//
// Ejemplo de uso:
//   mt19937 gen(seed);
//   ContextoVecino ctx{datos, mins_por_volumen, gen};
//   auto get_neighbor = [&ctx](const EstadoRuta& s) {
//       return vecino_completo(s, ctx);
//   };
//   auto best = simulated_annealing<EstadoRuta>(inicial, coste, get_neighbor, ...);
struct ContextoVecino {
    const DatosProblema& datos;
    double minutos_por_volumen;
    mt19937& gen;
};

// =============================================================================
// Variantes de generación de vecinos. Mismas firmas para poder enchufarlas
// alternativamente al SA y comparar el comportamiento.
// Cada variante hace hasta MAX_INTENTOS pruebas de operador aleatorio; si
// todos los intentos son rechazados, devuelve una copia sin cambios (SA verá
// coste idéntico y seguirá explorando).
// =============================================================================

// 1) Solo swap_intra: mínima exploración intra-ruta. Útil como baseline para
//    ver cuánto aporta cada capa adicional.
EstadoRuta vecino_solo_intra(const EstadoRuta& estado, ContextoVecino& ctx);

// 2) swap_intra + relocate: VRP clásico. Permuta dentro de cada ruta y mueve
//    paradas entre rutas, pero NO cambia qué conjunto de paradas se usa.
EstadoRuta vecino_intra_inter(const EstadoRuta& estado, ContextoVecino& ctx);

// 3) Añade swap_parada, insertar_parada y eliminar_parada: modifica también
//    QUÉ paradas se seleccionan para cubrir clientes. Se acerca al óptimo
//    cuando el conjunto inicial de paradas no es bueno.
EstadoRuta vecino_con_paradas(const EstadoRuta& estado, ContextoVecino& ctx);

// 4) Todo lo anterior + mover_cliente: reasignación fina cliente-level. Útil
//    cuando dos paradas se solapan parcialmente y SA decide qué parada cubre
//    a qué cliente concreto.
EstadoRuta vecino_completo(const EstadoRuta& estado, ContextoVecino& ctx);

#endif
