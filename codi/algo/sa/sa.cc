#include <iostream>
#include <cmath>
#include <random>
#include <functional>

using namespace std;

// Template genèrica per a Simulated Annealing
template <typename State>
State simulated_annealing(
    State initial_state,
    function<double(const State&)> cost_function,
    function<State(const State&)> get_neighbor,
    double initial_temp,
    double final_temp,
    double cooling_rate,
    int iterations_per_temp
) {
    State current_state = initial_state;
    State best_state = initial_state;

    double current_cost = cost_function(current_state);
    double best_cost = current_cost;

    double temp = initial_temp;

    // Inicialització del generador de nombres aleatoris
    random_device rd;
    mt19937 gen(rd());
    uniform_real_distribution<> dis(0.0, 1.0);

    while (temp > final_temp) {
        for (int i = 0; i < iterations_per_temp; ++i) {
            State neighbor = get_neighbor(current_state);
            double neighbor_cost = cost_function(neighbor);

            double cost_diff = neighbor_cost - current_cost;

            // Si el veí és millor (cost menor), o s'accepta per probabilitat de Boltzmann
            if (cost_diff < 0 || exp(-cost_diff / temp) > dis(gen)) {
                current_state = neighbor;
                current_cost = neighbor_cost;

                // Guardem el millor resultat global trobat fins ara
                if (current_cost < best_cost) {
                    best_state = current_state;
                    best_cost = current_cost;
                }
            }
        }
        temp *= cooling_rate; // Procés de refredament
    }

    return best_state;
}