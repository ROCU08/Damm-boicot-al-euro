/**
 * experimentos.cc
 *
 * Incluir DESPUÉS de todos los headers del proyecto y de sa.cc.
 * Compilar junto con main.cc (o sustituirlo).
 *
 * Cada experimento expone una función:
 *   void exp1_operadores();
 *   void exp2_inicializacion();
 *   void exp3_grid_search();
 *   void exp4_escalabilidad_proporcional();
 *   void exp5_escalabilidad_separada();
 *   void exp6_restriccion_dominio();
 *   void exp7_heuristicas_combinadas();
 *
 * Al final, main() orquesta todos los experimentos.
 */

#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <random>
#include <cmath>

#include "algo/ruta/estado_ruta.h"
#include "algo/ruta/heuristica/h_ruta.h"
#include "algo/ruta/operadores/op_ruta.h"
#include "algo/ruta/vecino/vecino.h"
#include "algo/ruta/greedy/greedy.h"
#include "io/exportar.h"
#include "algo/sa/sa.cc"

using namespace std;
using Clock = chrono::steady_clock;

// =============================================================================
// Semillas
// =============================================================================

// 10 semillas para experimentos 1, 2, 4, 5, 6, 7 (rápidos)
static const vector<int> SEEDS = {
    42, 137, 256, 512, 999,
    1234, 2048, 3141, 7777, 9999
};

// 100 semillas para el grid search (exp 3)
static vector<int> make_seeds2() {
    vector<int> s(100);
    iota(s.begin(), s.end(), 0);          // 0, 1, ..., 99
    for (auto& v : s) v = v * 31 + 17;   // dispersión trivial
    return s;
}
static const vector<int> SEEDS2 = make_seeds2();

// =============================================================================
// Parámetros SA base (fijos en Exp 1 y 2)
// =============================================================================

struct SAParams {
    double temp_ini  = 1000.0;
    double temp_fin  =    0.01;
    double cooling   =    0.95;
    int    iters_temp =   100;
};

static const SAParams BASE_PARAMS;

// =============================================================================
// Helpers
// =============================================================================

// Genera el dataset sintético (idéntico a main.cc).
DatosProblema generar_dataset(int n_clientes, int n_paradas, int n_camiones,
                              double cap_camion, int seed)
{
    mt19937 gen(seed);
    DatosProblema datos;
    datos.deposito = {50.0, 50.0};

    uniform_real_distribution<> coord(0.0, 100.0);
    uniform_real_distribution<> vol_recoger(2.0, 8.0);
    uniform_real_distribution<> vol_devolver(0.0, 3.0);
    uniform_int_distribution<> num_cs(1, 3);

    const int hora_ini = 8 * 60;
    const int hora_fin = 18 * 60;

    datos.clientes.resize(n_clientes);
    for (int c = 0; c < n_clientes; ++c) {
        int h_ini = uniform_int_distribution<>(hora_ini, hora_ini + 360)(gen);
        int h_fin = h_ini + uniform_int_distribution<>(120, 300)(gen);
        if (h_fin > hora_fin) h_fin = hora_fin;
        datos.clientes[c].id = c;
        datos.clientes[c].hora_ini = h_ini;
        datos.clientes[c].hora_fin = h_fin;
        datos.clientes[c].volumen_recoger  = vol_recoger(gen);
        datos.clientes[c].volumen_devolver = vol_devolver(gen);
    }

    datos.paradas.resize(n_paradas);
    for (int p = 0; p < n_paradas; ++p) {
        datos.paradas[p].id = p;
        datos.paradas[p].pos.x = coord(gen);
        datos.paradas[p].pos.y = coord(gen);
    }

    uniform_int_distribution<> ci(0, n_clientes - 1);
    for (int p = 0; p < n_paradas; ++p) {
        int n_cs = num_cs(gen);
        for (int x = 0; x < n_cs; ++x) {
            int c = ci(gen);
            bool ya = false;
            for (int e : datos.paradas[p].clientes_servidos)
                if (e == c) { ya = true; break; }
            if (!ya) datos.paradas[p].clientes_servidos.push_back(c);
        }
    }
    for (int p = 0; p < n_paradas; ++p)
        for (int c : datos.paradas[p].clientes_servidos)
            datos.clientes[c].paradas_cercanas.push_back(p);

    for (int c = 0; c < n_clientes; ++c) {
        if (datos.clientes[c].paradas_cercanas.empty()) {
            int p = uniform_int_distribution<>(0, n_paradas - 1)(gen);
            datos.paradas[p].clientes_servidos.push_back(c);
            datos.clientes[c].paradas_cercanas.push_back(p);
        }
    }

    datos.camiones.resize(n_camiones);
    for (int k = 0; k < n_camiones; ++k) {
        datos.camiones[k].tipo = (k % 2 == 0) ? TipoVehiculo::CAMION : TipoVehiculo::FURGONETA;
        datos.camiones[k].capacidad_volumen = cap_camion;
        datos.camiones[k].hora_inicio = hora_ini;
    }

    int M = n_paradas;
    datos.matriz_distancia.assign(M * M, 0.0);
    datos.matriz_tiempo.assign(M * M, 0.0);
    const double vel = 2.0;
    for (int i = 0; i < M; ++i)
        for (int j = 0; j < M; ++j) {
            if (i == j) continue;
            double dx = datos.paradas[i].pos.x - datos.paradas[j].pos.x;
            double dy = datos.paradas[i].pos.y - datos.paradas[j].pos.y;
            double d  = sqrt(dx*dx + dy*dy);
            datos.matriz_distancia[i*M+j] = d;
            datos.matriz_tiempo[i*M+j]    = d / vel;
        }

    datos.dist_deposito.assign(M, 0.0);
    datos.tiempo_deposito.assign(M, 0.0);
    for (int i = 0; i < M; ++i) {
        double dx = datos.deposito.x - datos.paradas[i].pos.x;
        double dy = datos.deposito.y - datos.paradas[i].pos.y;
        double d  = sqrt(dx*dx + dy*dy);
        datos.dist_deposito[i]  = d;
        datos.tiempo_deposito[i] = d / vel;
    }
    return datos;
}

