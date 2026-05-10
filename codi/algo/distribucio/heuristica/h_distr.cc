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

// Comprueba si un ítem es físicamente alcanzable según el tipo de vehículo.
// Reglas:
//   - Camión 8 (n>=7): pallets 0,1 acceso completo; pares acceso desde fila 0; impares desde fila 4
//   - Camión 6 (n=4-6): pares acceso desde fila 0; impares desde fila 4
//   - Furgoneta (n<=3): todos acceso desde fila 4
bool es_accesible(const Solucion& s, int pal, int piso, int fila, int col) {
    // Vertical: si hay algo encima en la misma columna, no se puede sacar
    for (int p = piso + 1; p < Solucion::PISOS; ++p)
        if (!s.layout[pal][p][fila][col].vacio())
            return false;

    bool es_furgoneta = (s.n_palets <= 3);
    bool es_camion_8 = (s.n_palets >= 7);

    // Camión 8: pallets 0 y 1 se bajan al suelo, acceso total
    if (es_camion_8 && pal <= 1)
        return true;

    // Lateral: pares acceden desde fila 0, impares desde fila 4
    // Furgoneta: todos acceden desde fila 4
    bool desde_fila_0 = !es_furgoneta && (pal % 2 == 0);

    if (desde_fila_0) {
        // Acceso lateral desde fila 0: nada puede bloquear entre fila 0 y esta fila
        for (int f = 0; f < fila; ++f)
            if (!s.layout[pal][piso][f][col].vacio())
                return false;
    } else {
        // Acceso lateral desde fila 4: nada puede bloquear entre esta fila y fila 4
        for (int f = fila + 1; f < Solucion::FILAS; ++f)
            if (!s.layout[pal][piso][f][col].vacio())
                return false;
    }

    return true;
}

// F3: Penalización de accesibilidad en descarga
double accesibilidad(const Solucion& s, const vector<uint16_t>& orden_ruta) {
    // Mapa cliente_id -> posición en ruta (0 = primera entrega)
    unordered_map<uint16_t, int> orden;
    for (int i = 0; i < (int)orden_ruta.size(); ++i)
        orden[orden_ruta[i]] = i;

    bool es_furgoneta = (s.n_palets <= 3);
    bool es_camion_8 = (s.n_palets >= 7);

    double penalizacion = 0.0;

    for (int pal = 0; pal < s.n_palets; ++pal) {
        // Pallets 0,1 en camión 8: acceso completo, sin bloqueo lateral
        bool acceso_completo = (es_camion_8 && pal <= 1);
        // Pares acceden desde fila 0, impares desde fila 4 (furgoneta: todos desde fila 4)
        bool desde_fila_0 = !es_furgoneta && (pal % 2 == 0);

        for (int piso = 0; piso < Solucion::PISOS; ++piso) {

            // --- Restricción física: barriles no pueden estar encima de cajas ---
            bool piso_tiene_barril = false;
            for (int f = 0; f < Solucion::FILAS; ++f)
                for (int c = 0; c < Solucion::COLS; ++c)
                    if (!s.layout[pal][piso][f][c].vacio() && s.layout[pal][piso][f][c].es_barril)
                        piso_tiene_barril = true;

            if (piso_tiene_barril) {
                // Penalizar cada caja en pisos inferiores (barril aplastándola)
                for (int p_inf = 0; p_inf < piso; ++p_inf)
                    for (int f = 0; f < Solucion::FILAS; ++f)
                        for (int c = 0; c < Solucion::COLS; ++c) {
                            const Item& inf = s.layout[pal][p_inf][f][c];
                            if (!inf.vacio() && !inf.es_barril)
                                penalizacion += 10.0;
                        }
            }

            // --- Bloqueos por orden de entrega ---
            for (int f = 0; f < Solucion::FILAS; ++f) {
                for (int c = 0; c < Solucion::COLS; ++c) {
                    const Item& item = s.layout[pal][piso][f][c];
                    if (item.vacio()) continue;

                    auto it = orden.find(item.cliente_id);
                    if (it == orden.end()) continue;
                    int mi_orden = it->second;

                    // Bloqueo vertical: cliente tardío encima en la misma (fila, col)
                    for (int p = piso + 1; p < Solucion::PISOS; ++p) {
                        const Item& sup = s.layout[pal][p][f][c];
                        if (sup.vacio()) continue;
                        auto it2 = orden.find(sup.cliente_id);
                        if (it2 != orden.end() && it2->second > mi_orden)
                            penalizacion += 1.0;
                    }

                    // Bloqueo lateral: cliente tardío entre el ítem y el lado de acceso
                    if (!acceso_completo) {
                        if (desde_fila_0) {
                            // Acceso desde fila 0: comprobar filas entre 0 y esta
                            for (int f2 = 0; f2 < f; ++f2) {
                                const Item& lat = s.layout[pal][piso][f2][c];
                                if (lat.vacio()) continue;
                                auto it2 = orden.find(lat.cliente_id);
                                if (it2 != orden.end() && it2->second > mi_orden)
                                    penalizacion += 1.0;
                            }
                        } else {
                            // Acceso desde fila 4: comprobar filas entre esta y fila 4
                            for (int f2 = f + 1; f2 < Solucion::FILAS; ++f2) {
                                const Item& lat = s.layout[pal][piso][f2][c];
                                if (lat.vacio()) continue;
                                auto it2 = orden.find(lat.cliente_id);
                                if (it2 != orden.end() && it2->second > mi_orden)
                                    penalizacion += 1.0;
                            }
                        }
                    }
                }
            }
        }
    }

    return penalizacion;
}

