#include "vecino.h"
#include "../operadores/op_ruta.h"
#include <algorithm>

using namespace std;

// =============================================================================
// Helpers internos
// =============================================================================

namespace {

constexpr int MAX_INTENTOS = 10;

int rnd_idx(mt19937& gen, int n) {
    if (n <= 0) return 0;
    return uniform_int_distribution<int>(0, n - 1)(gen);
}

bool contiene(const vector<int>& v, int x) {
    for (int e : v) if (e == x) return true;
    return false;
}

// -----------------------------------------------------------------------------
// Wrappers "intentar_X": eligen parámetros aleatorios y llaman al operador.
// Devuelven true si el operador aceptó el movimiento.
// -----------------------------------------------------------------------------

bool intentar_swap_intra(EstadoRuta& estado, ContextoVecino& ctx) {
    int n_rutas = (int)estado.rutas.size();
    if (n_rutas == 0) return false;

    int k = rnd_idx(ctx.gen, n_rutas);
    int n = (int)estado.rutas[k].paradas.size();
    if (n < 2) return false;

    int i = rnd_idx(ctx.gen, n);
    int j = rnd_idx(ctx.gen, n);
    if (i == j) j = (j + 1) % n;

    return swap_intra(estado.rutas[k], i, j, ctx.datos,
                      ctx.datos.camiones[k], ctx.minutos_por_volumen);
}

bool intentar_relocate(EstadoRuta& estado, ContextoVecino& ctx) {
    int n_rutas = (int)estado.rutas.size();
    if (n_rutas < 2) return false;

    int ka = rnd_idx(ctx.gen, n_rutas);
    int kb = rnd_idx(ctx.gen, n_rutas);
    if (ka == kb) kb = (kb + 1) % n_rutas;

    int na = (int)estado.rutas[ka].paradas.size();
    if (na == 0) return false;
    int nb = (int)estado.rutas[kb].paradas.size();

    int i = rnd_idx(ctx.gen, na);
    int j = rnd_idx(ctx.gen, nb + 1);   // permitido insertar al final → nb+1 huecos

    return relocate(estado, ka, i, kb, j, ctx.datos, ctx.minutos_por_volumen);
}

bool intentar_swap_parada(EstadoRuta& estado, ContextoVecino& ctx) {
    int n_rutas = (int)estado.rutas.size();
    int n_paradas = (int)ctx.datos.paradas.size();
    if (n_rutas == 0 || n_paradas < 2) return false;

    int k = rnd_idx(ctx.gen, n_rutas);
    int n = (int)estado.rutas[k].paradas.size();
    if (n == 0) return false;

    int i = rnd_idx(ctx.gen, n);
    int parada_actual = estado.rutas[k].paradas[i];
    int parada_nueva = rnd_idx(ctx.gen, n_paradas);
    if (parada_nueva == parada_actual) parada_nueva = (parada_nueva + 1) % n_paradas;

    // Subset greedy: clientes que la nueva parada puede atender y que no están
    // ya atendidos en otra visita (los que están en clientes_viejos sí entran,
    // porque vamos a sustituirlos).
    const vector<int>& clientes_viejos = estado.rutas[k].clientes_atendidos[i];
    vector<int> clientes_a_atender;
    for (int cid : ctx.datos.paradas[parada_nueva].clientes_servidos) {
        if (estado.atendido_ruta[cid] == -1 || contiene(clientes_viejos, cid)) {
            clientes_a_atender.push_back(cid);
        }
    }

    return swap_parada(estado, k, i, parada_nueva, clientes_a_atender,
                       ctx.datos, ctx.minutos_por_volumen);
}

bool intentar_insertar_parada(EstadoRuta& estado, ContextoVecino& ctx) {
    int n_rutas = (int)estado.rutas.size();
    int n_paradas = (int)ctx.datos.paradas.size();
    if (n_rutas == 0 || n_paradas == 0) return false;

    int k = rnd_idx(ctx.gen, n_rutas);
    int n = (int)estado.rutas[k].paradas.size();
    int j = rnd_idx(ctx.gen, n + 1);
    int parada_nueva = rnd_idx(ctx.gen, n_paradas);

    // Subset greedy: solo clientes no atendidos por nadie. Si la nueva parada
    // no aporta clientes nuevos (todos ya cubiertos), descartamos por
    // redundancia.
    vector<int> clientes_a_atender = clientes_no_cubiertos_de(estado, ctx.datos, parada_nueva);
    if (clientes_a_atender.empty()) return false;

    return insertar_parada(estado, k, j, parada_nueva, clientes_a_atender,
                           ctx.datos, ctx.minutos_por_volumen);
}

bool intentar_eliminar_parada(EstadoRuta& estado, ContextoVecino& ctx) {
    int n_rutas = (int)estado.rutas.size();
    if (n_rutas == 0) return false;

    int k = rnd_idx(ctx.gen, n_rutas);
    int n = (int)estado.rutas[k].paradas.size();
    if (n == 0) return false;

    int i = rnd_idx(ctx.gen, n);
    return eliminar_parada(estado, k, i, ctx.datos, ctx.minutos_por_volumen);
}

bool intentar_mover_cliente(EstadoRuta& estado, ContextoVecino& ctx) {
    // Lista de clientes actualmente atendidos.
    vector<int> atendidos;
    int n_clientes = (int)ctx.datos.clientes.size();
    for (int c = 0; c < n_clientes; ++c) {
        if (estado.atendido_ruta[c] != -1) atendidos.push_back(c);
    }
    if (atendidos.empty()) return false;

    int cliente_id = atendidos[rnd_idx(ctx.gen, atendidos.size())];

    // Destinos candidatos: cualquier (ruta, posición) cuya parada sirva al
    // cliente. Incluye su ubicación actual; mover_cliente lo trata como no-op.
    vector<pair<int, int>> destinos;
    for (int k = 0; k < (int)estado.rutas.size(); ++k) {
        const auto& paradas_k = estado.rutas[k].paradas;
        for (int i = 0; i < (int)paradas_k.size(); ++i) {
            if (contiene(ctx.datos.paradas[paradas_k[i]].clientes_servidos, cliente_id)) {
                destinos.push_back({k, i});
            }
        }
    }
    if (destinos.size() < 2) return false;   // sin destino alternativo

    auto [ruta_dst, pos_dst] = destinos[rnd_idx(ctx.gen, destinos.size())];
    return mover_cliente(estado, cliente_id, ruta_dst, pos_dst,
                         ctx.datos, ctx.minutos_por_volumen);
}

// -----------------------------------------------------------------------------
// Bucle común: prueba operadores aleatorios hasta aceptar uno o agotar intentos.
// -----------------------------------------------------------------------------

using OpFn = bool (*)(EstadoRuta&, ContextoVecino&);

EstadoRuta aplicar_op_aleatoria(const EstadoRuta& estado,
                                ContextoVecino& ctx,
                                const vector<OpFn>& ops) {
    EstadoRuta candidato = estado;
    for (int t = 0; t < MAX_INTENTOS; ++t) {
        OpFn op = ops[rnd_idx(ctx.gen, ops.size())];
        if (op(candidato, ctx)) return candidato;
        // Si rechazó, el operador ya restauró su parte; candidato == estado.
    }
    return candidato;   // todos rechazados → devolvemos copia sin cambios
}

}  // namespace

