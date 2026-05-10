import sys
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib
import numpy as np

matplotlib.rcParams['figure.dpi'] = 150
matplotlib.rcParams['savefig.bbox'] = 'tight'

def cargar(filepath):
    df = pd.read_csv(filepath)
    return df

# ── Exp 1: Operadores ──────────────────────────────────────

def plot_exp1(df):
    d = df[df['experimento'] == 'exp1']
    if d.empty: return

    fig, axes = plt.subplots(1, 3, figsize=(15, 5))
    fig.suptitle('Exp 1: Comparación de Operadores', fontweight='bold')

    orden = d.groupby('config')['fitness_fin'].mean().sort_values().index.tolist()

    # Boxplot fitness final
    data = [d[d['config'] == c]['fitness_fin'].values for c in orden]
    bp = axes[0].boxplot(data, labels=orden, patch_artist=True)
    for patch, color in zip(bp['boxes'], plt.cm.Set2(np.linspace(0, 1, len(orden)))):
        patch.set_facecolor(color)
    axes[0].set_ylabel('Fitness final')
    axes[0].set_title('Calidad de solución')
    axes[0].tick_params(axis='x', rotation=30)

    # Tiempo medio
    medias_t = [d[d['config'] == c]['tiempo_ms'].mean() for c in orden]
    bars = axes[1].bar(orden, medias_t, color=plt.cm.Set2(np.linspace(0, 1, len(orden))))
    axes[1].set_ylabel('Tiempo medio (ms)')
    axes[1].set_title('Coste computacional')
    axes[1].tick_params(axis='x', rotation=30)

    # Desglose F1, F2, F3
    medias = d.groupby('config')[['f1', 'f2', 'f3']].mean().loc[orden]
    medias.plot(kind='bar', stacked=True, ax=axes[2], colormap='Set2')
    axes[2].set_ylabel('Penalización media')
    axes[2].set_title('Desglose heurísticas')
    axes[2].tick_params(axis='x', rotation=30)
    axes[2].legend(title='Componente')

    plt.tight_layout()
    plt.savefig('exp1_operadores.png')
    print('Guardado exp1_operadores.png')

# ── Exp 2: Inicialización ──────────────────────────────────

def plot_exp2(df):
    d = df[df['experimento'] == 'exp2']
    if d.empty: return

    fig, axes = plt.subplots(1, 2, figsize=(10, 5))
    fig.suptitle('Exp 2: Estrategias de Inicialización', fontweight='bold')

    orden = d.groupby('config')['fitness_fin'].mean().sort_values().index.tolist()

    data = [d[d['config'] == c]['fitness_fin'].values for c in orden]
    bp = axes[0].boxplot(data, labels=orden, patch_artist=True)
    for patch, color in zip(bp['boxes'], plt.cm.Pastel1(np.linspace(0, 1, len(orden)))):
        patch.set_facecolor(color)
    axes[0].set_ylabel('Fitness final')
    axes[0].set_title('Calidad tras SA')

    # Fitness inicial vs final
    resumen = d.groupby('config')[['fitness_ini', 'fitness_fin']].mean().loc[orden]
    resumen.plot(kind='bar', ax=axes[1], color=['#ff9999', '#66b3ff'])
    axes[1].set_ylabel('Fitness medio')
    axes[1].set_title('Inicial vs Final')
    axes[1].tick_params(axis='x', rotation=30)
    axes[1].legend(['Inicial', 'Final'])

    plt.tight_layout()
    plt.savefig('exp2_inicializacion.png')
    print('Guardado exp2_inicializacion.png')

# ── Exp 3: Grid Search ─────────────────────────────────────

def plot_exp3(df):
    d = df[df['experimento'] == 'exp3']
    if d.empty: return

    # Parsear config: "T1_c0.9_i10"
    parsed = d['config'].str.extract(r'T([\d.]+)_c([\d.]+)_i(\d+)')
    d = d.copy()
    d['T_init'] = parsed[0].astype(float)
    d['cooling'] = parsed[1].astype(float)
    d['iter'] = parsed[2].astype(int)

    iters = sorted(d['iter'].unique())
    fig, axes = plt.subplots(1, len(iters), figsize=(6 * len(iters), 5))
    if len(iters) == 1: axes = [axes]
    fig.suptitle('Exp 3: Grid Search SA (fitness medio)', fontweight='bold')

    for ax, it in zip(axes, iters):
        sub = d[d['iter'] == it]
        pivot = sub.groupby(['T_init', 'cooling'])['fitness_fin'].mean().unstack()
        im = ax.imshow(pivot.values, aspect='auto', cmap='RdYlGn_r')
        ax.set_xticks(range(len(pivot.columns)))
        ax.set_xticklabels([f'{c}' for c in pivot.columns], rotation=45)
        ax.set_yticks(range(len(pivot.index)))
        ax.set_yticklabels([f'{t}' for t in pivot.index])
        ax.set_xlabel('Cooling rate')
        ax.set_ylabel('T_init')
        ax.set_title(f'iter_por_temp = {it}')
        for i in range(len(pivot.index)):
            for j in range(len(pivot.columns)):
                val = pivot.values[i, j]
                ax.text(j, i, f'{val:.0f}', ha='center', va='center', fontsize=8)
        plt.colorbar(im, ax=ax, shrink=0.8)

    plt.tight_layout()
    plt.savefig('exp3_grid_search.png')
    print('Guardado exp3_grid_search.png')