// Hard constraint: valida gravedad, pisos llenos, palets llenos.
// Retorna false al primer fallo (short-circuit).
bool es_valida(const Solucion& s) {
    int cap_piso = Solucion::FILAS * Solucion::COLS;
    int cap_palet = Solucion::PISOS * cap_piso;

    // a) Gravedad: no puede haber ítems flotando
    for (int pal = 0; pal < s.n_palets; ++pal)
        for (int piso = 1; piso < Solucion::PISOS; ++piso)
            for (int f = 0; f < Solucion::FILAS; ++f)
                for (int c = 0; c < Solucion::COLS; ++c)
                    if (!s.layout[pal][piso][f][c].vacio() && s.layout[pal][piso - 1][f][c].vacio())
                        return false;

    // b) No empezar piso si el inferior no está lleno
    for (int pal = 0; pal < s.n_palets; ++pal)
        for (int piso = 1; piso < Solucion::PISOS; ++piso) {
            bool tiene_items = false;
            for (int f = 0; f < Solucion::FILAS && !tiene_items; ++f)
                for (int c = 0; c < Solucion::COLS && !tiene_items; ++c)
                    if (!s.layout[pal][piso][f][c].vacio())
                        tiene_items = true;
            if (!tiene_items) continue;

            int ocupados_inf = 0;
            for (int f = 0; f < Solucion::FILAS; ++f)
                for (int c = 0; c < Solucion::COLS; ++c)
                    if (!s.layout[pal][piso - 1][f][c].vacio())
                        ocupados_inf++;
            if (ocupados_inf < cap_piso) return false;
        }

    // c) No empezar palet si el anterior no está lleno
    for (int pal = 1; pal < s.n_palets; ++pal) {
        bool tiene_items = false;
        for (int piso = 0; piso < Solucion::PISOS && !tiene_items; ++piso)
            for (int f = 0; f < Solucion::FILAS && !tiene_items; ++f)
                for (int c = 0; c < Solucion::COLS && !tiene_items; ++c)
                    if (!s.layout[pal][piso][f][c].vacio())
                        tiene_items = true;
        if (!tiene_items) continue;

        int ocupados_prev = 0;
        for (int piso = 0; piso < Solucion::PISOS; ++piso)
            for (int f = 0; f < Solucion::FILAS; ++f)
                for (int c = 0; c < Solucion::COLS; ++c)
                    if (!s.layout[pal - 1][piso][f][c].vacio())
                        ocupados_prev++;
        if (ocupados_prev < cap_palet) return false;
    }

    return true;
}

double fitness(const Solucion& s, const vector<uint16_t>& orden_ruta) {
    double w2 = 2.0, w3 = 3.0,w1 = 1.0;
    return w1 * fragmentacion_producto(s) + w2* fragmentacion_cliente(s)
         + w3 * accesibilidad(s, orden_ruta);
}
