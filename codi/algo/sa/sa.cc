#pragma once
#include <functional>
#include <random>
#include <cmath>
#include <iostream>

using namespace std;

struct ParametrosSA {
    double T_init;
    double T_min;
    double cooling_rate;
    int iter_por_temp;
};

template <typename Estado>
Estado simulated_annealing(
    Estado inicial,
    function<double(const Estado&)> costo,
    function<Estado(const Estado&, mt19937&)> vecino,
    ParametrosSA params,
    mt19937& rng
) {
    Estado actual = inicial;
    double costo_actual = costo(actual);

    Estado mejor = actual;
    double costo_mejor = costo_actual;

    double T = params.T_init;

    while (T > params.T_min) {
        for (int i = 0; i < params.iter_por_temp; ++i) {
            Estado candidato = vecino(actual, rng);
            double costo_candidato = costo(candidato);
            double delta = costo_candidato - costo_actual;

            if (delta < 0 || uniform_real_distribution<double>(0.0, 1.0)(rng) < exp(-delta / T)) {
                actual = candidato;
                costo_actual = costo_candidato;
            }

            if (costo_actual < costo_mejor) {
                mejor = actual;
                costo_mejor = costo_actual;
            }
        }
        T *= params.cooling_rate;
    }

    cerr << "[SA] Mejor costo: " << costo_mejor << endl;
    return mejor;
}
