#!/usr/bin/env python3
"""
analizar_experimentos.py
========================

Lee los CSV producidos por `damm-exp` (uno por experimento) y genera plots
PNG con matplotlib. Auto-detecta qué CSVs existen en el directorio de
entrada y solo procesa los que están.

Uso:
    python viz/analizar_experimentos.py [--input .] [--output viz/output/exp/]

Por defecto busca los CSVs en el directorio actual y vuelca PNGs a
`viz/output/exp/`. El script no rompe si falta algún experimento — solo
salta y reporta cuáles ha procesado.

Dependencias: pandas, matplotlib, numpy.
"""

import argparse
import os
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


# ============================================================================
# Estilo común
# ============================================================================

plt.rcParams.update({
    "figure.figsize": (10, 6),
    "axes.grid": True,
    "grid.alpha": 0.3,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "font.size": 10,
    "axes.titlesize": 12,
    "axes.labelsize": 10,
    "legend.fontsize": 9,
})


def _save(fig, out_path: Path):
    fig.tight_layout()
    fig.savefig(out_path, dpi=140, bbox_inches="tight")
    plt.close(fig)
    print(f"  Guardado: {out_path}")


# ============================================================================
# Exp 1 – Operadores
# Columnas: operador, seed, coste_ini, coste_fin, mejora_pct, tiempo_s, cobertura_pct
# ============================================================================

def plot_exp1(csv_path: Path, out_dir: Path):
    df = pd.read_csv(csv_path)
    operadores = df["operador"].unique().tolist()

    # 1. Box plot de mejora_pct por operador
    fig, ax = plt.subplots()
    data = [df[df["operador"] == op]["mejora_pct"].values for op in operadores]
    ax.boxplot(data, labels=operadores, showmeans=True)
    ax.set_xlabel("Operador (vecindario)")
    ax.set_ylabel("Mejora (%) sobre el greedy")
    ax.set_title("Exp 1 — Mejora del SA por tipo de operador")
    _save(fig, out_dir / "exp1_mejora_box.png")

    # 2. Bar chart tiempo medio con std
    fig, ax = plt.subplots()
    means  = [df[df["operador"] == op]["tiempo_s"].mean() for op in operadores]
    stds   = [df[df["operador"] == op]["tiempo_s"].std()  for op in operadores]
    bars = ax.bar(operadores, means, yerr=stds, capsize=5,
                  color=plt.cm.tab10.colors[:len(operadores)],
                  edgecolor="black", linewidth=0.5)
    for bar, m in zip(bars, means):
        ax.text(bar.get_x() + bar.get_width()/2, bar.get_height(),
                f"{m:.2f}s", ha="center", va="bottom", fontsize=9)
    ax.set_ylabel("Tiempo SA (s)")
    ax.set_title("Exp 1 — Tiempo medio por operador (± std)")
    _save(fig, out_dir / "exp1_tiempo_bar.png")

    # 3. Scatter mejora vs tiempo (color por operador)
    fig, ax = plt.subplots()
    for i, op in enumerate(operadores):
        sub = df[df["operador"] == op]
        ax.scatter(sub["tiempo_s"], sub["mejora_pct"],
                   label=op, color=plt.cm.tab10.colors[i],
                   alpha=0.7, edgecolor="black", linewidth=0.3)
    ax.set_xlabel("Tiempo SA (s)")
    ax.set_ylabel("Mejora (%)")
    ax.set_title("Exp 1 — Trade-off coste/tiempo por operador")
    ax.legend(title="Operador")
    _save(fig, out_dir / "exp1_scatter.png")


# ============================================================================
# Exp 2 – Inicialización
# Columnas: greedy, seed, coste_ini, coste_fin, mejora_pct, tiempo_s, cobertura_pct
# ============================================================================

