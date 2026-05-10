#ifndef ESTADO_RUTA_H
#define ESTADO_RUTA_H

#include <vector>

using namespace std;

// Tipo de vehículo asignado a cada ruta.
enum class TipoVehiculo {
    CAMION,
    FURGONETA
};

// Coordenadas 2D. Solo se usan al construir las matrices al cargar datos
// y para visualizar el resultado. NO se leen durante el bucle de SA.
struct Coord {
    double x;
    double y;
};

// Cliente: punto de demanda + retornables. NO se visita directamente; el
// camión va a una de sus paradas_cercanas, y desde allí lo sirve.
struct Cliente {
    int id;

    // Intervalo de horas en que el cliente está disponible para recibir.
    int hora_ini, hora_fin;

    // Volumen que el cliente quiere recoger en esta estación (lo que el camión entrega).
    double volumen_recoger;

    // Volumen que el cliente quiere devolver en esta estación (retornables).
    double volumen_devolver;

    // IDs de paradas físicas desde las que se puede servir a este cliente.
    // Una estación puede tener varias paradas posibles.
    vector<int> paradas_cercanas;
};

// Parada física: nodo del recorrido. Lo que el camión visita realmente.
// Una parada puede dar servicio a varios clientes a la vez.
// Hay más paradas que clientes: la VRP elige un subconjunto suficiente
// para cubrir a todos los clientes.
struct Parada {
    int id;

    // Coordenadas. Solo se usan al construir las matrices al cargar datos
    // y para visualizar el resultado. NO se leen durante el bucle de SA.
    Coord pos;

    vector<int> clientes_servidos;
};

// Datos fijos del recurso "camión" (no cambian durante el SA).
// La posición en DatosProblema::camiones indica el id del camión.
struct Camion {
    TipoVehiculo tipo;
    double capacidad_volumen;

    // Número de palets físicos del camión (1..8). El SA-ruta no lo usa: lo
    // consume el SA-distribución para dimensionar el layout 3D. Lo guardamos
    // aquí para que viaje en el JSON de salida y el orquestador lo lea.
    int n_palets = 0;

    // Hora a la que el camión sale del depósito (mismas unidades que Cliente::hora_ini).
    int hora_inicio;
};

// Estado mutable del recorrido de un camión durante el SA.
// La posición en EstadoRuta::rutas indica el id del camión.
//
// IMPORTANTE: las cachés escalares (total_*) y los vectores per-segment se
// mantienen sincronizados con `paradas` por recalcular_ruta() y por los
// operadores de op_ruta.cc. Cualquier mutación de `paradas` debe ir seguida
// de una llamada que actualice las cachés.
struct RutaCamion {
    // Secuencia de paradas físicas visitadas, en orden. Cada entrada es un
    // id de Parada (índice en DatosProblema::paradas).
    vector<int> paradas;

    // Para cada visita, conjunto de clientes que se atienden ahí.
    // Invariantes:
    //   - clientes_atendidos.size() == paradas.size()
    //   - clientes_atendidos[i] ⊆ clientes_servidos de la parada paradas[i]
    //   - cada cliente aparece en a lo sumo UNA visita de TODO el estado
    //     (sin doble servicio entre rutas o dentro de la misma)
    // Esto permite usar una parada para atender solo un subconjunto de los
    // clientes que en teoría podría cubrir, dejando los demás libres para que
    // los cubra otra parada en otra ruta.
    vector<vector<int>> clientes_atendidos;

    // Cachés escalares per-ruta. Mantenidas por recalcular_ruta y por los
    // operadores. Son las únicas que lee calcular_coste → O(num_camiones).
    double total_distancia      = 0.0;
    double total_carga_inicial  = 0.0;
    double total_pico_volumen   = 0.0;
    double total_retraso        = 0.0;
};

// Datos inmutables del problema.
// Las matrices de distancia y tiempo vienen precalculadas ENTRE PARADAS.
// Acceso lineal: idx = i * paradas.size() + j.
// El depósito se trata aparte vía dist_deposito / tiempo_deposito (cada
// camión sale del depósito hacia su primera parada y vuelve al depósito
// tras la última).
struct DatosProblema {
    // Posición del centro logístico (depósito). La coordenada solo se usa
    // para visualización; el SA consume directamente los vectores de
    // distancia/tiempo de abajo.
    Coord deposito;

    vector<Cliente> clientes;
    vector<Camion> camiones;
    vector<Parada> paradas;

    vector<double> matriz_distancia;   // M x M con M = paradas.size()
    vector<double> matriz_tiempo;

    // Tramo depósito ↔ parada i. Tamaño = paradas.size().
    // Asumimos simetría depósito→parada == parada→depósito.
    vector<double> dist_deposito;
    vector<double> tiempo_deposito;
};

// Solución completa de la VRP. La posición del vector indica el id del camión.
//
// IMPORTANTE: `atendido_ruta` es un cache global por cliente que evita que
// los operadores tengan que recorrer todo el estado para chequear solapamiento
// o localizar a un cliente. Lo mantienen los operadores tras cada movimiento
// aceptado. Antes de usar operadores hay que llamar a `inicializar_estado`.
struct EstadoRuta {
    vector<RutaCamion> rutas;

    // atendido_ruta[c] = índice del camión que atiende al cliente c, o -1
    // si nadie lo atiende. Tamaño = datos.clientes.size().
    vector<int> atendido_ruta;
};

// =============================================================================
// Helpers sobre RutaCamion (implementados en estado_ruta.cc).
// =============================================================================

// Recalcula los 4 escalares cacheados en una sola pasada O(n_ruta).
// Llamar tras cualquier mutación de `paradas` o `clientes_atendidos`.
void recalcular_ruta(RutaCamion& ruta,
                     const DatosProblema& datos,
                     const Camion& camion,
                     double minutos_por_volumen);

// Pone los 4 escalares a 0 (estado de ruta sin visitas).
void limpiar_cachés(RutaCamion& ruta);

// Comprobación rápida usando la caché total_pico_volumen (no recorre la ruta).
bool factible_capacidad(const RutaCamion& ruta, const Camion& camion);

// Construye `estado.atendido_ruta` desde cero recorriendo todas las rutas y
// sus clientes_atendidos. Llamar UNA vez tras crear EstadoRuta a mano (p. ej.
// desde la heurística inicial) y antes de usar operadores. A partir de ahí el
// cache lo mantienen los operadores.
void inicializar_estado(EstadoRuta& estado, const DatosProblema& datos);

#endif
