// =============================================================================
// damm-dist — driver del SA-distribución por camión.
//
// Lee un RutaInputDist (JSON producido por pipeline/items_por_camion.py),
// construye una Solución inicial con greedy, corre Simulated Annealing con
// los vecinos `vecino_swap` y `vecino_mover` alternados, y exporta el
// layout 3D resultante a JSON conforme a DistribucionOutput.
//
// Uso:
//   ./damm-dist <input_camion.json> <output_distribucion.json> [seed]
//
// Convención: este TU es el ÚNICO que compone todo el SA-distribución, por
// eso incluye los .cc directamente (los demás módulos siguen el mismo
// patrón que el SA-ruta antes de la refactorización a .cc separados).
// =============================================================================

#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <random>
#include <string>
#include <vector>

// Núcleo del SA-distribución (single-TU compose).
#include "algo/distribucio/estado_distr.cc"
#include "algo/distribucio/heuristica/h_distr.cc"
#include "algo/distribucio/operadores/op_distr.cc"
#include "algo/distribucio/greedy_inicial.cc"

// I/O específico (header-only).
#include "io/cargar_dist.h"
#include "io/exportar_dist.h"

// Template SA (también single-TU, ya consolidado).
#include "algo/sa/sa.cc"

using namespace std;


int main(int argc, char** argv) {
    if (argc < 3) {
        cerr << "Uso: damm-dist <input.json> <output.json> [seed]\n";
        return 1;
    }
    string input_path  = argv[1];
    string output_path = argv[2];
    int seed = (argc > 3) ? atoi(argv[3]) : 42;

    // -------------------------------------------------------------------------
    // Cargar el input (camión + items + orden_ruta + sa_params)
    // -------------------------------------------------------------------------
    RutaInputDist inp;
    try {
        if (!cargar_ruta_input_dist(input_path, inp)) {
            cerr << "Error: no se pudo abrir " << input_path << "\n";
            return 1;
        }
    } catch (const exception& e) {
        cerr << "Error parseando " << input_path << ": " << e.what() << "\n";
        return 1;
    }

    cout << "=== damm-dist: camion " << inp.camion_id
         << " (" << inp.n_palets << " palets, "
         << inp.items.size() << " items, "
         << inp.orden_ruta.size() << " clientes) ===\n";

    int capacidad_total = inp.n_palets * Solucion::PISOS * Solucion::FILAS * Solucion::COLS;
    if ((int)inp.items.size() > capacidad_total) {
        cerr << "  ⚠ items (" << inp.items.size() << ") > capacidad ("
             << capacidad_total << "); algunos no se colocarán.\n";
    }

    // -------------------------------------------------------------------------
    // Estado inicial vía greedy
    // -------------------------------------------------------------------------
    Solucion inicial = greedy_inicial(inp.items, inp.n_palets);

    double fitness_ini = fitness(inicial, inp.orden_ruta);
    cout << "  fitness inicial = " << fitness_ini << "\n";

    // -------------------------------------------------------------------------
    // Simulated Annealing
    // -------------------------------------------------------------------------
    mt19937 gen(static_cast<unsigned>(seed));
    uniform_real_distribution<> coin(0.0, 1.0);

    auto coste_fn = [&](const Solucion& s) {
        return fitness(s, inp.orden_ruta);
    };

    // Vecindario: 50/50 entre swap_items y mover_a_vacio. Si el camión está
    // muy lleno, vecino_mover apenas hará nada (pocos vacíos), pero seguro.
    function<Solucion(const Solucion&)> vecino_fn = [&](const Solucion& s) -> Solucion {
        if (coin(gen) < 0.5) return vecino_swap(s, gen);
        return vecino_mover(s, gen);
    };

    auto t_ini = chrono::steady_clock::now();
    Solucion best = simulated_annealing<Solucion>(
        inicial, coste_fn, vecino_fn,
        inp.params.temp_ini,
        inp.params.temp_fin,
        inp.params.cooling,
        inp.params.iters_temp);
    auto t_fin = chrono::steady_clock::now();
    double secs = chrono::duration<double>(t_fin - t_ini).count();

    double fitness_fin = fitness(best, inp.orden_ruta);
    double mejora = (fitness_ini > 0)
                  ? (fitness_ini - fitness_fin) / fitness_ini * 100.0
                  : 0.0;

    cout << "  fitness final   = " << fitness_fin << "\n";
    cout << "  mejora          = " << mejora << "%\n";
    cout << "  tiempo SA       = " << secs << "s\n";

    // -------------------------------------------------------------------------
    // Desglose de componentes para el frontend
    // -------------------------------------------------------------------------
    ComponentesFitness comp;
    comp.fragmentacion_producto = fragmentacion_producto(best);
    comp.fragmentacion_cliente  = fragmentacion_cliente(best);
    comp.accesibilidad          = accesibilidad(best, inp.orden_ruta);

    // -------------------------------------------------------------------------
    // Exportar JSON
    // -------------------------------------------------------------------------
    if (!exportar_distribucion(best, inp.camion_id, fitness_fin, comp,
                               inp.mat_codigos, output_path)) {
        cerr << "Error escribiendo " << output_path << "\n";
        return 1;
    }
    cout << "  -> " << output_path << "\n";
    return 0;
}