# ── Exp 4: Escalabilidad proporcional ──────────────────────

def plot_exp4(df):
    d = df[df['experimento'] == 'exp4']
    if d.empty: return

    fig, axes = plt.subplots(1, 2, figsize=(12, 5))
    fig.suptitle('Exp 4: Escalabilidad Proporcional', fontweight='bold')

    orden = d['config'].unique()
    resumen = d.groupby('config').agg(
        fit_mean=('fitness_fin', 'mean'), fit_std=('fitness_fin', 'std'),
        t_mean=('tiempo_ms', 'mean'), t_std=('tiempo_ms', 'std')
    ).loc[orden]

    x = range(len(orden))
    axes[0].bar(x, resumen['fit_mean'], yerr=resumen['fit_std'],
                capsize=4, color='steelblue', alpha=0.8)
    axes[0].set_xticks(x)
    axes[0].set_xticklabels(orden, rotation=30)
    axes[0].set_ylabel('Fitness final')
    axes[0].set_title('Calidad vs tamaño')

    axes[1].bar(x, resumen['t_mean'], yerr=resumen['t_std'],
                capsize=4, color='coral', alpha=0.8)
    axes[1].set_xticks(x)
    axes[1].set_xticklabels(orden, rotation=30)
    axes[1].set_ylabel('Tiempo (ms)')
    axes[1].set_title('Tiempo vs tamaño')

    plt.tight_layout()
    plt.savefig('exp4_escalabilidad.png')
    print('Guardado exp4_escalabilidad.png')

# ── Exp 5: Escalabilidad separada ─────────────────────────

def plot_exp5(df):
    d5a = df[df['experimento'] == 'exp5a']
    d5b = df[df['experimento'] == 'exp5b']
    if d5a.empty and d5b.empty: return

    fig, axes = plt.subplots(2, 2, figsize=(12, 10))
    fig.suptitle('Exp 5: Escalabilidad Separada', fontweight='bold')

    for row, (d, label, xlabel) in enumerate([
        (d5a, '5a: fija n_palets, varía n_items', 'n_items'),
        (d5b, '5b: fija n_items, varía n_palets', 'n_palets')
    ]):
        if d.empty: continue
        orden = d['config'].unique()
        resumen = d.groupby('config').agg(
            fit_mean=('fitness_fin', 'mean'), fit_std=('fitness_fin', 'std'),
            t_mean=('tiempo_ms', 'mean'), t_std=('tiempo_ms', 'std')
        ).loc[orden]

        x = range(len(orden))
        axes[row][0].errorbar(x, resumen['fit_mean'], yerr=resumen['fit_std'],
                              marker='o', capsize=4, color='steelblue')
        axes[row][0].set_xticks(x)
        axes[row][0].set_xticklabels(orden, rotation=30)
        axes[row][0].set_ylabel('Fitness final')
        axes[row][0].set_title(f'{label} — Calidad')

        axes[row][1].errorbar(x, resumen['t_mean'], yerr=resumen['t_std'],
                              marker='s', capsize=4, color='coral')
        axes[row][1].set_xticks(x)
        axes[row][1].set_xticklabels(orden, rotation=30)
        axes[row][1].set_ylabel('Tiempo (ms)')
        axes[row][1].set_title(f'{label} — Tiempo')

    plt.tight_layout()
    plt.savefig('exp5_escalabilidad_sep.png')
    print('Guardado exp5_escalabilidad_sep.png')

# ── Exp 6: Restricción de dominio ─────────────────────────

