#include "op_ruta.h"
#include <algorithm>

using namespace std;

// =============================================================================
// Helpers internos
// =============================================================================

namespace {

bool contiene(const vector<int>& v, int x) {
    for (int e : v) if (e == x) return true;
    return false;
}

bool es_subconjunto_servidos(const vector<int>& subset, const Parada& parada) {
    for (int cid : subset) {
        if (!contiene(parada.clientes_servidos, cid)) return false;
    }
    return true;
}

// Marca a cada cliente del vector como atendido por `ruta_idx` en el cache.
// Si ruta_idx == -1, los marca como no atendidos.
void marcar(EstadoRuta& estado, const vector<int>& clientes, int ruta_idx) {
    for (int cid : clientes) estado.atendido_ruta[cid] = ruta_idx;
}

// Busca la posición dentro de la ruta `ruta_idx` donde está el cliente. Asume
// que `estado.atendido_ruta[cliente_id] == ruta_idx` (es decir, que el cache
// dice que está allí). Devuelve -1 si no aparece (cache inconsistente).
int buscar_posicion(const EstadoRuta& estado, int ruta_idx, int cliente_id) {
    const auto& visitas = estado.rutas[ruta_idx].clientes_atendidos;
    for (int i = 0; i < (int)visitas.size(); ++i) {
        if (contiene(visitas[i], cliente_id)) return i;
    }
    return -1;
}

}  // namespace

// =============================================================================
// Helper público
// =============================================================================

vector<int> clientes_no_cubiertos_de(const EstadoRuta& estado,
                                     const DatosProblema& datos,
                                     int parada_id) {
    vector<int> resultado;
    if (parada_id < 0 || parada_id >= (int)datos.paradas.size()) return resultado;
    for (int cid : datos.paradas[parada_id].clientes_servidos) {
        if (estado.atendido_ruta[cid] == -1) resultado.push_back(cid);
    }
    return resultado;
}

// =============================================================================
// Operadores que NO cambian el conjunto global de clientes atendidos
// =============================================================================

bool swap_intra(RutaCamion& ruta, int i, int j,
                const DatosProblema& datos,
                const Camion& camion,
                double minutos_por_volumen) {
    int n = (int)ruta.paradas.size();
    if (i == j) return true;
    if (i < 0 || j < 0 || i >= n || j >= n) return false;

    RutaCamion backup = ruta;

    swap(ruta.paradas[i], ruta.paradas[j]);
    swap(ruta.clientes_atendidos[i], ruta.clientes_atendidos[j]);
    recalcular_ruta(ruta, datos, camion, minutos_por_volumen);

    if (!factible_capacidad(ruta, camion)) {
        ruta = backup;
        return false;
    }
    // El cache `atendido_ruta` no necesita update: los clientes siguen
    // atendidos por la misma ruta.
    return true;
}

bool relocate(EstadoRuta& estado,
              int ruta_a_idx, int i,
              int ruta_b_idx, int j,
              const DatosProblema& datos,
              double minutos_por_volumen) {
    if (ruta_a_idx == ruta_b_idx) return false;
    if (ruta_a_idx < 0 || ruta_a_idx >= (int)estado.rutas.size()) return false;
    if (ruta_b_idx < 0 || ruta_b_idx >= (int)estado.rutas.size()) return false;

    RutaCamion& ruta_a = estado.rutas[ruta_a_idx];
    RutaCamion& ruta_b = estado.rutas[ruta_b_idx];

    int na = (int)ruta_a.paradas.size();
    int nb = (int)ruta_b.paradas.size();
    if (i < 0 || i >= na) return false;
    if (j < 0 || j > nb) return false;

    RutaCamion backup_a = ruta_a;
    RutaCamion backup_b = ruta_b;

    int parada_id = ruta_a.paradas[i];
    vector<int> clientes_movidos = ruta_a.clientes_atendidos[i];

    ruta_a.paradas.erase(ruta_a.paradas.begin() + i);
    ruta_a.clientes_atendidos.erase(ruta_a.clientes_atendidos.begin() + i);

    ruta_b.paradas.insert(ruta_b.paradas.begin() + j, parada_id);
    ruta_b.clientes_atendidos.insert(ruta_b.clientes_atendidos.begin() + j, clientes_movidos);

    recalcular_ruta(ruta_a, datos, datos.camiones[ruta_a_idx], minutos_por_volumen);
    recalcular_ruta(ruta_b, datos, datos.camiones[ruta_b_idx], minutos_por_volumen);

    if (!factible_capacidad(ruta_a, datos.camiones[ruta_a_idx])
        || !factible_capacidad(ruta_b, datos.camiones[ruta_b_idx])) {
        ruta_a = backup_a;
        ruta_b = backup_b;
        return false;
    }

    // Update cache: los clientes movidos ahora pertenecen a ruta_b_idx.
    marcar(estado, clientes_movidos, ruta_b_idx);
    return true;
}

