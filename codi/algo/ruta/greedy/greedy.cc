#include "greedy.h"
#include <algorithm>
#include <numeric>
#include <limits>

using namespace std;

// =============================================================================
// Helpers internos compartidos por los dos greedys.
// =============================================================================

namespace {

// Resultado del set cover: paradas elegidas y, en paralelo, la lista de
// clientes que cada parada cubre (un cliente solo aparece una vez, en la
// primera parada que lo cubrió).
struct ResultadoCover {
    vector<int> paradas;
    vector<vector<int>> clientes_por_parada;
};

// Set cover greedy: elige paradas que maximicen el aporte (clientes
// objetivo aún no cubiertos) en cada paso. Una parada NO se elige dos veces.
ResultadoCover greedy_cover(const DatosProblema& datos, const vector<bool>& objetivo) {
    ResultadoCover r;
    int N = (int)objetivo.size();
    int M = (int)datos.paradas.size();

    vector<bool> cubierto(N, false);
    int falta = 0;
    for (int c = 0; c < N; ++c) if (objetivo[c]) ++falta;

    while (falta > 0) {
        int mejor_p = -1;
        int mejor_aporte = 0;
        vector<int> mejor_lista;

        for (int p = 0; p < M; ++p) {
            // Saltar paradas ya elegidas.
            bool ya = false;
            for (int q : r.paradas) if (q == p) { ya = true; break; }
            if (ya) continue;

            int aporte = 0;
            vector<int> lista;
            for (int c : datos.paradas[p].clientes_servidos) {
                if (objetivo[c] && !cubierto[c]) {
                    ++aporte;
                    lista.push_back(c);
                }
            }
            if (aporte > mejor_aporte) {
                mejor_aporte = aporte;
                mejor_p = p;
                mejor_lista = lista;
            }
        }

        if (mejor_p == -1) break;   // ningún cliente más se puede cubrir
        r.paradas.push_back(mejor_p);
        r.clientes_por_parada.push_back(mejor_lista);
        for (int c : mejor_lista) cubierto[c] = true;
        falta -= mejor_aporte;
    }

    return r;
}

// Reordena los dos vectores en paralelo aplicando nearest neighbor desde
// la primera entrada. Útil tras seleccionar un conjunto de paradas para
// reducir la distancia recorrida.
void nn_reorder(vector<int>& paradas,
                vector<vector<int>>& clientes_por_parada,
                const DatosProblema& datos) {
    int n = (int)paradas.size();
    if (n < 2) return;
    int M = (int)datos.paradas.size();

    vector<int> nuevo_p;
    vector<vector<int>> nuevo_c;
    vector<bool> usado(n, false);

    nuevo_p.push_back(paradas[0]);
    nuevo_c.push_back(clientes_por_parada[0]);
    usado[0] = true;

    for (int step = 1; step < n; ++step) {
        int actual = nuevo_p.back();
        int mejor = -1;
        double mejor_d = numeric_limits<double>::max();
        for (int i = 0; i < n; ++i) {
            if (usado[i]) continue;
            double d = datos.matriz_distancia[actual * M + paradas[i]];
            if (d < mejor_d) {
                mejor_d = d;
                mejor = i;
            }
        }
        nuevo_p.push_back(paradas[mejor]);
        nuevo_c.push_back(clientes_por_parada[mejor]);
        usado[mejor] = true;
    }

    paradas = move(nuevo_p);
    clientes_por_parada = move(nuevo_c);
}

double volumen_recoger_total(const vector<int>& clientes, const DatosProblema& datos) {
    double v = 0.0;
    for (int c : clientes) v += datos.clientes[c].volumen_recoger;
    return v;
}

// Devuelve el índice del camión con más capacidad libre. Si todos están al
// límite, devuelve el menos cargado en términos absolutos.
int camion_con_mas_libre(const DatosProblema& datos, const vector<double>& carga) {
    int K = (int)datos.camiones.size();
    int mejor = 0;
    double max_libre = datos.camiones[0].capacidad_volumen - carga[0];
    for (int k = 1; k < K; ++k) {
        double libre = datos.camiones[k].capacidad_volumen - carga[k];
        if (libre > max_libre) {
            max_libre = libre;
            mejor = k;
        }
    }
    return mejor;
}

void finalizar_estado(EstadoRuta& estado, const DatosProblema& datos,
                      double minutos_por_volumen) {
    inicializar_estado(estado, datos);
    int K = (int)estado.rutas.size();
    for (int k = 0; k < K; ++k) {
        recalcular_ruta(estado.rutas[k], datos, datos.camiones[k], minutos_por_volumen);
    }
}

}  // namespace