def plot_exp2(csv_path: Path, out_dir: Path):
    df = pd.read_csv(csv_path)
    greedys = df["greedy"].unique().tolist()
    colors = plt.cm.tab10.colors[:len(greedys)]

    # 1. Box plot lado a lado: coste inicial y coste final
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 5))

    data_ini = [df[df["greedy"] == g]["coste_ini"].values for g in greedys]
    ax1.boxplot(data_ini, labels=greedys, showmeans=True,
                patch_artist=True,
                boxprops=dict(facecolor="lightblue"))
    ax1.set_ylabel("Coste inicial")
    ax1.set_title("Calidad del estado inicial")

    data_fin = [df[df["greedy"] == g]["coste_fin"].values for g in greedys]
    ax2.boxplot(data_fin, labels=greedys, showmeans=True,
                patch_artist=True,
                boxprops=dict(facecolor="lightgreen"))
    ax2.set_ylabel("Coste final tras SA")
    ax2.set_title("Calidad tras optimización")

    fig.suptitle("Exp 2 — Comparación de estrategias greedy")
    _save(fig, out_dir / "exp2_costes_box.png")

    # 2. Box de mejora_pct por greedy
    fig, ax = plt.subplots()
    data = [df[df["greedy"] == g]["mejora_pct"].values for g in greedys]
    ax.boxplot(data, labels=greedys, showmeans=True,
               patch_artist=True,
               boxprops=dict(facecolor="wheat"))
    ax.set_xlabel("Greedy de inicialización")
    ax.set_ylabel("Mejora (%) tras SA")
    ax.set_title("Exp 2 — Mejora relativa por estrategia greedy")
    _save(fig, out_dir / "exp2_mejora_box.png")


# ============================================================================
# Exp 3 – Grid Search SA
# Columnas: temp_ini, cooling, iters_temp, media_mejora, std_mejora,
#           media_coste_fin, std_coste_fin, media_tiempo, std_tiempo,
#           media_cobertura
# ============================================================================

def plot_exp3(csv_path: Path, out_dir: Path):
    df = pd.read_csv(csv_path)

    # 1. Heatmap por cada valor de iters_temp: media_coste_fin over (temp_ini, cooling).
    # Usamos viridis_r para que valores BAJOS de coste (mejor) salgan en amarillo
    # y valores altos (peor) en violeta oscuro.
    iters_vals = sorted(df["iters_temp"].unique())
    n = len(iters_vals)
    fig, axes = plt.subplots(1, n, figsize=(5*n, 4.5), squeeze=False)
    axes = axes[0]

    vmin = df["media_coste_fin"].min()
    vmax = df["media_coste_fin"].max()

    for ax, it in zip(axes, iters_vals):
        sub = df[df["iters_temp"] == it]
        pivot = sub.pivot(index="temp_ini", columns="cooling", values="media_coste_fin")
        im = ax.imshow(pivot.values, aspect="auto", origin="lower",
                       cmap="viridis_r", vmin=vmin, vmax=vmax)
        ax.set_xticks(range(len(pivot.columns)))
        ax.set_xticklabels([f"{c:.2f}" for c in pivot.columns])
        ax.set_yticks(range(len(pivot.index)))
        ax.set_yticklabels([f"{t:.0f}" for t in pivot.index])
        ax.set_xlabel("cooling (λ)")
        ax.set_ylabel("temp_ini (T₀)")
        ax.set_title(f"iters_temp = {it}")
        # Anotaciones dentro de las celdas: blanco sobre violeta (alto), negro sobre amarillo (bajo).
        for i in range(len(pivot.index)):
            for j in range(len(pivot.columns)):
                v = pivot.values[i, j]
                ax.text(j, i, f"{v:.0f}", ha="center", va="center",
                        color="white" if v > (vmin+vmax)/2 else "black",
                        fontsize=8)
    fig.colorbar(im, ax=axes, label="media_coste_fin (menor = mejor)", shrink=0.8)
    fig.suptitle("Exp 3 — Grid search SA: coste final medio (minimizar)")
    _save(fig, out_dir / "exp3_heatmap.png")

    # 2. Top-10 configuraciones
    top = df.nlargest(10, "media_mejora").reset_index(drop=True)
    fig, ax = plt.subplots(figsize=(11, 5))
    labels = [f"T₀={r.temp_ini:.0f}\nλ={r.cooling:.2f}\nL={r.iters_temp}"
              for r in top.itertuples()]
    bars = ax.bar(range(len(top)), top["media_mejora"],
                  yerr=top["std_mejora"], capsize=4,
                  color=plt.cm.plasma(np.linspace(0.2, 0.85, len(top))),
                  edgecolor="black", linewidth=0.5)
    ax.set_xticks(range(len(top)))
    ax.set_xticklabels(labels, fontsize=8)
    ax.set_ylabel("Mejora media (%)")
    ax.set_title("Exp 3 — Top 10 configuraciones de SA (± std)")
    for bar, v in zip(bars, top["media_mejora"]):
        ax.text(bar.get_x() + bar.get_width()/2, bar.get_height(),
                f"{v:.1f}", ha="center", va="bottom", fontsize=8)
    _save(fig, out_dir / "exp3_top10.png")


