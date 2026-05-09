#include "h_ruta.h"
#include <algorithm>

using namespace std;

// =============================================================================
// FUNCIÓN DE COSTE
// Lee cachés escalares (O(num_camiones)) y calcula la cobertura recorriendo
// paradas → clientes_servidos (O(sum n_paradas_ruta * avg_clientes_servidos)).
// =============================================================================

double calcular_coste(const EstadoRuta& estado,
                      const DatosProblema& datos,
                      const PesosCoste& pesos) {
    double total = 0.0;
    int n_clientes = (int)datos.clientes.size();

    for (int k = 0; k < (int)estado.rutas.size(); ++k) {
        const RutaCamion& ruta = estado.rutas[k];
        if (ruta.paradas.empty()) continue;
        

        const Camion& camion = datos.camiones[k];

        // Factor 1: distancia
        total += pesos.w_dist * ruta.total_distancia;

        // Factor 2: maximizar carga inicial → penalizar volumen no aprovechado
        double no_aprovechada = max(0.0, camion.capacidad_volumen - ruta.total_carga_inicial);
        total += pesos.w_carga_baja * no_aprovechada;

        // Factor 3: ventanas horarias
        total += pesos.w_ventanas * ruta.total_retraso;

        // Factor 4: pico de volumen (alto si se recogen retornables temprano)
        total += pesos.w_retorno_temprano * ruta.total_pico_volumen;

        // Restricción dura de capacidad. Con operadores feasibility-preserving
        // este término debería ser 0; red de seguridad.
        double exceso_cap = max(0.0, ruta.total_pico_volumen - camion.capacidad_volumen);
        total += pesos.w_capacidad * exceso_cap;
    }

    // Cobertura: clientes que están explícitamente atendidos en alguna visita.
    // Solo cuentan los que aparecen en clientes_atendidos, no todos los
    // clientes_servidos de las paradas elegidas.
    vector<bool> cubierto(n_clientes, false);
    for (const RutaCamion& ruta : estado.rutas) {
        for (const auto& clientes_visita : ruta.clientes_atendidos) {
            for (int cid : clientes_visita) {
                cubierto[cid] = true;
            }
        }
    }
    int no_servidos = 0;
    for (bool c : cubierto) if (!c) ++no_servidos;
    if (no_servidos > 0) {
        total += pesos.w_no_servidos * no_servidos;
    }

    return total;
}