// =============================================================================
// Operadores que cambian el conjunto global de clientes atendidos
// =============================================================================

bool swap_parada(EstadoRuta& estado, int ruta_idx, int i,
                 int parada_nueva,
                 const vector<int>& clientes_a_atender,
                 const DatosProblema& datos,
                 double minutos_por_volumen) {
    if (ruta_idx < 0 || ruta_idx >= (int)estado.rutas.size()) return false;
    if (parada_nueva < 0 || parada_nueva >= (int)datos.paradas.size()) return false;

    RutaCamion& ruta = estado.rutas[ruta_idx];
    int n = (int)ruta.paradas.size();
    if (i < 0 || i >= n) return false;

    // 1) Subconjunto válido respecto a clientes_servidos de la nueva parada.
    if (!es_subconjunto_servidos(clientes_a_atender, datos.paradas[parada_nueva])) {
        return false;
    }

    // 2) Solapamiento via cache: cliente ya atendido y NO en la posición que
    //    vamos a sustituir es solapamiento real.
    const vector<int>& clientes_viejos = ruta.clientes_atendidos[i];
    for (int cid : clientes_a_atender) {
        if (estado.atendido_ruta[cid] != -1 && !contiene(clientes_viejos, cid)) {
            return false;
        }
    }

    // 3) Aplicar y comprobar capacidad.
    RutaCamion backup = ruta;
    vector<int> viejos_copia = clientes_viejos;   // necesitamos copia antes de sobrescribir
    ruta.paradas[i] = parada_nueva;
    ruta.clientes_atendidos[i] = clientes_a_atender;
    recalcular_ruta(ruta, datos, datos.camiones[ruta_idx], minutos_por_volumen);

    if (!factible_capacidad(ruta, datos.camiones[ruta_idx])) {
        ruta = backup;
        return false;
    }

    // Update cache: borrar viejos, marcar nuevos.
    marcar(estado, viejos_copia, -1);
    marcar(estado, clientes_a_atender, ruta_idx);
    return true;
}

bool insertar_parada(EstadoRuta& estado, int ruta_idx, int j,
                     int parada_nueva,
                     const vector<int>& clientes_a_atender,
                     const DatosProblema& datos,
                     double minutos_por_volumen) {
    if (ruta_idx < 0 || ruta_idx >= (int)estado.rutas.size()) return false;
    if (parada_nueva < 0 || parada_nueva >= (int)datos.paradas.size()) return false;

    RutaCamion& ruta = estado.rutas[ruta_idx];
    int n = (int)ruta.paradas.size();
    if (j < 0 || j > n) return false;

    // 1) Subconjunto válido.
    if (!es_subconjunto_servidos(clientes_a_atender, datos.paradas[parada_nueva])) {
        return false;
    }

    // 2) Solapamiento via cache: cualquier cliente ya atendido es solapamiento.
    for (int cid : clientes_a_atender) {
        if (estado.atendido_ruta[cid] != -1) return false;
    }

    // 3) Aplicar y comprobar capacidad.
    RutaCamion backup = ruta;
    ruta.paradas.insert(ruta.paradas.begin() + j, parada_nueva);
    ruta.clientes_atendidos.insert(ruta.clientes_atendidos.begin() + j, clientes_a_atender);
    recalcular_ruta(ruta, datos, datos.camiones[ruta_idx], minutos_por_volumen);

    if (!factible_capacidad(ruta, datos.camiones[ruta_idx])) {
        ruta = backup;
        return false;
    }

    marcar(estado, clientes_a_atender, ruta_idx);
    return true;
}