# ============================================================================
# Exp 4 – Escalabilidad proporcional
# Columnas: n_clientes, n_paradas, n_camiones, media_coste_ini,
#           media_coste_fin, media_mejora, media_tiempo_s, std_tiempo_s,
#           media_cobertura
# ============================================================================

def plot_exp4(csv_path: Path, out_dir: Path):
    df = pd.read_csv(csv_path).sort_values("n_clientes")

    fig, axes = plt.subplots(1, 3, figsize=(15, 4.5))

    # Tiempo vs n_clientes con banda de std
    ax = axes[0]
    ax.plot(df["n_clientes"], df["media_tiempo_s"], "o-", color="C0", linewidth=2)
    ax.fill_between(df["n_clientes"],
                    df["media_tiempo_s"] - df["std_tiempo_s"],
                    df["media_tiempo_s"] + df["std_tiempo_s"],
                    alpha=0.2, color="C0")
    ax.set_xlabel("n_clientes (n_paradas≈2x)")
    ax.set_ylabel("Tiempo SA medio (s)")
    ax.set_title("Coste temporal")

    # Coste final vs n_clientes
    ax = axes[1]
    ax.plot(df["n_clientes"], df["media_coste_fin"], "s-", color="C1", linewidth=2)
    ax.set_xlabel("n_clientes")
    ax.set_ylabel("Coste final medio")
    ax.set_title("Calidad de la solución")

    # Cobertura vs n_clientes
    ax = axes[2]
    ax.plot(df["n_clientes"], df["media_cobertura"], "^-", color="C2", linewidth=2)
    ax.axhline(100, color="green", linestyle="--", alpha=0.5, label="100%")
    ax.set_xlabel("n_clientes")
    ax.set_ylabel("Cobertura media (%)")
    ax.set_title("Cobertura")
    ax.legend()
    ax.set_ylim(0, 105)

    fig.suptitle("Exp 4 — Escalabilidad proporcional (paradas≈2·clientes)")
    _save(fig, out_dir / "exp4_escalabilidad.png")


# ============================================================================
# Exp 5a / 5b – Escalabilidad separada
# Columnas: n_clientes, n_paradas, n_camiones, media_coste_fin,
#           media_mejora, media_tiempo_s, std_tiempo_s, media_cobertura
# ============================================================================

