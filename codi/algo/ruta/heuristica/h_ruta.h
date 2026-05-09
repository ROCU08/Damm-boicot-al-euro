#ifndef H_RUTA_H
#define H_RUTA_H

#include "../estado_ruta.h"

// Pesos de la función de coste multi-objetivo. EDITAR para tunear el balance.
struct PesosCoste {
    // Factor 1: distancia recorrida.
    double w_dist               = 1.0;

    // Factor 2: maximizar carga inicial (penaliza capacidad infrautilizada).
    double w_carga_baja         = 0.0;

    // Factor 3: ventanas horarias (penaliza retraso sobre cliente.hora_fin).
    double w_ventanas           = 5000.0;

    // Factor 4: evitar retornables temprano (penaliza el pico de volumen).
    double w_retorno_temprano   = 200.0;

    // Restricción dura: el pico de volumen no debe exceder la capacidad.
    // Con operadores feasibility-preserving este término debería quedar a 0
    // en cualquier estado válido. Se mantiene como red de seguridad.
    double w_capacidad          = 100000.0;

    // Penalización por dejar clientes sin atender.
    double w_no_servidos        = 100000.0;

    // Factor 5: tiempo de descarga por unidad de volumen movida.
    double minutos_por_volumen  = 2.0;
};

// Coste total del estado completo. Lee solo cachés escalares de cada
// RutaCamion (mantenidas por recalcular_ruta y por los operadores), por lo
// que es O(num_camiones).
double calcular_coste(const EstadoRuta& estado,
                      const DatosProblema& datos,
                      const PesosCoste& pesos);

#endif
