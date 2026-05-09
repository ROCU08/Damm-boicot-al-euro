#include "../estado_distr.cc"
#include <random>
#include <vector>

using namespace std;

static vector<Pos> slots_ocupados(const Solucion& s) {
    vector<Pos> ocupados;
    for (int pal = 0; pal < s.n_palets; ++pal)
        for (int piso = 0; piso < Solucion::PISOS; ++piso)
            for (int f = 0; f < Solucion::FILAS; ++f)
                for (int c = 0; c < Solucion::COLS; ++c)
                    if (!s.layout[pal][piso][f][c].vacio())
                        ocupados.push_back({(uint8_t)pal, (uint8_t)piso, (uint8_t)f, (uint8_t)c});
    return ocupados;
}

static vector<Pos> slots_vacios(const Solucion& s) {
    vector<Pos> vacios;
    for (int pal = 0; pal < s.n_palets; ++pal)
        for (int piso = 0; piso < Solucion::PISOS; ++piso)
            for (int f = 0; f < Solucion::FILAS; ++f)
                for (int c = 0; c < Solucion::COLS; ++c)
                    if (s.layout[pal][piso][f][c].vacio())
                        vacios.push_back({(uint8_t)pal, (uint8_t)piso, (uint8_t)f, (uint8_t)c});
    return vacios;
}

Solucion vecino_swap(const Solucion& s, mt19937& rng) {
    Solucion copia = s;
    auto ocupados = slots_ocupados(copia);
    if (ocupados.size() < 2) return copia;

    uniform_int_distribution<size_t> dist(0, ocupados.size() - 1);
    size_t i = dist(rng);
    size_t j = i;
    while (j == i) j = dist(rng);

    copia.swap_items(ocupados[i], ocupados[j]);
    return copia;
}

Solucion vecino_mover(const Solucion& s, mt19937& rng) {
    Solucion copia = s;
    auto ocupados = slots_ocupados(copia);
    auto vacios = slots_vacios(copia);
    if (ocupados.empty() || vacios.empty()) return copia;

    uniform_int_distribution<size_t> dist_oc(0, ocupados.size() - 1);
    uniform_int_distribution<size_t> dist_va(0, vacios.size() - 1);

    Pos origen = ocupados[dist_oc(rng)];
    Pos destino = vacios[dist_va(rng)];

    Item item = copia.get(origen);
    copia.quitar(origen);
    copia.colocar(destino, item);
    return copia;
}