def plot_exp5(csv_5a: Path, csv_5b: Path, out_dir: Path):
    df_a = pd.read_csv(csv_5a).sort_values("n_paradas") if csv_5a.exists() else None
    df_b = pd.read_csv(csv_5b).sort_values("n_clientes") if csv_5b.exists() else None

    if df_a is None and df_b is None:
        return

    fig, axes = plt.subplots(2, 2, figsize=(13, 9))

    # 5a: clientes fijos, paradas variable
    if df_a is not None:
        ax = axes[0, 0]
        ax.plot(df_a["n_paradas"], df_a["media_tiempo_s"], "o-", color="C0", linewidth=2)
        ax.fill_between(df_a["n_paradas"],
                        df_a["media_tiempo_s"] - df_a["std_tiempo_s"],
                        df_a["media_tiempo_s"] + df_a["std_tiempo_s"],
                        alpha=0.2, color="C0")
        ax.set_xlabel("n_paradas (clientes fijos)")
        ax.set_ylabel("Tiempo SA medio (s)")
        ax.set_title("5a — Tiempo")

        ax = axes[0, 1]
        ax.plot(df_a["n_paradas"], df_a["media_coste_fin"], "s-", color="C1", linewidth=2)
        ax.set_xlabel("n_paradas (clientes fijos)")
        ax.set_ylabel("Coste final medio")
        ax.set_title("5a — Coste final")
    else:
        for ax in axes[0]:
            ax.text(0.5, 0.5, "5a no disponible", ha="center", va="center",
                    transform=ax.transAxes)

    # 5b: paradas fijas, clientes variable
    if df_b is not None:
        ax = axes[1, 0]
        ax.plot(df_b["n_clientes"], df_b["media_tiempo_s"], "o-", color="C2", linewidth=2)
        ax.fill_between(df_b["n_clientes"],
                        df_b["media_tiempo_s"] - df_b["std_tiempo_s"],
                        df_b["media_tiempo_s"] + df_b["std_tiempo_s"],
                        alpha=0.2, color="C2")
        ax.set_xlabel("n_clientes (paradas fijas)")
        ax.set_ylabel("Tiempo SA medio (s)")
        ax.set_title("5b — Tiempo")

        ax = axes[1, 1]
        ax.plot(df_b["n_clientes"], df_b["media_coste_fin"], "s-", color="C3", linewidth=2)
        ax.set_xlabel("n_clientes (paradas fijas)")
        ax.set_ylabel("Coste final medio")
        ax.set_title("5b — Coste final")
    else:
        for ax in axes[1]:
            ax.text(0.5, 0.5, "5b no disponible", ha="center", va="center",
                    transform=ax.transAxes)

    fig.suptitle("Exp 5 — Escalabilidad separada")
    _save(fig, out_dir / "exp5_escalabilidad.png")


# ============================================================================
# Exp 6 – Restricción de dominio (capacidad)
# Columnas: capacidad, media_coste_ini, media_coste_fin, media_mejora,
#           std_mejora, media_cobertura, media_tiempo_s
# ============================================================================

def plot_exp6(csv_path: Path, out_dir: Path):
    df = pd.read_csv(csv_path).sort_values("capacidad")

    fig, axes = plt.subplots(1, 3, figsize=(15, 4.5))

    # Coste inicial y final vs capacidad
    ax = axes[0]
    ax.plot(df["capacidad"], df["media_coste_ini"], "o--", color="C0", label="Inicial (greedy)")
    ax.plot(df["capacidad"], df["media_coste_fin"], "s-",  color="C1", label="Final (SA)")
    ax.set_xlabel("Capacidad camión")
    ax.set_ylabel("Coste medio")
    ax.set_title("Coste vs capacidad")
    ax.legend()

    # Mejora vs capacidad
    ax = axes[1]
    ax.errorbar(df["capacidad"], df["media_mejora"], yerr=df["std_mejora"],
                fmt="^-", color="C2", capsize=4, linewidth=2)
    ax.set_xlabel("Capacidad camión")
    ax.set_ylabel("Mejora media (%)")
    ax.set_title("Mejora vs capacidad")

    # Cobertura vs capacidad
    ax = axes[2]
    ax.plot(df["capacidad"], df["media_cobertura"], "D-", color="C3", linewidth=2)
    ax.axhline(100, color="green", linestyle="--", alpha=0.5, label="100%")
    ax.set_xlabel("Capacidad camión")
    ax.set_ylabel("Cobertura media (%)")
    ax.set_title("Cobertura vs capacidad")
    ax.set_ylim(0, 105)
    ax.legend()

    fig.suptitle("Exp 6 — Restricción de dominio (capacidad de los camiones)")
    _save(fig, out_dir / "exp6_capacidad.png")


# ============================================================================
# Exp 7 – Heurísticas combinadas H = H1 + w·H2
# Columnas: w, seed, H1_ini, H2_ini, H_ini, H1_fin, H2_fin, H_fin,
#           mejora_H, tiempo_s
# ============================================================================

