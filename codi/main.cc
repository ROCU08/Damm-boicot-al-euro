#include <iostream>
#include <string>
#include <random>
#include <cmath>
#include <functional>
#include <chrono>

#include "algo/ruta/estado_ruta.h"
#include "algo/ruta/heuristica/h_ruta.h"
#include "algo/ruta/operadores/op_ruta.h"
#include "algo/ruta/vecino/vecino.h"
#include "algo/ruta/greedy/greedy.h"
#include "io/exportar.h"
#include "io/cargar_json.h"

// El template de SA está en un .cc; lo incluimos directo (es solo un template).
#include "algo/sa/sa.cc"

using namespace std;

// =============================================================================
// Generador de dataset sintético.
// Coordenadas en [0, 100] x [0, 100], velocidad constante.
// Tiempo en MINUTOS desde medianoche (08:00 = 480, 18:00 = 1080).
// =============================================================================

DatosProblema generar_dataset(int n_clientes, int n_paradas, int n_camiones,
                              double cap_camion, int seed) {
    mt19937 gen(seed);
    DatosProblema datos;

    // Centro logístico: en el centro del área para que las rutas radien.
    datos.deposito = {50.0, 50.0};

    uniform_real_distribution<> coord(0.0, 100.0);
    uniform_real_distribution<> vol_recoger(2.0, 8.0);
    uniform_real_distribution<> vol_devolver(0.0, 3.0);
    uniform_int_distribution<> num_cs(1, 3);

    const int hora_jornada_ini = 8 * 60;    // 08:00
    const int hora_jornada_fin = 18 * 60;   // 18:00

    // Clientes (con ventana horaria aleatoria dentro de la jornada)
    datos.clientes.resize(n_clientes);
    for (int c = 0; c < n_clientes; ++c) {
        int h_ini = uniform_int_distribution<>(hora_jornada_ini, hora_jornada_ini + 360)(gen);
        int h_fin = h_ini + uniform_int_distribution<>(120, 300)(gen);
        if (h_fin > hora_jornada_fin) h_fin = hora_jornada_fin;

        datos.clientes[c].id = c;
        datos.clientes[c].hora_ini = h_ini;
        datos.clientes[c].hora_fin = h_fin;
        datos.clientes[c].volumen_recoger = vol_recoger(gen);
        datos.clientes[c].volumen_devolver = vol_devolver(gen);
    }

    // Paradas con coordenadas aleatorias.
    datos.paradas.resize(n_paradas);
    for (int p = 0; p < n_paradas; ++p) {
        datos.paradas[p].id = p;
        datos.paradas[p].pos.x = coord(gen);
        datos.paradas[p].pos.y = coord(gen);
    }

    // Asignar 1-3 clientes a cada parada (con solapamiento entre paradas).
    uniform_int_distribution<> cliente_idx(0, n_clientes - 1);
    for (int p = 0; p < n_paradas; ++p) {
        int n_cs = num_cs(gen);
        for (int x = 0; x < n_cs; ++x) {
            int c = cliente_idx(gen);
            bool ya = false;
            for (int existing : datos.paradas[p].clientes_servidos) {
                if (existing == c) { ya = true; break; }
            }
            if (!ya) datos.paradas[p].clientes_servidos.push_back(c);
        }
    }

    // Construir paradas_cercanas (relación inversa).
    for (int p = 0; p < n_paradas; ++p) {
        for (int c : datos.paradas[p].clientes_servidos) {
            datos.clientes[c].paradas_cercanas.push_back(p);
        }
    }

    // Garantía: cada cliente tiene al menos una parada que lo sirve.
    for (int c = 0; c < n_clientes; ++c) {
        if (datos.clientes[c].paradas_cercanas.empty()) {
            int p = uniform_int_distribution<>(0, n_paradas - 1)(gen);
            datos.paradas[p].clientes_servidos.push_back(c);
            datos.clientes[c].paradas_cercanas.push_back(p);
        }
    }

    // Camiones (alternando tipo entre CAMION y FURGONETA).
    // n_palets se asigna por convención (CAMION=6, FURGONETA=3); en el dataset
    // sintético la capacidad está en m³, así que n_palets es informativo y
    // sólo se usa cuando el JSON de salida lo lee el SA-distribución.
    datos.camiones.resize(n_camiones);
    for (int k = 0; k < n_camiones; ++k) {
        datos.camiones[k].tipo = (k % 2 == 0) ? TipoVehiculo::CAMION : TipoVehiculo::FURGONETA;
        datos.camiones[k].capacidad_volumen = cap_camion;
        datos.camiones[k].n_palets = (k % 2 == 0) ? 6 : 3;
        datos.camiones[k].hora_inicio = hora_jornada_ini;
    }

    // Matrices: distancia euclídea entre paradas, tiempo a velocidad constante.
    int M = n_paradas;
    datos.matriz_distancia.assign(M * M, 0.0);
    datos.matriz_tiempo.assign(M * M, 0.0);

    const double vel = 2.0;   // unidades por minuto

    for (int i = 0; i < M; ++i) {
        for (int j = 0; j < M; ++j) {
            if (i == j) continue;
            double dx = datos.paradas[i].pos.x - datos.paradas[j].pos.x;
            double dy = datos.paradas[i].pos.y - datos.paradas[j].pos.y;
            double d = sqrt(dx*dx + dy*dy);
            datos.matriz_distancia[i*M + j] = d;
            datos.matriz_tiempo[i*M + j] = d / vel;
        }
    }

    // Vectores depósito ↔ parada (también euclidianos, simétricos).
    datos.dist_deposito.assign(M, 0.0);
    datos.tiempo_deposito.assign(M, 0.0);
    for (int i = 0; i < M; ++i) {
        double dx = datos.deposito.x - datos.paradas[i].pos.x;
        double dy = datos.deposito.y - datos.paradas[i].pos.y;
        double d = sqrt(dx*dx + dy*dy);
        datos.dist_deposito[i] = d;
        datos.tiempo_deposito[i] = d / vel;
    }

    return datos;
}

