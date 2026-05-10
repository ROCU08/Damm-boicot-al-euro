#include "estado_distr.cc"
#include <map>
#include <algorithm>

using namespace std;

Solucion greedy_inicial(const vector<Item>& items, int n_palets) {
    Solucion s(n_palets);

    map<uint16_t, vector<Item>> por_cliente;
    for (const auto& item : items)
        por_cliente[item.cliente_id].push_back(item);

    int pal = 0, piso = 0, fila = 0, col = 0;

    auto avanzar = [&]() {
        col++;
        if (col >= Solucion::COLS) { col = 0; fila++; }
        if (fila >= Solucion::FILAS) { fila = 0; piso++; }
        if (piso >= Solucion::PISOS) { piso = 0; pal++; }
    };

    for (auto& entry : por_cliente) {
        auto& v = entry.second;
        sort(v.begin(), v.end(), [](const Item& a, const Item& b) {
            return a.es_barril < b.es_barril;
        });

        for (const auto& item : v) {
            if (pal >= n_palets) break;
            Pos p = {(uint8_t)pal, (uint8_t)piso, (uint8_t)fila, (uint8_t)col};
            s.colocar(p, item);
            avanzar();
        }
    }

    return s;
}
