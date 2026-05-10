#include "../estado_distr.cc"
#include <random>

using namespace std;

Solucion vecino_swap(const Solucion& s, mt19937& rng) {
    Solucion copia = s;
    if (copia.ocupados.size() < 2) return copia;

    uniform_int_distribution<size_t> dist(0, copia.ocupados.size() - 1);
    size_t i = dist(rng);
    size_t j = i;
    while (j == i) j = dist(rng);

    copia.swap_items(copia.ocupados[i], copia.ocupados[j]);
    return copia;
}

Solucion vecino_mover(const Solucion& s, mt19937& rng) {
    Solucion copia = s;
    if (copia.ocupados.empty() || copia.vacios.empty()) return copia;

    uniform_int_distribution<size_t> dist_oc(0, copia.ocupados.size() - 1);
    uniform_int_distribution<size_t> dist_va(0, copia.vacios.size() - 1);

    Pos origen = copia.ocupados[dist_oc(rng)];
    Pos destino = copia.vacios[dist_va(rng)];

    Item item = copia.get(origen);
    copia.quitar(origen);
    copia.colocar(destino, item);
    return copia;
}
