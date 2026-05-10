#include "mapa.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mapa {

namespace {

// Parser CSV mínimo que respeta comillas dobles. Suficiente para nuestros outputs
// (sin saltos de línea dentro de campos, escape "" para comilla literal).
std::vector<std::string> parse_csv_line(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool in_quotes = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (in_quotes) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                cur.push_back('"');
                ++i;
            } else if (c == '"') {
                in_quotes = false;
            } else {
                cur.push_back(c);
            }
        } else {
            if (c == ',') {
                out.push_back(std::move(cur));
                cur.clear();
            } else if (c == '"' && cur.empty()) {
                in_quotes = true;
            } else if (c == '\r') {
                // ignorar
            } else {
                cur.push_back(c);
            }
        }
    }
    out.push_back(std::move(cur));
    return out;
}

std::ifstream open_or_throw(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        throw std::runtime_error("mapa: no se pudo abrir " + path);
    }
    return f;
}

float parse_float_or_neg1(const std::string& s) {
    if (s.empty()) return -1.0f;
    try {
        return std::stof(s);
    } catch (...) {
        return -1.0f;
    }
}

uint64_t parse_uint64(const std::string& s) {
    return static_cast<uint64_t>(std::stoull(s));
}

std::size_t header_index(const std::vector<std::string>& header, const std::string& name) {
    for (std::size_t i = 0; i < header.size(); ++i) {
        if (header[i] == name) return i;
    }
    throw std::runtime_error("mapa: columna no encontrada: " + name);
}

}  // namespace

void Mapa::cargar(const std::string& dir) {
    cliente_to_idx_.clear();
    snaps_.clear();
    publicos_.clear();
    dist_.clear();
    time_.clear();
    n_ = 0;

    // 1. cliente_index.csv: idx, Destino_ID, lat, lon
    {
        std::ifstream f = open_or_throw(dir + "/cliente_index.csv");
        std::string line;
        if (!std::getline(f, line)) {
            throw std::runtime_error("mapa: cliente_index.csv vacío");
        }
        auto header = parse_csv_line(line);
        std::size_t i_idx = header_index(header, "idx");
        std::size_t i_id  = header_index(header, "Destino_ID");
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            auto cols = parse_csv_line(line);
            std::size_t idx = static_cast<std::size_t>(std::stoul(cols.at(i_idx)));
            uint64_t cid    = parse_uint64(cols.at(i_id));
            cliente_to_idx_[cid] = idx;
            n_ = std::max(n_, idx + 1);
        }
    }

    snaps_.assign(n_, LoadingPoint{});
    dist_.assign(n_ * n_, -1.0f);
    time_.assign(n_ * n_, -1.0f);

    // 2. loading_points_per_client.csv: enlazado por dirección, no por Destino_ID.
    //    Por simplicidad, buscamos el cliente por proximidad de coords del index.
    //    En esta primera versión rellenamos lat/lon de snap = coords del index si no hay match.
    {
        std::ifstream f = open_or_throw(dir + "/cliente_index.csv");
        std::string line;
        std::getline(f, line); // header
        auto header = parse_csv_line(line);
        std::size_t i_idx = header_index(header, "idx");
        std::size_t i_lat = header_index(header, "lat");
        std::size_t i_lon = header_index(header, "lon");
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            auto cols = parse_csv_line(line);
            std::size_t idx = static_cast<std::size_t>(std::stoul(cols.at(i_idx)));
            snaps_.at(idx).lat = std::stod(cols.at(i_lat));
            snaps_.at(idx).lon = std::stod(cols.at(i_lon));
            snaps_.at(idx).tipo = "snap_cliente";
            snaps_.at(idx).hgv_ok = true;  // se sobreescribe abajo si hay info real
        }
    }
    // Nota: el join fino loading_points_per_client.csv ↔ cliente_index requiere mapear
    // direccion_normalizada → Destino_ID, lo cual hoy no está en los CSV. Cuando esa
    // tabla exista, aquí se sobreescriben snaps_[idx] con datos reales (snap_dist, hgv_ok).

    // 3. dist_matrix.csv y time_matrix.csv: misma forma. Cabecera = "" + 0..N-1.
    auto load_matrix = [&](const std::string& path, std::vector<float>& m) {
        std::ifstream f = open_or_throw(path);
        std::string line;
        if (!std::getline(f, line)) {
            throw std::runtime_error("mapa: " + path + " vacío");
        }
        std::size_t row = 0;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            auto cols = parse_csv_line(line);
            // cols[0] = idx fila; cols[1..N] = valores
            if (cols.size() < n_ + 1) {
                throw std::runtime_error("mapa: matriz mal formada en fila " + std::to_string(row));
            }
            for (std::size_t j = 0; j < n_; ++j) {
                m[row * n_ + j] = parse_float_or_neg1(cols[j + 1]);
            }
            ++row;
        }
        if (row != n_) {
            throw std::runtime_error("mapa: " + path + " tiene " + std::to_string(row) +
                                     " filas, esperaba " + std::to_string(n_));
        }
    };
    load_matrix(dir + "/dist_matrix.csv", dist_);
    load_matrix(dir + "/time_matrix.csv", time_);

    // 4. loading_points_public.csv: osm_id, lat, lon, tipo, nombre, capacidad
    {
        std::ifstream f = open_or_throw(dir + "/loading_points_public.csv");
        std::string line;
        if (!std::getline(f, line)) {
            return;  // archivo vacío es válido (puede no haber zonas públicas)
        }
        auto header = parse_csv_line(line);
        std::size_t i_lat = header_index(header, "lat");
        std::size_t i_lon = header_index(header, "lon");
        std::size_t i_tipo = header_index(header, "tipo");
        std::size_t i_nom = header_index(header, "nombre");
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            auto cols = parse_csv_line(line);
            LoadingPoint p;
            p.lat = std::stod(cols.at(i_lat));
            p.lon = std::stod(cols.at(i_lon));
            p.tipo = cols.at(i_tipo);
            p.nombre = cols.at(i_nom);
            p.hgv_ok = true;  // si está en este catálogo, asumimos transitable a camión
            publicos_.push_back(std::move(p));
        }
    }
}

float Mapa::dist_m(uint64_t cliente_i, uint64_t cliente_j) const {
    auto it_i = cliente_to_idx_.find(cliente_i);
    auto it_j = cliente_to_idx_.find(cliente_j);
    if (it_i == cliente_to_idx_.end() || it_j == cliente_to_idx_.end()) {
        return -1.0f;
    }
    return dist_[it_i->second * n_ + it_j->second];
}

float Mapa::time_s(uint64_t cliente_i, uint64_t cliente_j) const {
    auto it_i = cliente_to_idx_.find(cliente_i);
    auto it_j = cliente_to_idx_.find(cliente_j);
    if (it_i == cliente_to_idx_.end() || it_j == cliente_to_idx_.end()) {
        return -1.0f;
    }
    return time_[it_i->second * n_ + it_j->second];
}

const LoadingPoint& Mapa::snap_cliente(uint64_t cliente_id) const {
    auto it = cliente_to_idx_.find(cliente_id);
    if (it == cliente_to_idx_.end()) {
        return sentinel_;
    }
    return snaps_[it->second];
}

const std::vector<LoadingPoint>& Mapa::puntos_carga_publicos() const {
    return publicos_;
}

}  // namespace mapa