// ----------------------------------------------------------------------------
// Resultado de una ejecución SA
// ----------------------------------------------------------------------------
struct RunResult {
    double coste_inicial;
    double coste_fin;
    double mejora_pct;    // (ini - fin) / ini * 100
    double tiempo_s;
    int    clientes_cubiertos;
    int    n_clientes;
};

// Ejecuta greedy + SA con parámetros dados y devuelve métricas.
RunResult ejecutar_sa(
    const DatosProblema& datos,
    const PesosCoste&    pesos,
    const string&        greedy_kind,
    function<EstadoRuta(const EstadoRuta&)> vecino_fn,
    const SAParams& p,
    int seed)
{
    EstadoRuta inicial;
    if (greedy_kind == "set_cover")
        inicial = greedy_set_cover(datos, pesos.minutos_por_volumen);
    else
        inicial = greedy_por_cliente(datos, pesos.minutos_por_volumen);

    double coste_ini = calcular_coste(inicial, datos, pesos);

    auto coste_fn = [&](const EstadoRuta& s) {
        return calcular_coste(s, datos, pesos);
    };

    auto t0 = Clock::now();
    EstadoRuta best = simulated_annealing<EstadoRuta>(
        inicial, coste_fn, vecino_fn,
        p.temp_ini, p.temp_fin, p.cooling, p.iters_temp);
    double secs = chrono::duration<double>(Clock::now() - t0).count();

    double coste_fin = calcular_coste(best, datos, pesos);
    int nc = (int)datos.clientes.size();
    int cubiertos = 0;
    for (int c = 0; c < nc; ++c)
        if (best.atendido_ruta[c] != -1) ++cubiertos;

    double mejora = (coste_ini > 1e-9)
                  ? (coste_ini - coste_fin) / coste_ini * 100.0 : 0.0;

    return {coste_ini, coste_fin, mejora, secs, cubiertos, nc};
}

// Calcula media y desv. estándar muestral de un vector.
pair<double,double> stats(const vector<double>& v) {
    double mean = accumulate(v.begin(), v.end(), 0.0) / v.size();
    double var  = 0.0;
    for (double x : v) var += (x - mean) * (x - mean);
    var /= (v.size() > 1 ? v.size() - 1 : 1);
    return {mean, sqrt(var)};
}

// Imprime una línea CSV (sep = ',').
void csv_line(ofstream& f, const vector<string>& cols) {
    for (size_t i = 0; i < cols.size(); ++i) {
        if (i) f << ',';
        f << cols[i];
    }
    f << '\n';
}

// =============================================================================
// Exp 1 – Operadores
// =============================================================================
/**
 * Compara los cuatro vecindarios disponibles ejecutando SA con parámetros
 * base sobre el escenario "medio" y las SEEDS.
 * Genera experimento1_operadores.csv con columnas:
 *   operador, seed, coste_ini, coste_fin, mejora_pct, tiempo_s, cobertura_pct
 */
void exp1_operadores() {
    cout << "\n========== EXP 1: Operadores ==========\n";

    const int n_clientes = 15, n_paradas = 25, n_camiones = 3;
    const double cap = 40.0;
    PesosCoste pesos;

    // Definición de los vecindarios
    vector<pair<string, function<EstadoRuta(const DatosProblema&, mt19937&, const EstadoRuta&)>>>
    vecinos = {
        {"intra",       [](const DatosProblema& d, mt19937& g, const EstadoRuta& s) {
            ContextoVecino ctx{d, PesosCoste().minutos_por_volumen, g};
            return vecino_solo_intra(s, ctx); }},
        {"intra_inter", [](const DatosProblema& d, mt19937& g, const EstadoRuta& s) {
            ContextoVecino ctx{d, PesosCoste().minutos_por_volumen, g};
            return vecino_intra_inter(s, ctx); }},
        {"paradas",     [](const DatosProblema& d, mt19937& g, const EstadoRuta& s) {
            ContextoVecino ctx{d, PesosCoste().minutos_por_volumen, g};
            return vecino_con_paradas(s, ctx); }},
        {"completo",    [](const DatosProblema& d, mt19937& g, const EstadoRuta& s) {
            ContextoVecino ctx{d, PesosCoste().minutos_por_volumen, g};
            return vecino_completo(s, ctx); }},
    };

    ofstream out("experimento1_operadores.csv");
    csv_line(out, {"operador","seed","coste_ini","coste_fin","mejora_pct","tiempo_s","cobertura_pct"});

    // Acumuladores para resumen por operador
    map<string, vector<double>> mejoras, tiempos, coberturas;

    for (auto& [nombre, vfn] : vecinos) {
        for (int seed : SEEDS) {
            DatosProblema datos = generar_dataset(n_clientes, n_paradas, n_camiones, cap, seed);
            mt19937 gen(seed);

            auto vfn_bound = [&](const EstadoRuta& s) { return vfn(datos, gen, s); };
            RunResult r = ejecutar_sa(datos, pesos, "set_cover", vfn_bound, BASE_PARAMS, seed);

            double cob_pct = 100.0 * r.clientes_cubiertos / r.n_clientes;
            csv_line(out, {
                nombre,
                to_string(seed),
                to_string(r.coste_inicial),
                to_string(r.coste_fin),
                to_string(r.mejora_pct),
                to_string(r.tiempo_s),
                to_string(cob_pct)
            });

            mejoras[nombre].push_back(r.mejora_pct);
            tiempos[nombre].push_back(r.tiempo_s);
            coberturas[nombre].push_back(cob_pct);

            cout << "  [" << nombre << "] seed=" << seed
                 << "  mejora=" << fixed << setprecision(2) << r.mejora_pct << "%"
                 << "  t=" << r.tiempo_s << "s\n";
        }
    }

    // Resumen
    cout << "\n-- Resumen (media ± std) --\n";
    cout << left << setw(14) << "operador"
         << right << setw(14) << "mejora(%)"
         << setw(10) << "std"
         << setw(14) << "tiempo(s)"
         << setw(10) << "std"
         << setw(14) << "cob(%)"
         << "\n";
    string mejor_operador;
    double mejor_mejora = -1e9;
    for (auto& [nombre, mv] : mejoras) {
        auto [mm, sm] = stats(mv);
        auto [mt_, st] = stats(tiempos[nombre]);
        auto [mc, sc]  = stats(coberturas[nombre]);
        cout << left  << setw(14) << nombre
             << right << setw(14) << fixed << setprecision(2) << mm
             << setw(10) << sm
             << setw(14) << mt_
             << setw(10) << st
             << setw(14) << mc
             << "\n";
        if (mm > mejor_mejora) { mejor_mejora = mm; mejor_operador = nombre; }
    }
    cout << "\n=> Mejor operador: " << mejor_operador
         << " (media mejora=" << mejor_mejora << "%)\n";
    cout << "   Exportado a experimento1_operadores.csv\n";
}