def plot_exp7(csv_path: Path, out_dir: Path):
    df = pd.read_csv(csv_path)
    ws = sorted(df["w"].unique())

    # 1. Scatter Pareto: H1 vs H2 coloreado por w
    fig, ax = plt.subplots(figsize=(10, 7))
    for i, w in enumerate(ws):
        sub = df[df["w"] == w]
        ax.scatter(sub["H1_fin"], sub["H2_fin"],
                   label=f"w={w:g}",
                   color=plt.cm.viridis(i/max(1, len(ws)-1)),
                   s=60, alpha=0.7, edgecolor="black", linewidth=0.3)
    # También conectar los centros (medias) para ver la frontera Pareto
    centers = df.groupby("w").agg(H1=("H1_fin", "mean"),
                                   H2=("H2_fin", "mean")).sort_index()
    ax.plot(centers["H1"], centers["H2"], "k--", linewidth=1.5,
            alpha=0.5, label="Pareto (medias)")
    ax.set_xlabel("H1 final (distancia)")
    ax.set_ylabel("H2 final (retraso)")
    ax.set_title("Exp 7 — Trade-off H1 vs H2 por peso w")
    ax.legend(title="Peso w", loc="best")
    _save(fig, out_dir / "exp7_pareto.png")

    # 2. Línea H1, H2 (medias) en función de w
    fig, axes = plt.subplots(1, 2, figsize=(13, 4.5))
    g = df.groupby("w")
    means = g.agg(H1=("H1_fin", "mean"), H1_std=("H1_fin", "std"),
                  H2=("H2_fin", "mean"), H2_std=("H2_fin", "std")).sort_index()

    ax = axes[0]
    ax.errorbar(means.index, means["H1"], yerr=means["H1_std"],
                fmt="o-", color="C0", capsize=4, linewidth=2)
    ax.set_xlabel("Peso w")
    ax.set_ylabel("H1 medio (distancia)")
    ax.set_title("H1 final vs w")
    if (means.index > 0).any():
        ax.set_xscale("symlog")

    ax = axes[1]
    ax.errorbar(means.index, means["H2"], yerr=means["H2_std"],
                fmt="s-", color="C1", capsize=4, linewidth=2)
    ax.set_xlabel("Peso w")
    ax.set_ylabel("H2 medio (retraso)")
    ax.set_title("H2 final vs w")
    if (means.index > 0).any():
        ax.set_xscale("symlog")

    fig.suptitle("Exp 7 — Componentes H1 y H2 en función del peso w")
    _save(fig, out_dir / "exp7_componentes.png")


# ============================================================================
# Exp 8 – Variando número de camiones
# Columnas: n_camiones, media_coste_ini, media_coste_fin,
#           media_mejora, std_mejora, media_tiempo_s, std_tiempo_s,
#           media_cobertura
# ============================================================================

def plot_exp8(csv_path: Path, out_dir: Path):
    df = pd.read_csv(csv_path).sort_values("n_camiones")

    fig, axes = plt.subplots(1, 3, figsize=(15, 4.5))

    # Coste inicial y final vs número de camiones
    ax = axes[0]
    ax.plot(df["n_camiones"], df["media_coste_ini"], "o--",
            color="C0", label="Inicial (greedy)")
    ax.plot(df["n_camiones"], df["media_coste_fin"], "s-",
            color="C1", label="Final (SA)")
    ax.set_xlabel("Número de camiones")
    ax.set_ylabel("Coste medio")
    ax.set_title("Coste vs flota")
    ax.legend()

    # Mejora con error bars
    ax = axes[1]
    ax.errorbar(df["n_camiones"], df["media_mejora"], yerr=df["std_mejora"],
                fmt="^-", color="C2", capsize=4, linewidth=2)
    ax.set_xlabel("Número de camiones")
    ax.set_ylabel("Mejora media (%)")
    ax.set_title("Mejora del SA vs flota")

    # Tiempo y cobertura en ejes paralelos
    ax = axes[2]
    ax_t = ax
    line_t = ax_t.plot(df["n_camiones"], df["media_tiempo_s"], "D-",
                       color="C3", linewidth=2, label="Tiempo SA")
    ax_t.fill_between(df["n_camiones"],
                      df["media_tiempo_s"] - df["std_tiempo_s"],
                      df["media_tiempo_s"] + df["std_tiempo_s"],
                      alpha=0.2, color="C3")
    ax_t.set_xlabel("Número de camiones")
    ax_t.set_ylabel("Tiempo SA medio (s)", color="C3")
    ax_t.tick_params(axis="y", labelcolor="C3")

    ax_c = ax_t.twinx()
    line_c = ax_c.plot(df["n_camiones"], df["media_cobertura"], "v-",
                       color="C4", linewidth=2, label="Cobertura")
    ax_c.axhline(100, color="green", linestyle="--", alpha=0.4)
    ax_c.set_ylabel("Cobertura media (%)", color="C4")
    ax_c.tick_params(axis="y", labelcolor="C4")
    ax_c.set_ylim(0, 105)
    ax.set_title("Tiempo y cobertura vs flota")
    ax.grid(True, alpha=0.3)

    fig.suptitle("Exp 8 — Impacto del número de camiones (n_clientes=15, n_paradas=25)")
    _save(fig, out_dir / "exp8_camiones.png")