def plot_exp6(df):
    d = df[df['experimento'] == 'exp6']
    if d.empty: return

    fig, axes = plt.subplots(1, 2, figsize=(12, 5))
    fig.suptitle('Exp 6: Tipo de Vehículo (restricción de dominio)', fontweight='bold')

    orden = d['config'].unique()
    resumen = d.groupby('config').agg(
        fit_mean=('fitness_fin', 'mean'), fit_std=('fitness_fin', 'std'),
        t_mean=('tiempo_ms', 'mean'), t_std=('tiempo_ms', 'std')
    ).loc[orden]

    # Colores por tipo de vehículo
    colores = []
    for c in orden:
        if 'furgoneta' in c: colores.append('#66c2a5')
        elif 'camion6' in c: colores.append('#fc8d62')
        else: colores.append('#8da0cb')

    x = range(len(orden))
    axes[0].bar(x, resumen['fit_mean'], yerr=resumen['fit_std'],
                capsize=4, color=colores, alpha=0.85)
    axes[0].set_xticks(x)
    axes[0].set_xticklabels(orden, rotation=35, ha='right')
    axes[0].set_ylabel('Fitness final')
    axes[0].set_title('Calidad por tipo de vehículo')

    axes[1].bar(x, resumen['t_mean'], yerr=resumen['t_std'],
                capsize=4, color=colores, alpha=0.85)
    axes[1].set_xticks(x)
    axes[1].set_xticklabels(orden, rotation=35, ha='right')
    axes[1].set_ylabel('Tiempo (ms)')
    axes[1].set_title('Tiempo por tipo de vehículo')

    from matplotlib.patches import Patch
    legend = [Patch(color='#66c2a5', label='Furgoneta (≤3)'),
              Patch(color='#fc8d62', label='Camión 6 (4-6)'),
              Patch(color='#8da0cb', label='Camión 8 (≥7)')]
    axes[0].legend(handles=legend, fontsize=8)

    plt.tight_layout()
    plt.savefig('exp6_vehiculo.png')
    print('Guardado exp6_vehiculo.png')

# ── Exp 7: Heurísticas combinadas ─────────────────────────

def plot_exp7(df):
    d = df[df['experimento'] == 'exp7']
    if d.empty: return

    fig, axes = plt.subplots(1, 2, figsize=(12, 5))
    fig.suptitle('Exp 7: Trade-off H1 (frag. cliente) vs H2 (accesibilidad)', fontweight='bold')

    # Extraer w del config
    d = d.copy()
    d['w'] = d['config'].str.extract(r'w([\d.]+)').astype(float)
    resumen = d.groupby('w').agg(
        f2_mean=('f2', 'mean'), f2_std=('f2', 'std'),
        f3_mean=('f3', 'mean'), f3_std=('f3', 'std'),
        fit_mean=('fitness_fin', 'mean')
    ).sort_index()

    w = resumen.index.values

    # H1 y H2 vs w
    axes[0].errorbar(w, resumen['f2_mean'], yerr=resumen['f2_std'],
                     marker='o', label='H1 (frag. cliente)', capsize=4)
    axes[0].errorbar(w, resumen['f3_mean'], yerr=resumen['f3_std'],
                     marker='s', label='H2 (accesibilidad)', capsize=4)
    axes[0].set_xlabel('Peso w')
    axes[0].set_ylabel('Valor heurística')
    axes[0].set_title('Componentes por peso')
    axes[0].legend()
    axes[0].set_xscale('symlog', linthresh=1)

    # Scatter H1 vs H2 (cada punto = una ejecución)
    scatter = axes[1].scatter(d['f2'], d['f3'], c=d['w'], cmap='viridis',
                              alpha=0.7, edgecolors='gray', linewidth=0.5)
    axes[1].set_xlabel('H1 (frag. cliente)')
    axes[1].set_ylabel('H2 (accesibilidad)')
    axes[1].set_title('Frente de Pareto')
    plt.colorbar(scatter, ax=axes[1], label='Peso w')

    plt.tight_layout()
    plt.savefig('exp7_tradeoff.png')
    print('Guardado exp7_tradeoff.png')

# ── Main ───────────────────────────────────────────────────

if __name__ == '__main__':
    filepath = sys.argv[1] if len(sys.argv) > 1 else 'resultados.csv'
    df = cargar(filepath)

    experimentos = df['experimento'].unique()
    print(f'Experimentos encontrados: {", ".join(experimentos)}')
    print(f'Total filas: {len(df)}')

    plot_exp1(df)
    plot_exp2(df)
    plot_exp3(df)
    plot_exp4(df)
    plot_exp5(df)
    plot_exp6(df)
    plot_exp7(df)
