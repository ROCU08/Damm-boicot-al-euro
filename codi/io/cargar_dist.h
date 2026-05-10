#ifndef CARGAR_DIST_H
#define CARGAR_DIST_H

// Loader header-only para el RutaInputDist producido por
// pipeline/items_por_camion.py.
//
// Se incluye desde distribucio_main.cc, que también incluye estado_distr.cc,
// h_distr.cc y op_distr.cc en un único TU. Por eso es header-only.

#include "json_parser.h"
#include "../algo/distribucio/estado_distr.cc"   // por Item, Solucion, etc.

#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

struct SaDistParams {
    double temp_ini = 1000.0;
    double temp_fin = 0.01;
    double cooling  = 0.95;
    int    iters_temp = 100;
};

struct RutaInputDist {
    int camion_id = -1;
    int n_palets  = 0;
    std::vector<uint16_t> orden_ruta;   // cliente_ids en orden de visita
    std::vector<Item>     items;        // ya expandidos (1 ítem = 1 slot del camión)
    // material_id (uint16) -> código humano (ej. "ED13"). Se preserva para el
    // exporter que serializa la Solucion final.
    std::unordered_map<uint16_t, std::string> mat_codigos;
    SaDistParams params;
};

// Necesita el mapping de códigos para rellenarlo durante el parse de items.
inline Item parse_item(jsonp::Parser& p,
                      std::unordered_map<uint16_t, std::string>& mat_codigos) {
    Item it{0, 0, false};
    std::string codigo;
    p.parse_object_fields([&](const std::string& k) {
        if (k == "material_id") it.material_id = static_cast<uint16_t>(p.parse_number());
        else if (k == "cliente_id") it.cliente_id = static_cast<uint16_t>(p.parse_number());
        else if (k == "es_barril") it.es_barril = p.parse_bool();
        else if (k == "material_codigo") codigo = p.parse_string();
        else p.skip_value();
    });
    if (!codigo.empty() && mat_codigos.find(it.material_id) == mat_codigos.end()) {
        mat_codigos[it.material_id] = codigo;
    }
    return it;
}

inline SaDistParams parse_sa_params(jsonp::Parser& p) {
    SaDistParams sp;
    p.parse_object_fields([&](const std::string& k) {
        if (k == "temp_ini") sp.temp_ini = p.parse_number();
        else if (k == "temp_fin") sp.temp_fin = p.parse_number();
        else if (k == "cooling") sp.cooling = p.parse_number();
        else if (k == "iters_temp") sp.iters_temp = static_cast<int>(p.parse_number());
        else p.skip_value();
    });
    return sp;
}

inline bool cargar_ruta_input_dist(const std::string& path, RutaInputDist& out) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::stringstream buf;
    buf << f.rdbuf();
    std::string src = buf.str();

    jsonp::Parser p(src);
    p.parse_object_fields([&](const std::string& k) {
        if (k == "camion_id") {
            out.camion_id = static_cast<int>(p.parse_number());
        } else if (k == "n_palets") {
            out.n_palets = static_cast<int>(p.parse_number());
        } else if (k == "orden_ruta") {
            p.parse_array_elems([&](){
                out.orden_ruta.push_back(static_cast<uint16_t>(p.parse_number()));
            });
        } else if (k == "items") {
            p.parse_array_elems([&](){
                out.items.push_back(parse_item(p, out.mat_codigos));
            });
        } else if (k == "sa_params") {
            out.params = parse_sa_params(p);
        } else {
            p.skip_value();
        }
    });
    return true;
}

#endif
