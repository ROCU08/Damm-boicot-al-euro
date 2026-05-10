#ifndef EXPORTAR_DIST_H
#define EXPORTAR_DIST_H

// Exporter header-only de Solucion (SA-distribución) a JSON.
// Conforme a pipeline/contratos.py:DistribucionOutput.
//
// Header-only para coherencia con cargar_dist.h: distribucio_main.cc lo
// incluye en un único TU.

#include "../algo/distribucio/estado_distr.cc"   // Solucion, Item, Pos
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

struct ComponentesFitness {
    double fragmentacion_producto = 0.0;
    double fragmentacion_cliente  = 0.0;
    double accesibilidad          = 0.0;
};

inline bool exportar_distribucion(const Solucion& sol,
                                  int camion_id,
                                  double fitness,
                                  const ComponentesFitness& comp,
                                  const std::unordered_map<uint16_t, std::string>& mat_codigos,
                                  const std::string& path) {
    std::ofstream f(path);
    if (!f.is_open()) return false;

    f.setf(std::ios::fixed);
    f.precision(4);

    f << "{\n";
    f << "  \"camion_id\":" << camion_id << ",\n";
    f << "  \"n_palets\":"  << sol.n_palets << ",\n";
    f << "  \"fitness\":"   << fitness << ",\n";
    f << "  \"componentes\":{"
      << "\"fragmentacion_producto\":" << comp.fragmentacion_producto
      << ",\"fragmentacion_cliente\":" << comp.fragmentacion_cliente
      << ",\"accesibilidad\":"         << comp.accesibilidad
      << "},\n";

    f << "  \"layout\":[";
    bool primero = true;
    for (int pal = 0; pal < sol.n_palets; ++pal) {
        for (int piso = 0; piso < Solucion::PISOS; ++piso) {
            for (int fila = 0; fila < Solucion::FILAS; ++fila) {
                for (int col = 0; col < Solucion::COLS; ++col) {
                    const Item& it = sol.layout[pal][piso][fila][col];
                    if (it.vacio()) continue;
                    if (!primero) f << ",";
                    primero = false;
                    auto mc_it = mat_codigos.find(it.material_id);
                    std::string mc = (mc_it != mat_codigos.end()) ? mc_it->second : "";
                    f << "{\"palet\":" << pal
                      << ",\"piso\":" << piso
                      << ",\"fila\":" << fila
                      << ",\"col\":"  << col
                      << ",\"material_id\":" << it.material_id
                      << ",\"material_codigo\":\"" << mc << "\""
                      << ",\"cliente_id\":" << it.cliente_id
                      << ",\"es_barril\":" << (it.es_barril ? "true" : "false")
                      << "}";
                }
            }
        }
    }
    f << "]\n";
    f << "}\n";

    return true;
}

#endif