// =============================================================================
// Exp 2 – Inicialización
// =============================================================================
/**
 * Fija el mejor operador de Exp 1 y compara las dos estrategias greedy.
 * Mismo escenario y parámetros base.
 * Genera experimento2_inicializacion.csv.
 */
void exp2_inicializacion(const string& mejor_operador = "completo") {
    cout << "\n========== EXP 2: Inicialización ==========\n";
    cout << "   Usando operador: " << mejor_operador << "\n";

    const int n_clientes = 15, n_paradas = 25, n_camiones = 3;
    const double cap = 40.0;
    PesosCoste pesos;

    vector<string> greedys = {"set_cover", "cliente"};

    ofstream out("experimento2_inicializacion.csv");
    csv_line(out, {"greedy","seed","coste_ini","coste_fin","mejora_pct","tiempo_s","cobertura_pct"});

    map<string, vector<double>> mejoras, tiempos, coberturas;

    for (const string& gk : greedys) {
        for (int seed : SEEDS) {
            DatosProblema datos = generar_dataset(n_clientes, n_paradas, n_camiones, cap, seed);
            mt19937 gen(seed);

            // Selecciona el vecino por nombre
            function<EstadoRuta(const EstadoRuta&)> vfn;
            auto make_ctx = [&]() -> ContextoVecino { return {datos, pesos.minutos_por_volumen, gen}; };
            if (mejor_operador == "intra")
                vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_solo_intra(s, c); };
            else if (mejor_operador == "intra_inter")
                vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_intra_inter(s, c); };
            else if (mejor_operador == "paradas")
                vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_con_paradas(s, c); };
            else
                vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_completo(s, c); };

            RunResult r = ejecutar_sa(datos, pesos, gk, vfn, BASE_PARAMS, seed);
            double cob_pct = 100.0 * r.clientes_cubiertos / r.n_clientes;

            csv_line(out, {
                gk, to_string(seed),
                to_string(r.coste_inicial), to_string(r.coste_fin),
                to_string(r.mejora_pct),    to_string(r.tiempo_s),
                to_string(cob_pct)
            });

            mejoras[gk].push_back(r.mejora_pct);
            tiempos[gk].push_back(r.tiempo_s);
            coberturas[gk].push_back(cob_pct);

            cout << "  [" << gk << "] seed=" << seed
                 << "  coste_ini=" << fixed << setprecision(1) << r.coste_inicial
                 << "  mejora=" << r.mejora_pct << "%\n";
        }
    }

    cout << "\n-- Resumen (media ± std) --\n";
    cout << left  << setw(14) << "greedy"
         << right << setw(14) << "coste_ini"
         << setw(14) << "mejora(%)"
         << setw(10) << "std"
         << setw(14) << "cob(%)"
         << "\n";
    string mejor_greedy;
    for (const string& gk : greedys) {
        auto [mm, sm] = stats(mejoras[gk]);
        auto [mc, sc] = stats(coberturas[gk]);
        (void)sc;   // sc no se usa, se descarta el unused-warning
        // Usamos mejora media para seleccionar
        if (mm > (mejor_greedy.empty() ? -1e9 : stats(mejoras[mejor_greedy]).first))
            mejor_greedy = gk;
        cout << left  << setw(14) << gk
             << right << setw(14) << fixed << setprecision(2) << mm
             << setw(10) << sm
             << setw(14) << mc
             << "\n";
    }
    cout << "\n=> Mejor inicialización: " << mejor_greedy << "\n";
    cout << "   Exportado a experimento2_inicializacion.csv\n";
}

