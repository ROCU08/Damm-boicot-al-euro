#include "algo/distribucio/heuristica/h_distr.cc"
#include "algo/distribucio/operadores/op_distr.cc"
#include "algo/distribucio/greedy_inicial.cc"
#include "algo/sa/sa.cc"
#include <iostream>
#include <fstream>
#include <chrono>
#include <sstream>
#include <map>

using namespace std;

// ============================================================
// Infraestructura
// ============================================================

static ofstream devnull("/dev/null");

struct Instancia {
    vector<Item> items;
    vector<uint16_t> orden_ruta;
    int n_palets;
};

Instancia generar_instancia(int n_items, int n_clientes, int n_materiales, int n_palets, mt19937& rng) {
    Instancia inst;
    inst.n_palets = n_palets;
    vector<uint16_t> clientes;
    for (int i = 0; i < n_clientes; ++i)
        clientes.push_back((uint16_t)((i + 1) * 100));
    inst.orden_ruta = clientes;
    shuffle(inst.orden_ruta.begin(), inst.orden_ruta.end(), rng);
    for (int i = 0; i < n_items; ++i)
        inst.items.push_back({(uint16_t)((i % n_materiales) + 1),
                              clientes[i % n_clientes],
                              uniform_int_distribution<int>(0, 4)(rng) == 0});
    return inst;
}

// --- Inicializaciones alternativas ---

Solucion greedy_material(const vector<Item>& items, int n_palets) {
    Solucion s(n_palets);
    map<uint16_t, vector<Item>> grupos;
    for (const auto& it : items) grupos[it.material_id].push_back(it);
    int pal = 0, piso = 0, fila = 0, col = 0;
    auto avanzar = [&]() {
        if (++col >= Solucion::COLS) { col = 0; if (++fila >= Solucion::FILAS) { fila = 0; if (++piso >= Solucion::PISOS) { piso = 0; ++pal; } } }
    };
    for (auto& e : grupos) {
        sort(e.second.begin(), e.second.end(), [](const Item& a, const Item& b) { return a.es_barril < b.es_barril; });
        for (const auto& it : e.second) {
            if (pal >= n_palets) break;
            s.colocar({(uint8_t)pal, (uint8_t)piso, (uint8_t)fila, (uint8_t)col}, it);
            avanzar();
        }
    }
    return s;
}

Solucion random_valida(const vector<Item>& items, int n_palets, mt19937& rng) {
    Solucion s(n_palets);
    vector<Item> v = items;
    shuffle(v.begin(), v.end(), rng);
    int pal = 0, piso = 0, fila = 0, col = 0;
    auto avanzar = [&]() {
        if (++col >= Solucion::COLS) { col = 0; if (++fila >= Solucion::FILAS) { fila = 0; if (++piso >= Solucion::PISOS) { piso = 0; ++pal; } } }
    };
    for (const auto& it : v) {
        if (pal >= n_palets) break;
        s.colocar({(uint8_t)pal, (uint8_t)piso, (uint8_t)fila, (uint8_t)col}, it);
        avanzar();
    }
    return s;
}

// --- Fábrica de operadores ---

using VecinoFn = function<Solucion(const Solucion&, mt19937&)>;

VecinoFn crear_vecino(int prob_swap_pct) {
    return [prob_swap_pct](const Solucion& s, mt19937& r) -> Solucion {
        for (int i = 0; i < 100; ++i) {
            Solucion c = (uniform_int_distribution<int>(1, 100)(r) <= prob_swap_pct)
                         ? vecino_swap(s, r) : vecino_mover(s, r);
            if (es_valida(c)) return c;
        }
        return s;
    };
}

// --- Runner ---

void csv_header() {
    cout << "experimento,config,seed,fitness_ini,fitness_fin,f1,f2,f3,valida,tiempo_ms" << endl;
}

void run(const string& exp, const string& cfg, int seed,
         const Solucion& ini, const vector<uint16_t>& ruta,
         ParametrosSA params, VecinoFn vecino,
         function<double(const Solucion&)> costo) {
    mt19937 rng(seed);
    double fi = costo(ini);

    auto ob = cerr.rdbuf(devnull.rdbuf());
    auto t0 = chrono::high_resolution_clock::now();
    Solucion m = simulated_annealing<Solucion>(ini, costo, vecino, params, rng);
    auto t1 = chrono::high_resolution_clock::now();
    cerr.rdbuf(ob);

    double ff = costo(m);
    cout << exp << "," << cfg << "," << seed << "," << fi << "," << ff << ","
         << fragmentacion_producto(m) << "," << fragmentacion_cliente(m) << ","
         << accesibilidad(m, ruta) << "," << es_valida(m) << ","
         << chrono::duration<double,milli>(t1-t0).count() << "\n";
}

