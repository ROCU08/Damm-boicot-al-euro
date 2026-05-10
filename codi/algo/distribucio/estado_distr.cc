#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <functional>

using namespace std;

struct Item {
    uint16_t material_id;
    uint16_t cliente_id;
    bool es_barril;

    bool vacio() const { return material_id == 0; }
};

struct Pos {
    uint8_t palet;
    uint8_t piso;
    uint8_t fila;
    uint8_t col;

    bool operator==(const Pos& o) const {
        return palet == o.palet && piso == o.piso && fila == o.fila && col == o.col;
    }
};

struct PosHash {
    size_t operator()(const Pos& p) const {
        return (p.palet << 24) | (p.piso << 16) | (p.fila << 8) | p.col;
    }
};

struct Solucion {
    static constexpr int MAX_PALETS = 8;
    static constexpr int PISOS = 6;
    static constexpr int FILAS = 5;
    static constexpr int COLS = 2;

    Item layout[MAX_PALETS][PISOS][FILAS][COLS];
    int n_palets;

    unordered_map<uint16_t, vector<Pos>> por_material;
    unordered_map<uint16_t, vector<Pos>> por_cliente;

    Solucion() : n_palets(0) {
        memset(layout, 0, sizeof(layout));
    }

    const Item& get(Pos p) const {
        return layout[p.palet][p.piso][p.fila][p.col];
    }

    Item& get_mut(Pos p) {
        return layout[p.palet][p.piso][p.fila][p.col];
    }

    void agregar_a_indice(uint16_t id, Pos p, unordered_map<uint16_t, vector<Pos>>& indice) {
        indice[id].push_back(p);
    }

    void quitar_de_indice(uint16_t id, Pos p, unordered_map<uint16_t, vector<Pos>>& indice) {
        auto it = indice.find(id);
        if (it == indice.end()) return;
        auto& vec = it->second;
        vec.erase(remove(vec.begin(), vec.end(), p), vec.end());
        if (vec.empty()) indice.erase(it);
    }

    void colocar(Pos p, Item item) {
        Item& slot = get_mut(p);
        if (!slot.vacio()) quitar(p);
        slot = item;
        if (!item.vacio()) {
            agregar_a_indice(item.material_id, p, por_material);
            agregar_a_indice(item.cliente_id, p, por_cliente);
        }
    }

    void quitar(Pos p) {
        Item& slot = get_mut(p);
        if (slot.vacio()) return;
        quitar_de_indice(slot.material_id, p, por_material);
        quitar_de_indice(slot.cliente_id, p, por_cliente);
        slot = {0, 0, false};
    }

    void swap_items(Pos a, Pos b) {
        Item& ia = get_mut(a);
        Item& ib = get_mut(b);

        if (!ia.vacio()) {
            quitar_de_indice(ia.material_id, a, por_material);
            quitar_de_indice(ia.cliente_id, a, por_cliente);
        }
        if (!ib.vacio()) {
            quitar_de_indice(ib.material_id, b, por_material);
            quitar_de_indice(ib.cliente_id, b, por_cliente);
        }

        std::swap(ia, ib);

        if (!ia.vacio()) {
            agregar_a_indice(ia.material_id, a, por_material);
            agregar_a_indice(ia.cliente_id, a, por_cliente);
        }
        if (!ib.vacio()) {
            agregar_a_indice(ib.material_id, b, por_material);
            agregar_a_indice(ib.cliente_id, b, por_cliente);
        }
    }
};