// =============================================================================
// Driver de pruebas
// =============================================================================

// Imprime ayuda con los dos modos de uso.
static void print_usage() {
    cerr << "Modo sintetico:\n"
         << "  damm <escenario> [seed] [greedy] [vecino] [output]\n"
         << "    escenario = pequeno | medio | grande\n"
         << "    greedy    = set_cover | cliente   (default: set_cover)\n"
         << "    vecino    = intra | intra_inter | paradas | completo (default: completo)\n\n"
         << "Modo desde JSON real (producido por pipeline/cargar_csv.py):\n"
         << "  damm --from-json <datos.json> [output] [seed] [greedy] [vecino]\n";
}

int main(int argc, char** argv) {
    string scenario, datos_path, output;
    int seed = 42;
    string greedy_kind = "set_cover";
    string vecino_kind = "completo";
    bool from_json = false;

    // Modo --from-json: argv = damm --from-json datos.json [output] [seed] [greedy] [vecino]
    if (argc >= 3 && string(argv[1]) == "--from-json") {
        from_json   = true;
        datos_path  = argv[2];
        output      = (argc > 3) ? argv[3] : "salida.json";
        seed        = (argc > 4) ? atoi(argv[4]) : 42;
        greedy_kind = (argc > 5) ? argv[5] : "set_cover";
        vecino_kind = (argc > 6) ? argv[6] : "completo";
    } else if (argc >= 2 && string(argv[1]) != "--help" && string(argv[1]) != "-h") {
        // Modo sintético (compatible con la firma original).
        scenario    = argv[1];
        seed        = (argc > 2) ? atoi(argv[2]) : 42;
        greedy_kind = (argc > 3) ? argv[3] : "set_cover";
        vecino_kind = (argc > 4) ? argv[4] : "completo";
        output      = (argc > 5) ? argv[5] : "salida.json";
    } else {
        print_usage();
        return (argc > 1) ? 0 : 1;
    }

    DatosProblema datos;
    if (from_json) {
        cout << "=== Carga desde JSON: " << datos_path << " ===\n";
        try {
            if (!cargar_datos_problema(datos_path, datos)) {
                cerr << "Error: no se pudo abrir " << datos_path << "\n";
                return 1;
            }
        } catch (const exception& e) {
            cerr << "Error parseando JSON: " << e.what() << "\n";
            return 1;
        }
    } else {
        // Seleccionar tamaño del dataset sintético.
        if (scenario == "pequeno") {
            datos = generar_dataset(5, 10, 2, 30.0, seed);
        } else if (scenario == "medio") {
            datos = generar_dataset(15, 25, 3, 40.0, seed);
        } else if (scenario == "grande") {
            datos = generar_dataset(30, 50, 5, 50.0, seed);
        } else {
            cerr << "Escenarios validos: pequeno | medio | grande\n";
            return 1;
        }
        cout << "=== Escenario: " << scenario << " (seed=" << seed << ") ===\n";
    }

    cout << "  clientes: " << datos.clientes.size() << "\n";
    cout << "  paradas:  " << datos.paradas.size() << "\n";
    cout << "  camiones: " << datos.camiones.size() << "\n\n";

    PesosCoste pesos;

    // Estado inicial vía greedy.
    EstadoRuta inicial;
    if (greedy_kind == "set_cover") {
        inicial = greedy_set_cover(datos, pesos.minutos_por_volumen);
    } else if (greedy_kind == "cliente") {
        inicial = greedy_por_cliente(datos, pesos.minutos_por_volumen);
    } else {
        cerr << "Greedys validos: set_cover | cliente\n";
        return 1;
    }

    double coste_inicial = calcular_coste(inicial, datos, pesos);
    cout << "Greedy '" << greedy_kind << "':\n";
    cout << "  coste inicial = " << coste_inicial << "\n";
    int paradas_usadas = 0;
    for (const auto& r : inicial.rutas) paradas_usadas += (int)r.paradas.size();
    cout << "  paradas usadas = " << paradas_usadas << "\n\n";

    // SA.
    mt19937 gen(seed);
    ContextoVecino ctx{datos, pesos.minutos_por_volumen, gen};

    auto coste_fn = [&](const EstadoRuta& s) {
        return calcular_coste(s, datos, pesos);
    };

    function<EstadoRuta(const EstadoRuta&)> vecino_fn;
    if (vecino_kind == "intra") {
        vecino_fn = [&](const EstadoRuta& s) { return vecino_solo_intra(s, ctx); };
    } else if (vecino_kind == "intra_inter") {
        vecino_fn = [&](const EstadoRuta& s) { return vecino_intra_inter(s, ctx); };
    } else if (vecino_kind == "paradas") {
        vecino_fn = [&](const EstadoRuta& s) { return vecino_con_paradas(s, ctx); };
    } else if (vecino_kind == "completo") {
        vecino_fn = [&](const EstadoRuta& s) { return vecino_completo(s, ctx); };
    } else {
        cerr << "Vecinos validos: intra | intra_inter | paradas | completo\n";
        return 1;
    }

    cout << "SA con vecino '" << vecino_kind << "':\n";

    auto t_ini = chrono::steady_clock::now();
    EstadoRuta best = simulated_annealing<EstadoRuta>(
        inicial, coste_fn, vecino_fn,
        /*temp_inicial*/ 1000.0,
        /*temp_final*/   0.01,
        /*cooling*/      0.95,
        /*iters_temp*/   100);
    auto t_fin = chrono::steady_clock::now();
    double secs = chrono::duration<double>(t_fin - t_ini).count();

    double coste_final = calcular_coste(best, datos, pesos);
    double mejora = (coste_inicial > 0)
                  ? (coste_inicial - coste_final) / coste_inicial * 100.0 : 0.0;

    cout << "  coste final   = " << coste_final << "\n";
    cout << "  mejora        = " << mejora << "%\n";
    cout << "  tiempo SA     = " << secs << "s\n\n";

    // Cobertura.
    int n_clientes = (int)datos.clientes.size();
    int cubiertos = 0;
    for (int c = 0; c < n_clientes; ++c) {
        if (best.atendido_ruta[c] != -1) ++cubiertos;
    }
    cout << "Cobertura final: " << cubiertos << "/" << n_clientes << " clientes\n\n";

    // Stats por ruta.
    cout << "Rutas:\n";
    for (int k = 0; k < (int)best.rutas.size(); ++k) {
        const auto& r = best.rutas[k];
        cout << "  Camion " << k
             << "  paradas=" << r.paradas.size()
             << "  dist=" << r.total_distancia
             << "  carga_ini=" << r.total_carga_inicial
             << "  pico=" << r.total_pico_volumen
             << "  retraso=" << r.total_retraso
             << "\n";
    }

    // Exportar a JSON para visualizar.
    if (exportar_json(best, datos, coste_final, output)) {
        cout << "\nResultado exportado a: " << output << "\n";
        cout << "Para visualizar:  python viz/visualizar.py " << output << "\n";
    } else {
        cerr << "Error al exportar a " << output << "\n";
        return 1;
    }
    return 0;
}