// =============================================================================
// Exp 3 – Grid Search SA
// =============================================================================
/**
 * Explora combinaciones (temp_ini k, lambda cooling, steps iters_temp)
 * con 100 semillas cada una.
 *
 * Grid definido en arrays abajo. Ajusta el tamaño para no tardar siglos.
 * Genera experimento3_grid_search.csv y al final imprime el top-5.
 */
void exp3_grid_search(const string& mejor_operador = "completo",
                      const string& mejor_greedy   = "cliente")
{
    cout << "\n========== EXP 3: Grid Search SA ==========\n";
    cout << "   operador=" << mejor_operador << "  greedy=" << mejor_greedy << "\n";

    const int n_clientes = 15, n_paradas = 25, n_camiones = 3;
    const double cap = 40.0;
    PesosCoste pesos;

    // Grid de hiperparámetros
    vector<double> v_temp_ini  = {500.0, 1000.0, 2000.0};
    vector<double> v_cooling   = {0.90, 0.95, 0.99};
    vector<int>    v_iters_temp = {50, 100, 200};
    // temp_fin fija; k (temp_ini) y lambda (cooling) determinan el descenso
    const double TEMP_FIN = 0.01;

    ofstream out("experimento3_grid_search.csv");
    csv_line(out, {"temp_ini","cooling","iters_temp",
                   "media_mejora","std_mejora",
                   "media_coste_fin","std_coste_fin",
                   "media_tiempo","std_tiempo",
                   "media_cobertura"});

    struct GridResult {
        double temp_ini, cooling;
        int    iters_temp;
        double media_mejora, media_coste_fin, media_cob;
    };
    vector<GridResult> resultados;

    int total = (int)(v_temp_ini.size() * v_cooling.size() * v_iters_temp.size());
    int run = 0;

    for (double t0 : v_temp_ini)
    for (double cool : v_cooling)
    for (int iters : v_iters_temp)
    {
        ++run;
        SAParams p;
        p.temp_ini   = t0;
        p.temp_fin   = TEMP_FIN;
        p.cooling    = cool;
        p.iters_temp = iters;

        vector<double> mejoras, costes_fin, tiempos, coberturas;

        for (int seed : SEEDS2) {
            DatosProblema datos = generar_dataset(n_clientes, n_paradas, n_camiones, cap, seed);
            mt19937 gen(seed);

            function<EstadoRuta(const EstadoRuta&)> vfn;
            auto make_ctx = [&]() -> ContextoVecino { return {datos, pesos.minutos_por_volumen, gen}; };
            if (mejor_operador == "intra")
                vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_solo_intra(s, c); };
            else if (mejor_operador == "intra_inter")
                vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_intra_inter(s, c); };
            else if (mejor_operador == "paradas")
                vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_con_paradas(s, c); };
            else
                vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_completo(s, c); };

            RunResult r = ejecutar_sa(datos, pesos, mejor_greedy, vfn, p, seed);
            mejoras.push_back(r.mejora_pct);
            costes_fin.push_back(r.coste_fin);
            tiempos.push_back(r.tiempo_s);
            coberturas.push_back(100.0 * r.clientes_cubiertos / r.n_clientes);
        }

        auto [mm, sm]   = stats(mejoras);
        auto [mc, sc]   = stats(costes_fin);
        auto [mt_, st_] = stats(tiempos);
        auto [mcob, _]  = stats(coberturas);

        csv_line(out, {
            to_string(t0), to_string(cool), to_string(iters),
            to_string(mm), to_string(sm),
            to_string(mc), to_string(sc),
            to_string(mt_), to_string(st_),
            to_string(mcob)
        });

        resultados.push_back({t0, cool, iters, mm, mc, mcob});
        cout << "  (" << run << "/" << total << ")"
             << " T0=" << t0 << " cool=" << cool << " iters=" << iters
             << "  media_mejora=" << fixed << setprecision(2) << mm << "%"
             << "  media_coste=" << mc << "\n";
    }

    // Top-5 por mayor mejora media
    sort(resultados.begin(), resultados.end(),
         [](const GridResult& a, const GridResult& b){ return a.media_mejora > b.media_mejora; });

    cout << "\n-- Top 5 configuraciones (mayor mejora media) --\n";
    cout << left << setw(8) << "rank"
         << setw(10) << "T0"
         << setw(10) << "cooling"
         << setw(10) << "iters"
         << right << setw(14) << "mejora(%)"
         << setw(14) << "coste_fin"
         << setw(12) << "cob(%)"
         << "\n";
    for (int i = 0; i < min(5, (int)resultados.size()); ++i) {
        const auto& gr = resultados[i];
        cout << left << setw(8) << (i+1)
             << setw(10) << gr.temp_ini
             << setw(10) << gr.cooling
             << setw(10) << gr.iters_temp
             << right << setw(14) << fixed << setprecision(2) << gr.media_mejora
             << setw(14) << gr.media_coste_fin
             << setw(12) << gr.media_cob
             << "\n";
    }
    cout << "   Exportado a experimento3_grid_search.csv\n";
}

// =============================================================================
// Exp 4 – Escalabilidad Proporcional
// =============================================================================
/**
 * Aumenta clientes y paradas de forma proporcional (ratio paradas/clientes = 2).
 * Usa los parámetros óptimos del Exp 3.
 * Genera experimento4_escalabilidad_proporcional.csv.
 */