ParametrosSA params_base() { return {100.0, 0.01, 0.995, 200}; }

Instancia inst_base() { mt19937 r(42); return generar_instancia(24, 3, 5, 3, r); }

// ============================================================
// Experimentos
// ============================================================

void exp1_operadores() {
    cerr << "=== Exp 1: Operadores ===" << endl;
    auto inst = inst_base();
    auto p = params_base();
    auto costo = [&](const Solucion& s) { return fitness(s, inst.orden_ruta); };
    auto ini = greedy_inicial(inst.items, inst.n_palets);

    vector<pair<string,int>> configs = {
        {"swap_only",100}, {"move_only",0}, {"mixed_50",50},
        {"mixed_80s",80}, {"mixed_20s",20}
    };
    for (auto& c : configs)
        for (int s = 0; s < 10; ++s)
            run("exp1", c.first, s, ini, inst.orden_ruta, p, crear_vecino(c.second), costo);
}

// Nota: tras analizar Exp 1, actualizar el operador usado aquí
void exp2_inicializacion() {
    cerr << "=== Exp 2: Inicialización ===" << endl;
    auto inst = inst_base();
    auto p = params_base();
    auto costo = [&](const Solucion& s) { return fitness(s, inst.orden_ruta); };
    auto vec = crear_vecino(80);

    auto ini_c = greedy_inicial(inst.items, inst.n_palets);
    auto ini_m = greedy_material(inst.items, inst.n_palets);
    for (int s = 0; s < 10; ++s) {
        run("exp2", "greedy_cliente", s, ini_c, inst.orden_ruta, p, vec, costo);
        run("exp2", "greedy_material", s, ini_m, inst.orden_ruta, p, vec, costo);
        mt19937 ir(s + 1000);
        run("exp2", "random", s, random_valida(inst.items, inst.n_palets, ir),
            inst.orden_ruta, p, vec, costo);
    }
}

// Nota: tras analizar Exp 1+2, actualizar operador e inicialización
void exp3_grid_search() {
    cerr << "=== Exp 3: Grid Search SA ===" << endl;
    auto inst = inst_base();
    auto costo = [&](const Solucion& s) { return fitness(s, inst.orden_ruta); };
    auto ini = greedy_inicial(inst.items, inst.n_palets);
    auto vec = crear_vecino(80);

    double T_inits[] = {1, 10, 100};
    double coolings[] = {0.9, 0.95, 0.99, 0.995};
    int iters[] = {10, 50, 200};

    int total = 3 * 4 * 3;
    int actual = 0;
    for (double ti : T_inits)
        for (double cr : coolings)
            for (int it : iters) {
                ++actual;
                cerr << "  Grid " << actual << "/" << total << "\r" << flush;
                ostringstream cfg;
                cfg << "T" << ti << "_c" << cr << "_i" << it;
                for (int s = 0; s < 100; ++s)
                    run("exp3", cfg.str(), s, ini, inst.orden_ruta,
                        {ti, 0.01, cr, it}, vec, costo);
            }
    cerr << endl;
}

// V1 = n_items, V2 = n_palets, ratio fijo ~8 items/palet
void exp4_escalabilidad_proporcional() {
    cerr << "=== Exp 4: Escalabilidad proporcional ===" << endl;
    auto p = params_base();
    auto vec = crear_vecino(80);

    int cfgs[][3] = {{16,2,2}, {24,3,3}, {40,5,4}, {48,6,5}, {64,8,6}};
    for (auto& c : cfgs) {
        mt19937 ir(42);
        auto inst = generar_instancia(c[0], c[2], 5, c[1], ir);
        auto costo = [&](const Solucion& s) { return fitness(s, inst.orden_ruta); };
        auto ini = greedy_inicial(inst.items, inst.n_palets);
        ostringstream cfg;
        cfg << "i" << c[0] << "_p" << c[1];
        cerr << "  " << cfg.str() << endl;
        for (int s = 0; s < 10; ++s)
            run("exp4", cfg.str(), s, ini, inst.orden_ruta, p, vec, costo);
    }
}

