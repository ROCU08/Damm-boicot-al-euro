#include "estado_ruta.h"
#include <climits>

using namespace std;

void limpiar_cachés(RutaCamion& ruta) {
    ruta.total_distancia      = 0.0;
    ruta.total_carga_inicial  = 0.0;
    ruta.total_pico_volumen   = 0.0;
    ruta.total_retraso        = 0.0;
}

bool factible_capacidad(const RutaCamion& ruta, const Camion& camion) {
    return ruta.total_pico_volumen <= camion.capacidad_volumen;
}

void inicializar_estado(EstadoRuta& estado, const DatosProblema& datos) {
    estado.atendido_ruta.assign(datos.clientes.size(), -1);
    for (int k = 0; k < (int)estado.rutas.size(); ++k) {
        for (const auto& visita : estado.rutas[k].clientes_atendidos) {
            for (int cid : visita) {
                estado.atendido_ruta[cid] = k;
            }
        }
    }
}

// Agregados de UNA visita: ventana horaria efectiva (intersección de las
// ventanas de los clientes atendidos) y volúmenes totales movidos.
namespace {
struct AggVisita {
    int eff_ini;
    int eff_fin;
    double recoger_total;
    double devolver_total;
    bool vacia;
};

AggVisita agregar_visita(const vector<int>& clientes_atendidos,
                         const vector<Cliente>& clientes) {
    AggVisita a;
    a.eff_ini = INT_MIN;
    a.eff_fin = INT_MAX;
    a.recoger_total = 0.0;
    a.devolver_total = 0.0;
    a.vacia = clientes_atendidos.empty();

    for (int cid : clientes_atendidos) {
        const Cliente& cli = clientes[cid];
        if (cli.hora_ini > a.eff_ini) a.eff_ini = cli.hora_ini;
        if (cli.hora_fin < a.eff_fin) a.eff_fin = cli.hora_fin;
        a.recoger_total += cli.volumen_recoger;
        a.devolver_total += cli.volumen_devolver;
    }
    return a;
}
}  // namespace

void recalcular_ruta(RutaCamion& ruta,
                     const DatosProblema& datos,
                     const Camion& camion,
                     double minutos_por_volumen) {
    int n = (int)ruta.paradas.size();

    if (n == 0) {
        limpiar_cachés(ruta);
        return;
    }

    int n_paradas = (int)datos.paradas.size();

    // Carga inicial: suma de volumen_recoger de los clientes que la ruta
    // atiende explícitamente.
    double carga_ini = 0.0;
    for (int i = 0; i < n; ++i) {
        for (int cid : ruta.clientes_atendidos[i]) {
            carga_ini += datos.clientes[cid].volumen_recoger;
        }
    }

    double vol = carga_ini;
    double pico = carga_ini;          // pico durante la leg depósito → primera visita
    double total_dist = 0.0;
    double total_retraso = 0.0;
    double t = (double)camion.hora_inicio;

    // Tramo depósito → primera parada.
    int primera = ruta.paradas[0];
    total_dist += datos.dist_deposito[primera];
    t          += datos.tiempo_deposito[primera];

    for (int i = 0; i < n; ++i) {
        AggVisita agg = agregar_visita(ruta.clientes_atendidos[i], datos.clientes);

        // Espera si llegamos antes de la ventana efectiva.
        if (!agg.vacia && t < (double)agg.eff_ini) {
            t = (double)agg.eff_ini;
        }

        // Retraso si llegamos después de la ventana.
        if (!agg.vacia) {
            double exceso = t - (double)agg.eff_fin;
            if (exceso > 0.0) total_retraso += exceso;
        }

        // Servicio: tiempo proporcional al volumen total movido aquí.
        double servicio = (agg.recoger_total + agg.devolver_total) * minutos_por_volumen;
        t += servicio;

        // Operaciones afectan al volumen a bordo.
        vol -= agg.recoger_total;
        vol += agg.devolver_total;

        // Pico durante la leg que sigue (incluyendo la de retorno tras la última visita).
        if (vol > pico) pico = vol;

        if (i < n - 1) {
            int origen  = ruta.paradas[i];
            int destino = ruta.paradas[i + 1];
            total_dist += datos.matriz_distancia[origen * n_paradas + destino];
            t          += datos.matriz_tiempo[origen * n_paradas + destino];
        }
    }

    // Tramo última parada → depósito (cierra el ciclo).
    int ultima = ruta.paradas.back();
    total_dist += datos.dist_deposito[ultima];
    t          += datos.tiempo_deposito[ultima];

    ruta.total_carga_inicial  = carga_ini;
    ruta.total_distancia      = total_dist;
    ruta.total_pico_volumen   = pico;
    ruta.total_retraso        = total_retraso;
}