void exp4_escalabilidad_proporcional(
    const string&  mejor_operador = "completo",
    const string&  mejor_greedy   = "set_cover",
    const SAParams& opt_params    = BASE_PARAMS)
{
    cout << "\n========== EXP 4: Escalabilidad Proporcional ==========\n";

    // Tamaños: (n_clientes, n_paradas, n_camiones)
    // Ratio paradas/clientes = 2 (fijo), camiones ≈ sqrt(clientes)
    vector<tuple<int,int,int>> tamanios = {
        { 5,  10, 2},
        {10,  20, 3},
        {15,  30, 3},
        {20,  40, 4},
        {30,  60, 5},
        {50, 100, 6},
    };
    const double cap = 40.0;
    PesosCoste pesos;

    ofstream out("experimento4_escalabilidad_proporcional.csv");
    csv_line(out, {"n_clientes","n_paradas","n_camiones",
                   "media_coste_ini","media_coste_fin","media_mejora",
                   "media_tiempo_s","std_tiempo_s","media_cobertura"});

    for (auto [nc, np, nk] : tamanios) {
        vector<double> mejoras, costes_fin, tiempos, coberturas;

        for (int seed : SEEDS) {
            DatosProblema datos = generar_dataset(nc, np, nk, cap, seed);
            mt19937 gen(seed);

            function<EstadoRuta(const EstadoRuta&)> vfn;
            auto make_ctx = [&]() -> ContextoVecino { return {datos, pesos.minutos_por_volumen, gen}; };
            if (mejor_operador == "intra")
                vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_solo_intra(s, c); };
            else if (mejor_operador == "intra_inter")
                vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_intra_inter(s, c); };
            else if (mejor_operador == "paradas")
                vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_con_paradas(s, c); };
            else
                vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_completo(s, c); };

            RunResult r = ejecutar_sa(datos, pesos, mejor_greedy, vfn, opt_params, seed);
            mejoras.push_back(r.mejora_pct);
            costes_fin.push_back(r.coste_fin);
            tiempos.push_back(r.tiempo_s);
            coberturas.push_back(100.0 * r.clientes_cubiertos / r.n_clientes);
        }

        auto [mm, sm]   = stats(mejoras);
        auto [mc, sc]   = stats(costes_fin);
        auto [mt_, st_] = stats(tiempos);
        auto [mcob, _]  = stats(coberturas);
        // coste_ini media (aproximada desde los datos ya procesados)
        double media_ini = mc / (1.0 - mm / 100.0);   // reconstruida

        csv_line(out, {
            to_string(nc), to_string(np), to_string(nk),
            to_string(media_ini), to_string(mc), to_string(mm),
            to_string(mt_), to_string(st_), to_string(mcob)
        });

        cout << "  nc=" << nc << " np=" << np << " nk=" << nk
             << "  media_t=" << fixed << setprecision(3) << mt_ << "s"
             << "  mejora=" << setprecision(2) << mm << "%"
             << "  cob=" << mcob << "%\n";
    }
    cout << "   Exportado a experimento4_escalabilidad_proporcional.csv\n";
}

// =============================================================================
// Exp 5 – Escalabilidad Separada (5a y 5b)
// =============================================================================
/**
 * 5a: fija n_clientes, varía n_paradas.
 * 5b: fija n_paradas, varía n_clientes.
 * Genera experimento5a_clientes_fijos.csv y experimento5b_paradas_fijas.csv.
 */
void exp5_escalabilidad_separada(
    const string&  mejor_operador = "completo",
    const string&  mejor_greedy   = "set_cover",
    const SAParams& opt_params    = BASE_PARAMS)
{
    cout << "\n========== EXP 5a: Clientes fijos, paradas variables ==========\n";

    PesosCoste pesos;
    const int  N_CLIENTES_FIJO = 15;
    const int  N_PARADAS_FIJO  = 25;
    const int  N_CAMIONES      = 3;
    const double cap = 40.0;

    auto run_batch = [&](const vector<tuple<int,int,int>>& configs,
                         const string& label,
                         const string& filename) {
        ofstream out(filename);
        csv_line(out, {"n_clientes","n_paradas","n_camiones",
                       "media_coste_fin","media_mejora",
                       "media_tiempo_s","std_tiempo_s","media_cobertura"});

        for (auto [nc, np, nk] : configs) {
            vector<double> mejoras, costes_fin, tiempos, coberturas;

            for (int seed : SEEDS) {
                DatosProblema datos = generar_dataset(nc, np, nk, cap, seed);
                mt19937 gen(seed);

                function<EstadoRuta(const EstadoRuta&)> vfn;
                auto make_ctx = [&]() -> ContextoVecino {
                    return {datos, pesos.minutos_por_volumen, gen};
                };
                if (mejor_operador == "intra")
                    vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_solo_intra(s, c); };
                else if (mejor_operador == "intra_inter")
                    vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_intra_inter(s, c); };
                else if (mejor_operador == "paradas")
                    vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_con_paradas(s, c); };
                else
                    vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_completo(s, c); };

                RunResult r = ejecutar_sa(datos, pesos, mejor_greedy, vfn, opt_params, seed);
                mejoras.push_back(r.mejora_pct);
                costes_fin.push_back(r.coste_fin);
                tiempos.push_back(r.tiempo_s);
                coberturas.push_back(100.0 * r.clientes_cubiertos / r.n_clientes);
            }

            auto [mm, sm]   = stats(mejoras);
            auto [mc, sc]   = stats(costes_fin);
            auto [mt_, st_] = stats(tiempos);
            auto [mcob, _]  = stats(coberturas);

            csv_line(out, {
                to_string(nc), to_string(np), to_string(nk),
                to_string(mc), to_string(mm),
                to_string(mt_), to_string(st_), to_string(mcob)
            });

            cout << "  [" << label << "] nc=" << nc << " np=" << np
                 << "  t=" << fixed << setprecision(3) << mt_ << "s"
                 << "  mejora=" << setprecision(2) << mm << "%\n";
        }
        cout << "   Exportado a " << filename << "\n";
    };

    // 5a: clientes fijos, paradas crecientes
    run_batch({
        {N_CLIENTES_FIJO,  10, N_CAMIONES},
        {N_CLIENTES_FIJO,  20, N_CAMIONES},
        {N_CLIENTES_FIJO,  30, N_CAMIONES},
        {N_CLIENTES_FIJO,  50, N_CAMIONES},
        {N_CLIENTES_FIJO,  80, N_CAMIONES},
        {N_CLIENTES_FIJO, 120, N_CAMIONES},
    }, "5a", "experimento5a_clientes_fijos.csv");

    cout << "\n========== EXP 5b: Paradas fijas, clientes variables ==========\n";

    // 5b: paradas fijas, clientes crecientes
    run_batch({
        { 5, N_PARADAS_FIJO, N_CAMIONES},
        {10, N_PARADAS_FIJO, N_CAMIONES},
        {15, N_PARADAS_FIJO, N_CAMIONES},
        {20, N_PARADAS_FIJO, N_CAMIONES},
        {30, N_PARADAS_FIJO, N_CAMIONES},
        {40, N_PARADAS_FIJO, N_CAMIONES},
    }, "5b", "experimento5b_paradas_fijas.csv");
}