bool eliminar_parada(EstadoRuta& estado, int ruta_idx, int i,
                     const DatosProblema& datos,
                     double minutos_por_volumen) {
    if (ruta_idx < 0 || ruta_idx >= (int)estado.rutas.size()) return false;

    RutaCamion& ruta = estado.rutas[ruta_idx];
    int n = (int)ruta.paradas.size();
    if (i < 0 || i >= n) return false;

    // Guardamos quiénes estaban atendidos aquí antes de borrar.
    vector<int> clientes_perdidos = ruta.clientes_atendidos[i];

    ruta.paradas.erase(ruta.paradas.begin() + i);
    ruta.clientes_atendidos.erase(ruta.clientes_atendidos.begin() + i);
    recalcular_ruta(ruta, datos, datos.camiones[ruta_idx], minutos_por_volumen);

    // Update cache: esos clientes ya no están atendidos.
    marcar(estado, clientes_perdidos, -1);
    return true;
}

bool mover_cliente(EstadoRuta& estado, int cliente_id,
                   int ruta_dst, int pos_dst,
                   const DatosProblema& datos,
                   double minutos_por_volumen) {
    if (cliente_id < 0 || cliente_id >= (int)datos.clientes.size()) return false;

    int ruta_origen = estado.atendido_ruta[cliente_id];
    if (ruta_origen == -1) return false;     // no atendido en ningún sitio

    if (ruta_dst < 0 || ruta_dst >= (int)estado.rutas.size()) return false;
    if (pos_dst < 0 || pos_dst >= (int)estado.rutas[ruta_dst].paradas.size()) return false;

    int pos_origen = buscar_posicion(estado, ruta_origen, cliente_id);
    if (pos_origen == -1) return false;      // cache inconsistente

    // No-op si ya está exactamente en (ruta_dst, pos_dst).
    if (ruta_origen == ruta_dst && pos_origen == pos_dst) return true;

    // La parada destino debe poder servir al cliente.
    int parada_dst_id = estado.rutas[ruta_dst].paradas[pos_dst];
    if (!contiene(datos.paradas[parada_dst_id].clientes_servidos, cliente_id)) {
        return false;
    }

    // Backup y aplicar.
    if (ruta_origen == ruta_dst) {
        // Movimiento interno a una sola ruta.
        RutaCamion& r = estado.rutas[ruta_origen];
        RutaCamion backup = r;

        auto& origen_v = r.clientes_atendidos[pos_origen];
        origen_v.erase(remove(origen_v.begin(), origen_v.end(), cliente_id), origen_v.end());
        r.clientes_atendidos[pos_dst].push_back(cliente_id);

        recalcular_ruta(r, datos, datos.camiones[ruta_origen], minutos_por_volumen);

        if (!factible_capacidad(r, datos.camiones[ruta_origen])) {
            r = backup;
            return false;
        }
        // El cache no cambia: el cliente sigue atendido en la misma ruta.
    } else {
        // Movimiento entre dos rutas.
        RutaCamion& ra = estado.rutas[ruta_origen];
        RutaCamion& rb = estado.rutas[ruta_dst];
        RutaCamion backup_a = ra;
        RutaCamion backup_b = rb;

        auto& origen_v = ra.clientes_atendidos[pos_origen];
        origen_v.erase(remove(origen_v.begin(), origen_v.end(), cliente_id), origen_v.end());
        rb.clientes_atendidos[pos_dst].push_back(cliente_id);

        recalcular_ruta(ra, datos, datos.camiones[ruta_origen], minutos_por_volumen);
        recalcular_ruta(rb, datos, datos.camiones[ruta_dst], minutos_por_volumen);

        if (!factible_capacidad(ra, datos.camiones[ruta_origen])
            || !factible_capacidad(rb, datos.camiones[ruta_dst])) {
            ra = backup_a;
            rb = backup_b;
            return false;
        }
        estado.atendido_ruta[cliente_id] = ruta_dst;
    }

    return true;
}