// =============================================================================
// Greedy 1: pocas paradas (set cover global → bin packing → NN)
// =============================================================================

EstadoRuta greedy_set_cover(const DatosProblema& datos, double minutos_por_volumen) {
    int N = (int)datos.clientes.size();
    int K = (int)datos.camiones.size();

    EstadoRuta estado;
    estado.rutas.resize(K);
    if (N == 0 || K == 0) {
        finalizar_estado(estado, datos, minutos_por_volumen);
        return estado;
    }

    // 1) Set cover global: cubrir todos los clientes posibles.
    vector<bool> objetivo(N, true);
    ResultadoCover cover = greedy_cover(datos, objetivo);

    // 2) Bin packing: orden por volumen decreciente y asignación a camión
    //    con más libre.
    int num_paradas = (int)cover.paradas.size();
    vector<int> orden(num_paradas);
    iota(orden.begin(), orden.end(), 0);
    sort(orden.begin(), orden.end(), [&](int a, int b) {
        return volumen_recoger_total(cover.clientes_por_parada[a], datos)
             > volumen_recoger_total(cover.clientes_por_parada[b], datos);
    });

    vector<vector<int>> rutas_paradas(K);
    vector<vector<vector<int>>> rutas_clientes(K);
    vector<double> carga(K, 0.0);

    for (int idx : orden) {
        int p = cover.paradas[idx];
        const auto& clientes = cover.clientes_por_parada[idx];
        double vol = volumen_recoger_total(clientes, datos);

        int k = camion_con_mas_libre(datos, carga);
        rutas_paradas[k].push_back(p);
        rutas_clientes[k].push_back(clientes);
        carga[k] += vol;
    }

    // 3) Nearest neighbor por ruta y commit al estado.
    for (int k = 0; k < K; ++k) {
        nn_reorder(rutas_paradas[k], rutas_clientes[k], datos);
        estado.rutas[k].paradas = move(rutas_paradas[k]);
        estado.rutas[k].clientes_atendidos = move(rutas_clientes[k]);
    }

    finalizar_estado(estado, datos, minutos_por_volumen);
    return estado;
}

// =============================================================================
// Greedy 2: cargas equilibradas (bin packing por cliente → set cover por
// camión → NN)
// =============================================================================

EstadoRuta greedy_por_cliente(const DatosProblema& datos, double minutos_por_volumen) {
    int N = (int)datos.clientes.size();
    int K = (int)datos.camiones.size();

    EstadoRuta estado;
    estado.rutas.resize(K);
    if (N == 0 || K == 0) {
        finalizar_estado(estado, datos, minutos_por_volumen);
        return estado;
    }

    // 1) Bin packing por cliente: ordenar por volumen desc y asignar al
    //    camión con más libre.
    vector<int> orden_c(N);
    iota(orden_c.begin(), orden_c.end(), 0);
    sort(orden_c.begin(), orden_c.end(), [&](int a, int b) {
        return datos.clientes[a].volumen_recoger > datos.clientes[b].volumen_recoger;
    });

    vector<vector<int>> clientes_por_camion(K);
    vector<double> carga(K, 0.0);

    for (int c : orden_c) {
        int k = camion_con_mas_libre(datos, carga);
        clientes_por_camion[k].push_back(c);
        carga[k] += datos.clientes[c].volumen_recoger;
    }

    // 2) Por camión: set cover greedy SOBRE SUS clientes.
    // 3) Nearest neighbor.
    for (int k = 0; k < K; ++k) {
        if (clientes_por_camion[k].empty()) continue;

        vector<bool> objetivo(N, false);
        for (int c : clientes_por_camion[k]) objetivo[c] = true;

        ResultadoCover cover_k = greedy_cover(datos, objetivo);
        nn_reorder(cover_k.paradas, cover_k.clientes_por_parada, datos);

        estado.rutas[k].paradas = move(cover_k.paradas);
        estado.rutas[k].clientes_atendidos = move(cover_k.clientes_por_parada);
    }

    finalizar_estado(estado, datos, minutos_por_volumen);
    return estado;
}