// =============================================================================
// Exp 6 – Restricción de Dominio (capacidad del vehículo)
// =============================================================================
/**
 * Mantiene el tamaño fijo (escenario medio) y varía la capacidad de los
 * vehículos.  A menor capacidad el problema es más difícil (más rutas
 * parciales, más recargas).
 * Genera experimento6_capacidad.csv.
 */
void exp6_restriccion_dominio(
    const string&  mejor_operador = "completo",
    const string&  mejor_greedy   = "set_cover",
    const SAParams& opt_params    = BASE_PARAMS)
{
    cout << "\n========== EXP 6: Restricción de Dominio (capacidad) ==========\n";

    const int n_clientes = 15, n_paradas = 25, n_camiones = 3;
    PesosCoste pesos;

    // Capacidades a explorar
    vector<double> capacidades = {10.0, 20.0, 30.0, 40.0, 60.0, 80.0, 120.0};

    ofstream out("experimento6_capacidad.csv");
    csv_line(out, {"capacidad","media_coste_ini","media_coste_fin",
                   "media_mejora","std_mejora","media_cobertura","media_tiempo_s"});

    for (double cap : capacidades) {
        vector<double> mejoras, costes_ini, costes_fin, coberturas, tiempos;

        for (int seed : SEEDS) {
            DatosProblema datos = generar_dataset(n_clientes, n_paradas, n_camiones, cap, seed);
            mt19937 gen(seed);

            function<EstadoRuta(const EstadoRuta&)> vfn;
            auto make_ctx = [&]() -> ContextoVecino {
                return {datos, pesos.minutos_por_volumen, gen};
            };
            if (mejor_operador == "intra")
                vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_solo_intra(s, c); };
            else if (mejor_operador == "intra_inter")
                vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_intra_inter(s, c); };
            else if (mejor_operador == "paradas")
                vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_con_paradas(s, c); };
            else
                vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_completo(s, c); };

            RunResult r = ejecutar_sa(datos, pesos, mejor_greedy, vfn, opt_params, seed);
            mejoras.push_back(r.mejora_pct);
            costes_ini.push_back(r.coste_inicial);
            costes_fin.push_back(r.coste_fin);
            coberturas.push_back(100.0 * r.clientes_cubiertos / r.n_clientes);
            tiempos.push_back(r.tiempo_s);
        }

        auto [mm, sm]   = stats(mejoras);
        auto [mci, _1]  = stats(costes_ini);
        auto [mcf, _2]  = stats(costes_fin);
        auto [mcob, _3] = stats(coberturas);
        auto [mt_, _4]  = stats(tiempos);

        csv_line(out, {
            to_string(cap), to_string(mci), to_string(mcf),
            to_string(mm), to_string(sm), to_string(mcob), to_string(mt_)
        });

        cout << "  cap=" << cap
             << "  coste_ini=" << fixed << setprecision(1) << mci
             << "  coste_fin=" << mcf
             << "  mejora=" << setprecision(2) << mm << "%"
             << "  cob=" << mcob << "%\n";
    }
    cout << "   Exportado a experimento6_capacidad.csv\n";
}

// =============================================================================
// Exp 7 – Heurísticas Combinadas
// =============================================================================
/**
 * Define H = H1 + w * H2, donde:
 *   H1 = distancia total (minimizar)
 *   H2 = retraso total (penalty por violación de ventanas)
 *
 * Para cada peso w se ejecuta SA y se registran H1 y H2 por separado
 * en el estado final para analizar el trade-off.
 *
 * Necesita una función de coste paramétrica. Se crea usando un PesosCoste
 * modificado en cada iteración.
 *
 * Genera experimento7_heuristicas_combinadas.csv.
 */
