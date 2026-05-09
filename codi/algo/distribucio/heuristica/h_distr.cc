#include "../estado_distr.cc"
#include <set>
#include <unordered_set>

using namespace std;

// F1: Fragmentación por producto
// Para cada material, cuenta en cuántos (palé, piso) distintos aparece.
// Penalización = suma de (ubicaciones_distintas - 1) por material.
// Score 0 = cada material concentrado en un único (palé, piso).
double fragmentacion_producto(const Solucion& s) {
    double penalizacion = 0.0;
    for (const auto& entry : s.por_material) {
        const auto& posiciones = entry.second;
        set<pair<uint8_t, uint8_t>> ubicaciones;
        for (const auto& p : posiciones)
            ubicaciones.insert(make_pair(p.palet, p.piso));
        if (ubicaciones.size() > 1)
            penalizacion += ubicaciones.size() - 1;
    }
    return penalizacion;
}

// F2: Fragmentación por cliente
// Para cada cliente, cuenta en cuántos palés distintos tiene ítems.
// Penalización = suma de (palés_distintos - 1) por cliente.
// Score 0 = cada cliente concentrado en un único palé.
double fragmentacion_cliente(const Solucion& s) {
    double penalizacion = 0.0;
    for (const auto& entry : s.por_cliente) {
        const auto& posiciones = entry.second;
        unordered_set<uint8_t> palets;
        for (const auto& p : posiciones)
            palets.insert(p.palet);
        if (palets.size() > 1)
            penalizacion += palets.size() - 1;
    }
    return penalizacion;
}

// F3: Accesibilidad en descarga + restricción barriles sobre cajas
// orden_ruta: cliente_id ordenados por orden de entrega (primero se entrega orden_ruta[0]).
// Penaliza:
//   a) Ítems de un cliente "temprano" bloqueados por ítems de cliente "tardío" en pisos superiores
//   b) Barriles colocados encima de cajas en el mismo palé
double accesibilidad(const Solucion& s, const vector<uint16_t>& orden_ruta) {
    unordered_map<uint16_t, int> orden;
    for (int i = 0; i < (int)orden_ruta.size(); ++i)
        orden[orden_ruta[i]] = i;

    double penalizacion = 0.0;

    for (int pal = 0; pal < s.n_palets; ++pal) {
        for (int piso = 0; piso < Solucion::PISOS; ++piso) {
            bool piso_tiene_barril = false;

            for (int f = 0; f < Solucion::FILAS; ++f)
                for (int c = 0; c < Solucion::COLS; ++c)
                    if (!s.layout[pal][piso][f][c].vacio() && s.layout[pal][piso][f][c].es_barril)
                        piso_tiene_barril = true;

            // Barril encima de caja: comprobar pisos inferiores
            if (piso_tiene_barril) {
                for (int piso_inf = 0; piso_inf < piso; ++piso_inf)
                    for (int f = 0; f < Solucion::FILAS; ++f)
                        for (int c = 0; c < Solucion::COLS; ++c) {
                            const Item& inf = s.layout[pal][piso_inf][f][c];
                            if (!inf.vacio() && !inf.es_barril)
                                penalizacion += 10.0;
                        }
            }

            // Accesibilidad: ítems bloqueados por clientes posteriores arriba
            for (int f = 0; f < Solucion::FILAS; ++f)
                for (int c = 0; c < Solucion::COLS; ++c) {
                    const Item& item = s.layout[pal][piso][f][c];
                    if (item.vacio()) continue;

                    auto it_ord = orden.find(item.cliente_id);
                    if (it_ord == orden.end()) continue;
                    int mi_orden = it_ord->second;

                    for (int piso_sup = piso + 1; piso_sup < Solucion::PISOS; ++piso_sup)
                        for (int f2 = 0; f2 < Solucion::FILAS; ++f2)
                            for (int c2 = 0; c2 < Solucion::COLS; ++c2) {
                                const Item& sup = s.layout[pal][piso_sup][f2][c2];
                                if (sup.vacio()) continue;
                                auto it_sup = orden.find(sup.cliente_id);
                                if (it_sup == orden.end()) continue;
                                if (it_sup->second > mi_orden)
                                    penalizacion += 1.0;
                            }
                }
        }
    }

    return penalizacion;
}

double fitness(const Solucion& s, const vector<uint16_t>& orden_ruta) {
    double w1 = 1.0, w2 = 2.0, w3 = 3.0;
    return w1 * fragmentacion_producto(s)
         + w2 * fragmentacion_cliente(s)
         + w3 * accesibilidad(s, orden_ruta);
}
