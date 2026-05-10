#include "algo/distribucio/heuristica/h_distr.cc"
#include "algo/distribucio/operadores/op_distr.cc"
#include "algo/distribucio/greedy_inicial.cc"
#include "algo/sa/sa.cc"
#include <iostream>
#include <fstream>

using namespace std;

int main() {
    // --- Test case: furgoneta 3 pallets, 24 items, 3 clientes ---
    vector<Item> items = {
        // Cliente 100: 4 cajas Estrella + 2 barriles Estrella
        {1, 100, false}, {1, 100, false}, {1, 100, false}, {1, 100, false},
        {2, 100, true},  {2, 100, true},
        // Cliente 200: 6 cajas Voll-Damm + 2 barriles Voll-Damm
        {3, 200, false}, {3, 200, false}, {3, 200, false},
        {3, 200, false}, {3, 200, false}, {3, 200, false},
        {4, 200, true},  {4, 200, true},
        // Cliente 300: 6 cajas Estrella + 4 cajas Inedit
        {1, 300, false}, {1, 300, false}, {1, 300, false},
        {1, 300, false}, {1, 300, false}, {1, 300, false},
        {5, 300, false}, {5, 300, false}, {5, 300, false}, {5, 300, false}
    };

    vector<uint16_t> orden_ruta = {300, 100, 200};
    int n_palets = 3;

    // --- Greedy inicial ---
    Solucion inicial = greedy_inicial(items, n_palets);
    double fit_ini = fitness(inicial, orden_ruta);
    cerr << "Fitness inicial (greedy): " << fit_ini << endl;

    // --- SA ---
    ParametrosSA params;
    params.T_init = 10.0;
    params.T_min = 0.01;
    params.cooling_rate = 0.995;
    params.iter_por_temp = 50;

    mt19937 rng(42);

    auto costo = [&](const Solucion& s) { return fitness(s, orden_ruta); };

    auto vecino_fn = [](const Solucion& s, mt19937& r) -> Solucion {
        for (int intento = 0; intento < 100; ++intento) {
            Solucion candidato;
            if (uniform_int_distribution<int>(0, 1)(r) == 0)
                candidato = vecino_swap(s, r);
            else
                candidato = vecino_mover(s, r);
            if (es_valida(candidato)) return candidato;
        }
        return s;
    };

    Solucion mejor = simulated_annealing<Solucion>(inicial, costo, vecino_fn, params, rng);
    double fit_fin = fitness(mejor, orden_ruta);

    // --- Resultados ---
    cerr << "Fitness final:   " << fit_fin << endl;
    cerr << "Mejora:          " << (fit_ini - fit_fin) << endl;
    cerr << "\n--- Breakdown ---" << endl;
    cerr << "F1 (frag producto): " << fragmentacion_producto(mejor) << endl;
    cerr << "F2 (frag cliente):  " << fragmentacion_cliente(mejor) << endl;
    cerr << "F3 (accesibilidad): " << accesibilidad(mejor, orden_ruta) << endl;
    cerr << "Valida: " << (es_valida(mejor) ? "si" : "NO") << endl;

    // --- JSON para visualizador ---
    ofstream out("resultado.json");
    out << "{\n";
    out << "  \"n_palets\": " << mejor.n_palets << ",\n";
    out << "  \"fitness\": " << fit_fin << ",\n";
    out << "  \"fitness_inicial\": " << fit_ini << ",\n";
    out << "  \"orden_ruta\": [";
    for (size_t i = 0; i < orden_ruta.size(); ++i) {
        if (i > 0) out << ", ";
        out << orden_ruta[i];
    }
    out << "],\n";
    out << "  \"items\": [\n";
    bool first = true;
    for (int pal = 0; pal < mejor.n_palets; ++pal)
        for (int piso = 0; piso < Solucion::PISOS; ++piso)
            for (int f = 0; f < Solucion::FILAS; ++f)
                for (int c = 0; c < Solucion::COLS; ++c) {
                    const Item& item = mejor.layout[pal][piso][f][c];
                    if (item.vacio()) continue;
                    if (!first) out << ",\n";
                    first = false;
                    out << "    {\"palet\": " << pal
                        << ", \"piso\": " << piso
                        << ", \"fila\": " << f
                        << ", \"col\": " << c
                        << ", \"material_id\": " << item.material_id
                        << ", \"cliente_id\": " << item.cliente_id
                        << ", \"es_barril\": " << (item.es_barril ? "true" : "false")
                        << "}";
                }
    out << "\n  ]\n}\n";

    cerr << "\nResultado guardado en resultado.json" << endl;
    return 0;
}