void exp7_heuristicas_combinadas(
    const string&  mejor_operador = "completo",
    const string&  mejor_greedy   = "set_cover",
    const SAParams& opt_params    = BASE_PARAMS)
{
    cout << "\n========== EXP 7: Heurísticas Combinadas ==========\n";

    const int n_clientes = 15, n_paradas = 25, n_camiones = 3;
    const double cap = 40.0;

    // Pesos w para H2 (retraso); H1 = distancia permanece con peso base 1.
    vector<double> pesos_w = {0.0, 1.0, 2.0, 4.0, 8.0, 16.0};

    ofstream out("experimento7_heuristicas_combinadas.csv");
    csv_line(out, {"w","seed",
                   "H1_ini","H2_ini","H_ini",
                   "H1_fin","H2_fin","H_fin",
                   "mejora_H","tiempo_s"});

    // Acumuladores para resumen
    map<double, vector<double>> map_H1, map_H2, map_H;

    for (double w : pesos_w) {
        cout << "  w=" << w << "\n";

        for (int seed : SEEDS) {
            DatosProblema datos = generar_dataset(n_clientes, n_paradas, n_camiones, cap, seed);
            mt19937 gen(seed);

            // H1 = distancia (w_dist=1, resto a 0).
            PesosCoste pesos_H1;
            pesos_H1.w_dist             = 1.0;
            pesos_H1.w_carga_baja       = 0.0;
            pesos_H1.w_ventanas         = 0.0;
            pesos_H1.w_retorno_temprano = 0.0;
            pesos_H1.w_capacidad        = 0.0;
            pesos_H1.w_no_servidos      = 0.0;
            auto coste_h1 = [&](const EstadoRuta& s) {
                return calcular_coste(s, datos, pesos_H1);
            };

            // H2 = retraso (w_ventanas=1, resto a 0).
            PesosCoste pesos_H2;
            pesos_H2.w_dist             = 0.0;
            pesos_H2.w_carga_baja       = 0.0;
            pesos_H2.w_ventanas         = 1.0;
            pesos_H2.w_retorno_temprano = 0.0;
            pesos_H2.w_capacidad        = 0.0;
            pesos_H2.w_no_servidos      = 0.0;
            auto coste_h2 = [&](const EstadoRuta& s) {
                return calcular_coste(s, datos, pesos_H2);
            };

            // H = H1 + w*H2: w_dist=1, w_ventanas=w, otros 0.
            // w_no_servidos se mantiene alto para no perder cobertura.
            PesosCoste pesos_w_obj;
            pesos_w_obj.w_dist             = 1.0;
            pesos_w_obj.w_carga_baja       = 0.0;
            pesos_w_obj.w_ventanas         = w;
            pesos_w_obj.w_retorno_temprano = 0.0;
            pesos_w_obj.w_capacidad        = 0.0;
            pesos_w_obj.w_no_servidos      = 100000.0;
            auto coste_h  = [&](const EstadoRuta& s) {
                return calcular_coste(s, datos, pesos_w_obj);
            };

            // Greedy inicial (usa minutos_por_volumen del base default).
            EstadoRuta inicial;
            if (mejor_greedy == "set_cover")
                inicial = greedy_set_cover(datos, pesos_w_obj.minutos_por_volumen);
            else
                inicial = greedy_por_cliente(datos, pesos_w_obj.minutos_por_volumen);

            double H1_ini = coste_h1(inicial);
            double H2_ini = coste_h2(inicial);
            double H_ini  = coste_h(inicial);

            // Vecino
            function<EstadoRuta(const EstadoRuta&)> vfn;
            auto make_ctx = [&]() -> ContextoVecino {
                return {datos, pesos_w_obj.minutos_por_volumen, gen};
            };
            if (mejor_operador == "intra")
                vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_solo_intra(s, c); };
            else if (mejor_operador == "intra_inter")
                vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_intra_inter(s, c); };
            else if (mejor_operador == "paradas")
                vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_con_paradas(s, c); };
            else
                vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_completo(s, c); };

            auto t0 = Clock::now();
            EstadoRuta best = simulated_annealing<EstadoRuta>(
                inicial, coste_h, vfn,
                opt_params.temp_ini, opt_params.temp_fin,
                opt_params.cooling,  opt_params.iters_temp);
            double secs = chrono::duration<double>(Clock::now() - t0).count();

            double H1_fin = coste_h1(best);
            double H2_fin = coste_h2(best);
            double H_fin  = coste_h(best);
            double mejora_H = (H_ini > 1e-9) ? (H_ini - H_fin) / H_ini * 100.0 : 0.0;

            csv_line(out, {
                to_string(w), to_string(seed),
                to_string(H1_ini), to_string(H2_ini), to_string(H_ini),
                to_string(H1_fin), to_string(H2_fin), to_string(H_fin),
                to_string(mejora_H), to_string(secs)
            });

            map_H1[w].push_back(H1_fin);
            map_H2[w].push_back(H2_fin);
            map_H[w].push_back(H_fin);
        }
    }

    // Resumen trade-off
    cout << "\n-- Trade-off H1 (distancia) vs H2 (retraso) --\n";
    cout << left  << setw(8)  << "w"
         << right << setw(14) << "media_H1"
         << setw(14) << "media_H2"
         << setw(14) << "media_H"
         << "\n";
    for (double w : pesos_w) {
        auto [m1, _1] = stats(map_H1[w]);
        auto [m2, _2] = stats(map_H2[w]);
        auto [mh, _h] = stats(map_H[w]);
        cout << left  << setw(8)  << w
             << right << setw(14) << fixed << setprecision(2) << m1
             << setw(14) << m2
             << setw(14) << mh
             << "\n";
    }
    cout << "   Exportado a experimento7_heuristicas_combinadas.csv\n";
}

