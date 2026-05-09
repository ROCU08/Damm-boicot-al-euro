#!/usr/bin/env python3
"""
Visualizador de resultados VRP.

Lee un JSON producido por `exportar_json` y genera dos PNGs:
  - mapa.png       : mapa-grafo de las rutas con paradas y clientes
  - capacidad.png  : utilización de capacidad por camión

Uso:
    python visualizar.py salida.json --output viz/output/
"""

import argparse
import json
import os
import sys

import matplotlib.pyplot as plt
import matplotlib.cm as cm
import numpy as np


def cargar(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def imprimir_resumen(datos_json):
    coste = datos_json["coste_total"]
    rutas = datos_json["solucion"]["rutas"]
    no_serv = datos_json["solucion"]["no_servidos"]
    n_clientes = len(datos_json["datos"]["clientes"])
    n_paradas = len(datos_json["datos"]["paradas"])
    n_camiones = len(datos_json["datos"]["camiones"])

    print(f"Coste total : {coste:.2f}")
    print(f"Camiones    : {n_camiones}")
    print(f"Paradas     : {n_paradas} totales")
    print(f"Clientes    : {n_clientes} totales")
    print(f"No servidos : {len(no_serv)} ({no_serv})")
    print()
    print(f"{'Camión':<8}{'Paradas':<10}{'Distancia':<12}{'Carga inic':<12}{'Pico vol':<10}{'Retraso':<10}")
    for r in rutas:
        print(
            f"{r['camion_id']:<8}{len(r['paradas']):<10}"
            f"{r['total_distancia']:<12.2f}{r['total_carga_inicial']:<12.2f}"
            f"{r['total_pico_volumen']:<10.2f}{r['total_retraso']:<10.2f}"
        )
    print()


# -----------------------------------------------------------------------------
# Plot 1: mapa-grafo de rutas
# -----------------------------------------------------------------------------

def plot_mapa(json_data, ruta_salida):
    paradas = json_data["datos"]["paradas"]
    rutas = json_data["solucion"]["rutas"]

    # Coordenadas indexadas por id de parada.
    pos_x = {p["id"]: p["x"] for p in paradas}
    pos_y = {p["id"]: p["y"] for p in paradas}

    # Conjunto de paradas usadas en alguna ruta.
    usadas = set()
    for r in rutas:
        usadas.update(r["paradas"])

    fig, ax = plt.subplots(figsize=(10, 8))

    # Paradas no usadas: gris claro de fondo.
    no_usadas_x = [pos_x[p["id"]] for p in paradas if p["id"] not in usadas]
    no_usadas_y = [pos_y[p["id"]] for p in paradas if p["id"] not in usadas]
    if no_usadas_x:
        ax.scatter(no_usadas_x, no_usadas_y, c="lightgray", s=40,
                   marker="o", zorder=1, label="Parada no usada")

    # Rutas: cada una con un color de la paleta tab10.
    cmap = cm.get_cmap("tab10")
    for i, r in enumerate(rutas):
        if not r["paradas"]:
            continue
        color = cmap(i % 10)
        xs = [pos_x[p] for p in r["paradas"]]
        ys = [pos_y[p] for p in r["paradas"]]

        # Polilínea conectando las paradas consecutivas.
        ax.plot(xs, ys, "-", color=color, linewidth=2, zorder=2,
                label=f"Camión {r['camion_id']} (d={r['total_distancia']:.1f})")

        # Puntos de las paradas usadas por esta ruta.
        ax.scatter(xs, ys, c=[color], s=120, edgecolors="black",
                   linewidths=0.5, zorder=3)

        # Anotación con orden de visita.
        for orden, (x, y) in enumerate(zip(xs, ys), start=1):
            ax.annotate(str(orden), (x, y), textcoords="offset points",
                        xytext=(7, 7), fontsize=8, color="black", zorder=4)

        # Etiqueta del camión sobre la primera parada.
        ax.annotate(f"Camión {r['camion_id']}",
                    (xs[0], ys[0]), textcoords="offset points",
                    xytext=(10, -14), fontsize=10, fontweight="bold",
                    color=color, zorder=5)

    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.set_title("Mapa de rutas")
    ax.legend(loc="best", fontsize=8)
    ax.grid(True, alpha=0.3)
    ax.set_aspect("equal", adjustable="datalim")

    fig.tight_layout()
    fig.savefig(ruta_salida, dpi=140)
    plt.close(fig)
    print(f"Guardado: {ruta_salida}")


# -----------------------------------------------------------------------------
# Plot 2: utilización de capacidad por camión
# -----------------------------------------------------------------------------

def plot_capacidad(json_data, ruta_salida):
    rutas = json_data["solucion"]["rutas"]
    camiones = json_data["datos"]["camiones"]

    n = len(rutas)
    if n == 0:
        return

    fig, ax = plt.subplots(figsize=(10, 0.8 * n + 2))

    y_pos = np.arange(n)
    cargas_iniciales = [r["total_carga_inicial"] for r in rutas]
    picos = [r["total_pico_volumen"] for r in rutas]
    capacidades = [camiones[r["camion_id"]]["capacidad_volumen"] for r in rutas]

    # Pico (rosa) en el fondo, carga inicial (azul claro) encima.
    ax.barh(y_pos, picos, color="#ff9eb5", edgecolor="black",
            linewidth=0.5, label="Pico volumen")
    ax.barh(y_pos, cargas_iniciales, color="#9ec5ff", edgecolor="black",
            linewidth=0.5, label="Carga inicial", height=0.5)

    # Marcas verticales rojas en la capacidad de cada camión.
    for i, cap in enumerate(capacidades):
        ax.plot([cap, cap], [i - 0.45, i + 0.45], color="red",
                linewidth=2, zorder=5)

    # Anotación con porcentaje de utilización (pico / capacidad).
    for i, (pico, cap) in enumerate(zip(picos, capacidades)):
        pct = (pico / cap * 100.0) if cap > 0 else 0.0
        ax.annotate(f"{pct:.0f}%",
                    (max(pico, cap) * 1.02, i),
                    va="center", fontsize=9)

    ax.set_yticks(y_pos)
    ax.set_yticklabels([f"Camión {r['camion_id']}" for r in rutas])
    ax.set_xlabel("Volumen")
    ax.set_title("Carga inicial vs pico de volumen vs capacidad (línea roja)")
    ax.legend(loc="lower right", fontsize=9)
    ax.grid(True, axis="x", alpha=0.3)

    fig.tight_layout()
    fig.savefig(ruta_salida, dpi=140)
    plt.close(fig)
    print(f"Guardado: {ruta_salida}")


# -----------------------------------------------------------------------------
# Entry point
# -----------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Visualiza un resultado VRP.")
    parser.add_argument("input", help="Path al JSON producido por exportar_json")
    parser.add_argument("--output", default="viz/output/",
                        help="Directorio de salida para los PNGs")
    args = parser.parse_args()

    if not os.path.exists(args.input):
        print(f"No existe el fichero: {args.input}", file=sys.stderr)
        sys.exit(1)

    os.makedirs(args.output, exist_ok=True)
    data = cargar(args.input)

    imprimir_resumen(data)

    plot_mapa(data, os.path.join(args.output, "mapa.png"))
    plot_capacidad(data, os.path.join(args.output, "capacidad.png"))


if __name__ == "__main__":
    main()
