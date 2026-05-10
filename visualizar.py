import json
import sys
import numpy as np
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
import matplotlib.colors as mcolors

def visualizar(filepath="resultado.json"):
    with open(filepath) as f:
        data = json.load(f)

    n_palets = data["n_palets"]
    items = data["items"]
    fitness_val = data["fitness"]
    fitness_ini = data.get("fitness_inicial", None)
    orden_ruta = data.get("orden_ruta", [])

    PISOS = 6
    FILAS = 5
    COLS = 2

    clientes = sorted(set(item["cliente_id"] for item in items))
    cmap = plt.cm.Set2
    color_map = {c: cmap(i / max(len(clientes), 1)) for i, c in enumerate(clientes)}

    fig = plt.figure(figsize=(6 * n_palets, 8))

    for pal_idx in range(n_palets):
        ax = fig.add_subplot(1, n_palets, pal_idx + 1, projection='3d')
        ax.set_title(f"Palet {pal_idx}", fontsize=12, fontweight='bold')

        pal_items = [it for it in items if it["palet"] == pal_idx]

        filled = np.zeros((COLS, FILAS, PISOS), dtype=bool)
        colors = np.empty((COLS, FILAS, PISOS), dtype=object)
        alphas = np.ones((COLS, FILAS, PISOS)) * 0.0

        for it in pal_items:
            c, f, p = it["col"], it["fila"], it["piso"]
            filled[c][f][p] = True
            color = color_map[it["cliente_id"]]
            if it["es_barril"]:
                colors[c][f][p] = (*color[:3], 0.5)
            else:
                colors[c][f][p] = (*color[:3], 0.9)

        for c in range(COLS):
            for f in range(FILAS):
                for p in range(PISOS):
                    if not filled[c][f][p]:
                        continue
                    rgba = colors[c][f][p]
                    ax.bar3d(c, f, p, 0.85, 0.85, 0.85,
                             color=rgba,
                             edgecolor='black' if not items else 'darkgray',
                             linewidth=0.5)

                    it = None
                    for item in pal_items:
                        if item["col"] == c and item["fila"] == f and item["piso"] == p:
                            it = item
                            break
                    if it:
                        label = f"C{it['cliente_id']}"
                        if it["es_barril"]:
                            label += "\n(B)"
                        ax.text(c + 0.42, f + 0.42, p + 0.42, label,
                                fontsize=5, ha='center', va='center', color='black')

        ax.set_xlabel('Col', fontsize=8)
        ax.set_ylabel('Fila', fontsize=8)
        ax.set_zlabel('Piso', fontsize=8)
        ax.set_xlim(-0.2, COLS)
        ax.set_ylim(-0.2, FILAS)
        ax.set_zlim(0, PISOS)
        ax.set_xticks(range(COLS))
        ax.set_yticks(range(FILAS))
        ax.set_zticks(range(PISOS))
        ax.view_init(elev=25, azim=-60)

    legend_elements = []
    for c in clientes:
        color = color_map[c]
        legend_elements.append(plt.Rectangle((0, 0), 1, 1, fc=color, label=f"Cliente {c}"))
    legend_elements.append(plt.Rectangle((0, 0), 1, 1, fc='white', ec='black',
                                          alpha=0.5, label="Barril (transp.)"))

    titulo = f"Fitness: {fitness_val:.1f}"
    if fitness_ini is not None:
        titulo += f"  (inicial: {fitness_ini:.1f})"
    if orden_ruta:
        titulo += f"\nOrden entrega: {' -> '.join(str(c) for c in orden_ruta)}"

    fig.suptitle(titulo, fontsize=14, fontweight='bold')
    fig.legend(handles=legend_elements, loc='lower center',
               ncol=len(legend_elements), fontsize=9)
    plt.tight_layout(rect=[0, 0.06, 1, 0.92])
    plt.savefig("resultado.png", dpi=150, bbox_inches='tight')
    print(f"Guardado resultado.png")
   # plt.show()

if __name__ == "__main__":
    filepath = sys.argv[1] if len(sys.argv) > 1 else "resultado.json"
    visualizar(filepath)
