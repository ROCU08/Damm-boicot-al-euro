#include "cargar_json.h"
#include "json_parser.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace std;
using jsonp::Parser;

// =============================================================================
// Builders del esquema DatosProblema (compatible con
// pipeline/contratos.py:DatosProblema).
// =============================================================================

namespace {

Coord parse_coord(Parser& p) {
    Coord c{0, 0};
    p.parse_object_fields([&](const string& k) {
        if (k == "x") c.x = p.parse_number();
        else if (k == "y") c.y = p.parse_number();
        else p.skip_value();
    });
    return c;
}

vector<int> parse_int_array(Parser& p) {
    vector<int> v;
    p.parse_array_elems([&](){ v.push_back(static_cast<int>(p.parse_number())); });
    return v;
}

vector<double> parse_double_array(Parser& p) {
    vector<double> v;
    p.parse_array_elems([&](){ v.push_back(p.parse_number()); });
    return v;
}

Cliente parse_cliente(Parser& p) {
    Cliente c;
    c.id = -1;
    c.hora_ini = 0;
    c.hora_fin = 0;
    c.volumen_recoger = 0.0;
    c.volumen_devolver = 0.0;
    p.parse_object_fields([&](const string& k) {
        if (k == "id") c.id = static_cast<int>(p.parse_number());
        else if (k == "hora_ini") c.hora_ini = static_cast<int>(p.parse_number());
        else if (k == "hora_fin") c.hora_fin = static_cast<int>(p.parse_number());
        else if (k == "volumen_recoger") c.volumen_recoger = p.parse_number();
        else if (k == "volumen_devolver") c.volumen_devolver = p.parse_number();
        else if (k == "paradas_cercanas") c.paradas_cercanas = parse_int_array(p);
        else p.skip_value();   // destino_id_externo, nombre, ...
    });
    return c;
}

Parada parse_parada(Parser& p) {
    Parada par;
    par.id = -1;
    par.pos = {0, 0};
    p.parse_object_fields([&](const string& k) {
        if (k == "id") par.id = static_cast<int>(p.parse_number());
        else if (k == "pos") par.pos = parse_coord(p);
        else if (k == "clientes_servidos") par.clientes_servidos = parse_int_array(p);
        else p.skip_value();
    });
    return par;
}

Camion parse_camion(Parser& p) {
    Camion c;
    c.tipo = TipoVehiculo::CAMION;
    c.capacidad_volumen = 0.0;
    c.n_palets = 0;
    c.hora_inicio = 0;
    p.parse_object_fields([&](const string& k) {
        if (k == "tipo") {
            string s = p.parse_string();
            c.tipo = (s == "FURGONETA") ? TipoVehiculo::FURGONETA : TipoVehiculo::CAMION;
        } else if (k == "capacidad_volumen") {
            c.capacidad_volumen = p.parse_number();
        } else if (k == "n_palets") {
            c.n_palets = static_cast<int>(p.parse_number());
        } else if (k == "hora_inicio") {
            c.hora_inicio = static_cast<int>(p.parse_number());
        } else {
            p.skip_value();   // id, etc.
        }
    });
    return c;
}

}  // namespace

// =============================================================================
// API pública
// =============================================================================

bool cargar_datos_problema(const string& path, DatosProblema& out) {
    ifstream f(path);
    if (!f.is_open()) return false;

    stringstream buf;
    buf << f.rdbuf();
    string src = buf.str();

    Parser p(src);
    p.parse_object_fields([&](const string& k) {
        if (k == "deposito") {
            out.deposito = parse_coord(p);
        } else if (k == "clientes") {
            p.parse_array_elems([&](){ out.clientes.push_back(parse_cliente(p)); });
        } else if (k == "paradas") {
            p.parse_array_elems([&](){ out.paradas.push_back(parse_parada(p)); });
        } else if (k == "camiones") {
            p.parse_array_elems([&](){ out.camiones.push_back(parse_camion(p)); });
        } else if (k == "matriz_distancia") {
            out.matriz_distancia = parse_double_array(p);
        } else if (k == "matriz_tiempo") {
            out.matriz_tiempo = parse_double_array(p);
        } else if (k == "dist_deposito") {
            out.dist_deposito = parse_double_array(p);
        } else if (k == "tiempo_deposito") {
            out.tiempo_deposito = parse_double_array(p);
        } else {
            p.skip_value();
        }
    });

    // Sanity checks: las matrices deben tener tamaño M*M y los vectores depósito M.
    size_t M = out.paradas.size();
    if (out.matriz_distancia.size() != M * M) {
        throw runtime_error("cargar_json: matriz_distancia tiene " +
                            to_string(out.matriz_distancia.size()) + " entries, esperaba " +
                            to_string(M * M));
    }
    if (out.matriz_tiempo.size() != M * M) {
        throw runtime_error("cargar_json: matriz_tiempo tamaño incorrecto");
    }
    if (out.dist_deposito.size() != M || out.tiempo_deposito.size() != M) {
        throw runtime_error("cargar_json: vectores depósito tamaño incorrecto");
    }

    return true;
}