// =============================================================================
// Exp 8 – Variando número de camiones
// =============================================================================
/**
 * Mantiene n_clientes y n_paradas fijos (escenario medio) y varía el número
 * de camiones disponibles. Sirve para ver:
 *   - Cuántos camiones hacen falta para cubrir todos los clientes (capacidad
 *     agregada vs demanda).
 *   - Cómo decae la utilización por camión (carga inicial / capacidad) al
 *     añadir flota redundante.
 *   - Cómo cambia el coste total (más camiones = más kilometraje fijo de
 *     ida/vuelta al depósito).
 * Genera experimento8_camiones.csv.
 */
void exp8_camiones(
    const string&  mejor_operador = "completo",
    const string&  mejor_greedy   = "set_cover",
    const SAParams& opt_params    = BASE_PARAMS)
{
    cout << "\n========== EXP 8: Variando número de camiones ==========\n";

    const int n_clientes = 15, n_paradas = 25;
    const double cap = 40.0;
    PesosCoste pesos;

    // Cantidades de camiones a explorar.
    vector<int> n_camiones_vals = { 3, 4, 5, 6, 8, 10};

    ofstream out("experimento8_camiones.csv");
    csv_line(out, {"n_camiones",
                   "media_coste_ini","media_coste_fin",
                   "media_mejora","std_mejora",
                   "media_tiempo_s","std_tiempo_s",
                   "media_cobertura"});

    for (int nk : n_camiones_vals) {
        vector<double> mejoras, costes_ini, costes_fin, tiempos, coberturas;

        for (int seed : SEEDS) {
            DatosProblema datos = generar_dataset(n_clientes, n_paradas, nk, cap, seed);
            mt19937 gen(seed);

            function<EstadoRuta(const EstadoRuta&)> vfn;
            auto make_ctx = [&]() -> ContextoVecino {
                return {datos, pesos.minutos_por_volumen, gen};
            };
            if (mejor_operador == "intra")
                vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_solo_intra(s, c); };
            else if (mejor_operador == "intra_inter")
                vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_intra_inter(s, c); };
            else if (mejor_operador == "paradas")
                vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_con_paradas(s, c); };
            else
                vfn = [&](const EstadoRuta& s) { auto c = make_ctx(); return vecino_completo(s, c); };

            RunResult r = ejecutar_sa(datos, pesos, mejor_greedy, vfn, opt_params, seed);
            mejoras.push_back(r.mejora_pct);
            costes_ini.push_back(r.coste_inicial);
            costes_fin.push_back(r.coste_fin);
            tiempos.push_back(r.tiempo_s);
            coberturas.push_back(100.0 * r.clientes_cubiertos / r.n_clientes);
        }

        auto [mm, sm]    = stats(mejoras);
        auto [mci, _1]   = stats(costes_ini);
        auto [mcf, _2]   = stats(costes_fin);
        auto [mt_, st_]  = stats(tiempos);
        auto [mcob, _3]  = stats(coberturas);

        csv_line(out, {
            to_string(nk),
            to_string(mci), to_string(mcf),
            to_string(mm),  to_string(sm),
            to_string(mt_), to_string(st_),
            to_string(mcob)
        });

        cout << "  nk=" << nk
             << "  coste_ini=" << fixed << setprecision(1) << mci
             << "  coste_fin=" << mcf
             << "  mejora="    << setprecision(2) << mm << "%"
             << "  t="         << setprecision(3) << mt_ << "s"
             << "  cob="       << setprecision(1) << mcob << "%\n";
    }
    cout << "   Exportado a experimento8_camiones.csv\n";
}

// =============================================================================
// main – orquesta los experimentos
// =============================================================================

int main(int argc, char** argv) {
    // Podemos seleccionar qué experimentos correr vía args:
    // ./prog all | 1 | 2 | ... | 7
    string which = (argc > 1) ? argv[1] : "all";

    // -------------------------------------------------------------------------
    // Parámetros "óptimos" (pueden sustituirse con los resultados reales
    // del Exp 3). Por defecto se usan los base.
    // -------------------------------------------------------------------------
    const string OPT_OPERADOR = "completo";   // <- actualizar tras Exp 1
    const string OPT_GREEDY   = "cliente";  // <- actualizar tras Exp 2
    SAParams OPT_PARAMS;                      // <- actualizar tras Exp 3
    OPT_PARAMS.temp_ini   = 1000.0;
    OPT_PARAMS.temp_fin   =    0.01;
    OPT_PARAMS.cooling    =    0.95;
    OPT_PARAMS.iters_temp =   200;

    // -------------------------------------------------------------------------
    auto run = [&](int n) {
        return which == "all" || which == to_string(n);
    };

    if (run(1)) exp1_operadores();
    if (run(2)) exp2_inicializacion(OPT_OPERADOR);
    if (run(3)) exp3_grid_search(OPT_OPERADOR, OPT_GREEDY);
    if (run(4)) exp4_escalabilidad_proporcional(OPT_OPERADOR, OPT_GREEDY, OPT_PARAMS);
    if (run(5)) exp5_escalabilidad_separada(OPT_OPERADOR, OPT_GREEDY, OPT_PARAMS);
    if (run(6)) exp6_restriccion_dominio(OPT_OPERADOR, OPT_GREEDY, OPT_PARAMS);
    if (run(7)) exp7_heuristicas_combinadas(OPT_OPERADOR, OPT_GREEDY, OPT_PARAMS);
    if (run(8)) exp8_camiones(OPT_OPERADOR, OPT_GREEDY, OPT_PARAMS);

    cout << "\n=== Todos los experimentos completados ===\n";
    return 0;
}