// =============================================================================
// Variantes públicas
// =============================================================================

EstadoRuta vecino_solo_intra(const EstadoRuta& estado, ContextoVecino& ctx) {
    static const vector<OpFn> ops = { intentar_swap_intra };
    return aplicar_op_aleatoria(estado, ctx, ops);
}

EstadoRuta vecino_intra_inter(const EstadoRuta& estado, ContextoVecino& ctx) {
    static const vector<OpFn> ops = {
        intentar_swap_intra,
        intentar_relocate
    };
    return aplicar_op_aleatoria(estado, ctx, ops);
}

EstadoRuta vecino_con_paradas(const EstadoRuta& estado, ContextoVecino& ctx) {
    static const vector<OpFn> ops = {
        intentar_swap_intra,
        intentar_relocate,
        intentar_swap_parada,
        intentar_insertar_parada,
        intentar_eliminar_parada
    };
    return aplicar_op_aleatoria(estado, ctx, ops);
}

EstadoRuta vecino_completo(const EstadoRuta& estado, ContextoVecino& ctx) {
    static const vector<OpFn> ops = {
        intentar_swap_intra,
        intentar_relocate,
        intentar_swap_parada,
        intentar_insertar_parada,
        intentar_eliminar_parada,
        intentar_mover_cliente
    };
    return aplicar_op_aleatoria(estado, ctx, ops);
}
