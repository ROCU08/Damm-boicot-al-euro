#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace mapa {

struct LoadingPoint {
    double lat = 0.0;
    double lon = 0.0;
    std::string nombre;     // calle si es snap_cliente, name OSM si es público
    std::string tipo;       // "snap_cliente" | "loading_dock" | "parking_truck" | ...
    bool hgv_ok = false;
};

class Mapa {
public:
    // Carga las 4 fuentes desde el directorio de salida del pipeline (típicamente "mapa/out").
    // Lanza std::runtime_error si falta algún CSV o el formato es inválido.
    void cargar(const std::string& dir);

    // Lookup O(1). Devuelve -1 si la ruta no es alcanzable o algún ID es desconocido.
    float dist_m(uint64_t cliente_i, uint64_t cliente_j) const;
    float time_s(uint64_t cliente_i, uint64_t cliente_j) const;

    // Snap point del cliente (donde el camión puede parar).
    // Devuelve un LoadingPoint con hgv_ok=false y nombre vacío si el cliente no existe.
    const LoadingPoint& snap_cliente(uint64_t cliente_id) const;

    // Catálogo de zonas de carga públicas en el área (orden estable por osm_id).
    const std::vector<LoadingPoint>& puntos_carga_publicos() const;

    // Número de clientes indexados en las matrices.
    std::size_t n_clientes() const { return n_; }

private:
    std::unordered_map<uint64_t, std::size_t> cliente_to_idx_;
    std::vector<float> dist_;        // N*N row-major, metros
    std::vector<float> time_;        // N*N row-major, segundos
    std::vector<LoadingPoint> snaps_; // tamaño N, alineado con cliente_to_idx_
    std::vector<LoadingPoint> publicos_;
    LoadingPoint sentinel_;          // devuelto por snap_cliente() si el ID no existe
    std::size_t n_ = 0;
};

}  // namespace mapa