# ============================================================================
# Dispatcher
# ============================================================================

def main():
    parser = argparse.ArgumentParser(
        description="Visualizador de los CSVs de los experimentos.")
    parser.add_argument("--input", default=".",
                        help="Directorio donde están los experimento*.csv (defecto: .)")
    parser.add_argument("--output", default="viz/output/exp/",
                        help="Directorio de salida para los PNGs")
    args = parser.parse_args()

    in_dir = Path(args.input)
    out_dir = Path(args.output)
    out_dir.mkdir(parents=True, exist_ok=True)

    print(f"Input:  {in_dir.resolve()}")
    print(f"Output: {out_dir.resolve()}")
    print()

    plotters = [
        ("Exp 1", in_dir / "experimento1_operadores.csv",
            lambda p: plot_exp1(p, out_dir)),
        ("Exp 2", in_dir / "experimento2_inicializacion.csv",
            lambda p: plot_exp2(p, out_dir)),
        ("Exp 3", in_dir / "experimento3_grid_search.csv",
            lambda p: plot_exp3(p, out_dir)),
        ("Exp 4", in_dir / "experimento4_escalabilidad_proporcional.csv",
            lambda p: plot_exp4(p, out_dir)),
        # Exp 5 lo manejamos aparte porque toma dos CSVs
        ("Exp 6", in_dir / "experimento6_capacidad.csv",
            lambda p: plot_exp6(p, out_dir)),
        ("Exp 7", in_dir / "experimento7_heuristicas_combinadas.csv",
            lambda p: plot_exp7(p, out_dir)),
        ("Exp 8", in_dir / "experimento8_camiones.csv",
            lambda p: plot_exp8(p, out_dir)),
    ]

    procesados = []
    saltados   = []

    for nombre, path, fn in plotters:
        if path.exists():
            print(f"[{nombre}] {path.name}")
            try:
                fn(path)
                procesados.append(nombre)
            except Exception as e:
                print(f"  ERROR procesando {nombre}: {e}", file=sys.stderr)
        else:
            saltados.append((nombre, path.name))

    # Exp 5 (dos ficheros)
    p5a = in_dir / "experimento5a_clientes_fijos.csv"
    p5b = in_dir / "experimento5b_paradas_fijas.csv"
    if p5a.exists() or p5b.exists():
        print(f"[Exp 5] {p5a.name if p5a.exists() else '-'} / "
              f"{p5b.name if p5b.exists() else '-'}")
        try:
            plot_exp5(p5a, p5b, out_dir)
            procesados.append("Exp 5")
        except Exception as e:
            print(f"  ERROR procesando Exp 5: {e}", file=sys.stderr)
    else:
        saltados.append(("Exp 5", "experimento5{a,b}_*.csv"))

    print()
    print(f"Procesados: {procesados}")
    if saltados:
        print("Saltados (CSV no encontrado):")
        for nombre, name in saltados:
            print(f"  {nombre}: {name}")


if __name__ == "__main__":
    main()