void exp5_escalabilidad_separada() {
    cerr << "=== Exp 5: Escalabilidad separada ===" << endl;
    auto p = params_base();
    auto vec = crear_vecino(50);

    // 5a: fija n_palets=3, varía n_items
    cerr << "  5a: n_palets=3, varía n_items" << endl;
    for (int ni : {10, 20, 30, 40, 50}) {
        mt19937 ir(42);
        auto inst = generar_instancia(ni, 3, 5, 3, ir);
        auto costo = [&](const Solucion& s) { return fitness(s, inst.orden_ruta); };
        auto ini = greedy_inicial(inst.items, inst.n_palets);
        ostringstream cfg; cfg << "items" << ni;
        for (int s = 0; s < 10; ++s)
            run("exp5a", cfg.str(), s, ini, inst.orden_ruta, p, vec, costo);
    }

    // 5b: fija n_items=24, varía n_palets
    cerr << "  5b: n_items=24, varía n_palets" << endl;
    for (int np : {2, 3, 4, 5, 6, 7, 8}) {
        mt19937 ir(42);
        auto inst = generar_instancia(24, 3, 5, np, ir);
        auto costo = [&](const Solucion& s) { return fitness(s, inst.orden_ruta); };
        auto ini = greedy_inicial(inst.items, inst.n_palets);
        ostringstream cfg; cfg << "pal" << np;
        for (int s = 0; s < 10; ++s)
            run("exp5b", cfg.str(), s, ini, inst.orden_ruta, p, vec, costo);
    }
}

// Densidad fija ~40%, varía n_palets (cambia tipo de vehículo)
// <=3: furgoneta | 4-6: camión 6 | >=7: camión 8
void exp6_restriccion_dominio() {
    cerr << "=== Exp 6: Restricción de dominio (tipo vehículo) ===" << endl;
    auto p = params_base();
    auto vec = crear_vecino(50);

    for (int np : {2, 3, 4, 5, 6, 7, 8}) {
        int ni = np * 24;
        int nc = (np < 2) ? 2 : np;
        mt19937 ir(42);
        auto inst = generar_instancia(ni, nc, 5, np, ir);
        auto costo = [&](const Solucion& s) { return fitness(s, inst.orden_ruta); };
        auto ini = greedy_inicial(inst.items, inst.n_palets);
        string tipo = np <= 3 ? "furgoneta" : (np <= 6 ? "camion6" : "camion8");
        ostringstream cfg; cfg << tipo << "_p" << np;
        cerr << "  " << cfg.str() << " (" << ni << " items)" << endl;
        for (int s = 0; s < 10; ++s)
            run("exp6", cfg.str(), s, ini, inst.orden_ruta, p, vec, costo);
    }
}

// H = fragmentacion_cliente (H1) + w * accesibilidad (H2)
void exp7_heuristicas_combinadas() {
    cerr << "=== Exp 7: Heurísticas combinadas ===" << endl;
    auto inst = inst_base();
    auto p = params_base();
    auto vec = crear_vecino(50);
    auto ini = greedy_inicial(inst.items, inst.n_palets);

    for (double w : {0.0, 1.0, 2.0, 4.0, 8.0, 16.0}) {
        auto costo = [&, w](const Solucion& s) -> double {
            return fragmentacion_cliente(s) + w * accesibilidad(s, inst.orden_ruta);
        };
        ostringstream cfg; cfg << "w" << w;
        cerr << "  " << cfg.str() << endl;
        for (int s = 0; s < 10; ++s)
            run("exp7", cfg.str(), s, ini, inst.orden_ruta, p, vec, costo);
    }
}

// ============================================================

int main(int argc, char* argv[]) {
    string e = argc > 1 ? argv[1] : "all";
    csv_header();

    if (e == "1" || e == "all") exp1_operadores();
    if (e == "2" || e == "all") exp2_inicializacion();
    if (e == "3" || e == "all") exp3_grid_search();
    if (e == "4" || e == "all") exp4_escalabilidad_proporcional();
    if (e == "5" || e == "all") exp5_escalabilidad_separada();
    if (e == "6" || e == "all") exp6_restriccion_dominio();
    if (e == "7" || e == "all") exp7_heuristicas_combinadas();

    if (e != "all" && (e < "1" || e > "7")) {
        cerr << "Uso: " << argv[0] << " [1|2|3|4|5|6|7|all]" << endl;
        return 1;
    }
    return 0;
}
