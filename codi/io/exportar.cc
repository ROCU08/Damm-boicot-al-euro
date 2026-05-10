#include "exportar.h"
#include <fstream>
#include <sstream>

using namespace std;

// =============================================================================
// Mini-helpers para serialización JSON manual.
// Sin dependencias externas: concatenación con ostringstream.
// =============================================================================

namespace {

string tipo_a_string(TipoVehiculo t) {
    return t == TipoVehiculo::CAMION ? "CAMION" : "FURGONETA";
}

// Junta los elementos de [a, b) con separador, aplicando `f` a cada uno.
// Útil para listas JSON (`a, b, c`).
template <typename It, typename F>
string join(It a, It b, const string& sep, F f) {
    ostringstream s;
    bool primero = true;
    for (auto it = a; it != b; ++it) {
        if (!primero) s << sep;
        s << f(*it);
        primero = false;
    }
    return s.str();
}

string join_ints(const vector<int>& v) {
    return join(v.begin(), v.end(), ",", [](int x){
        return to_string(x);
    });
}

string clientes_atendidos_json(const vector<vector<int>>& ca) {
    ostringstream s;
    s << "[";
    bool primero = true;
    for (const auto& v : ca) {
        if (!primero) s << ",";
        s << "[" << join_ints(v) << "]";
        primero = false;
    }
    s << "]";
    return s.str();
}

string cliente_json(const Cliente& c) {
    ostringstream s;
    s << "{\"id\":" << c.id
      << ",\"hora_ini\":" << c.hora_ini
      << ",\"hora_fin\":" << c.hora_fin
      << ",\"volumen_recoger\":" << c.volumen_recoger
      << ",\"volumen_devolver\":" << c.volumen_devolver
      << "}";
    return s.str();
}

string parada_json(const Parada& p) {
    ostringstream s;
    s << "{\"id\":" << p.id
      << ",\"x\":" << p.pos.x
      << ",\"y\":" << p.pos.y
      << ",\"clientes_servidos\":[" << join_ints(p.clientes_servidos) << "]"
      << "}";
    return s.str();
}

string camion_json(const Camion& c) {
    ostringstream s;
    s << "{\"tipo\":\"" << tipo_a_string(c.tipo) << "\""
      << ",\"capacidad_volumen\":" << c.capacidad_volumen
      << ",\"n_palets\":" << c.n_palets
      << ",\"hora_inicio\":" << c.hora_inicio
      << "}";
    return s.str();
}

string ruta_json(int camion_id, const RutaCamion& ruta) {
    ostringstream s;
    s << "{\"camion_id\":" << camion_id
      << ",\"paradas\":[" << join_ints(ruta.paradas) << "]"
      << ",\"clientes_atendidos\":" << clientes_atendidos_json(ruta.clientes_atendidos)
      << ",\"total_distancia\":" << ruta.total_distancia
      << ",\"total_carga_inicial\":" << ruta.total_carga_inicial
      << ",\"total_pico_volumen\":" << ruta.total_pico_volumen
      << ",\"total_retraso\":" << ruta.total_retraso
      << "}";
    return s.str();
}

vector<int> calcular_no_servidos(const EstadoRuta& estado, const DatosProblema& datos) {
    int n = (int)datos.clientes.size();
    vector<bool> cubierto(n, false);
    for (const RutaCamion& r : estado.rutas) {
        for (const auto& visita : r.clientes_atendidos) {
            for (int cid : visita) cubierto[cid] = true;
        }
    }
    vector<int> resultado;
    for (int i = 0; i < n; ++i) {
        if (!cubierto[i]) resultado.push_back(i);
    }
    return resultado;
}

}  // namespace

// =============================================================================
// Función pública
// =============================================================================

bool exportar_json(const EstadoRuta& estado,
                   const DatosProblema& datos,
                   double coste_total,
                   const string& path) {
    ofstream f(path);
    if (!f.is_open()) return false;

    // Configurar precisión decimal razonable.
    f.setf(ios::fixed);
    f.precision(4);

    f << "{\n";
    f << "  \"coste_total\":" << coste_total << ",\n";

    // datos
    f << "  \"datos\":{\n";

    f << "    \"deposito\":{\"x\":" << datos.deposito.x
      << ",\"y\":" << datos.deposito.y << "},\n";

    f << "    \"clientes\":[";
    f << join(datos.clientes.begin(), datos.clientes.end(), ",", cliente_json);
    f << "],\n";

    f << "    \"paradas\":[";
    f << join(datos.paradas.begin(), datos.paradas.end(), ",", parada_json);
    f << "],\n";

    f << "    \"camiones\":[";
    f << join(datos.camiones.begin(), datos.camiones.end(), ",", camion_json);
    f << "]\n";

    f << "  },\n";

    // solucion
    f << "  \"solucion\":{\n";

    f << "    \"rutas\":[";
    bool primero = true;
    for (int k = 0; k < (int)estado.rutas.size(); ++k) {
        if (!primero) f << ",";
        f << ruta_json(k, estado.rutas[k]);
        primero = false;
    }
    f << "],\n";

    vector<int> no_servidos = calcular_no_servidos(estado, datos);
    f << "    \"no_servidos\":[" << join_ints(no_servidos) << "]\n";

    f << "  }\n";
    f << "}\n";

    return true;
}
