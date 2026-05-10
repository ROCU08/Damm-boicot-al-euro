"""Cálculo de hora_llegada_estimada y retraso_min por visita de una ruta.

El SA-ruta en C++ calcula `total_retraso` por ruta pero no exporta el
desglose por visita. Lo recalculamos aquí en Python a partir de la matriz
de tiempos y los volúmenes atendidos. El algoritmo es lineal sobre las
visitas y usa los mismos pesos que h_ruta.cc (minutos_por_volumen).
"""

from __future__ import annotations


# Default coherente con codi/algo/ruta/heuristica/h_ruta.h:PesosCoste.
MINUTOS_POR_VOLUMEN_DEFAULT = 0.5


def visitas_con_horarios(
    ruta_raw: dict,
    datos: dict,
    minutos_por_volumen: float = MINUTOS_POR_VOLUMEN_DEFAULT,
) -> list[dict]:
    """Convierte una ruta en formato 'raw' (paradas + clientes_atendidos) en
    una lista de visitas enriquecidas con `hora_llegada_estimada` y
    `retraso_min`.

    Modelo:
      - El camión sale del depósito a `camion.hora_inicio`.
      - Tiempo entre depósito y primera parada: tiempo_deposito[primera].
      - Tiempo entre paradas i y j: matriz_tiempo[i*M + j].
      - Si llega antes de hora_ini del cliente, ESPERA (no penaliza, no avanza el reloj
        más allá de hora_ini). Esto se aproxima sumando max(t, hora_ini) tras la espera.
      - Si llega después de hora_fin del cliente, retraso = t - hora_fin.
      - Tiempo de servicio en la parada: (vol_recoger + vol_devolver) * minutos_por_volumen.
    """
    M = len(datos["paradas"])
    matriz_tiempo = datos["matriz_tiempo"]
    tiempo_deposito = datos["tiempo_deposito"]
    camion = datos["camiones"][ruta_raw["camion_id"]]
    t = float(camion["hora_inicio"])

    visitas: list[dict] = []
    prev_parada: int | None = None

    paradas_seq = ruta_raw["paradas"]
    clientes_seq = ruta_raw["clientes_atendidos"]

    for parada_id, clientes in zip(paradas_seq, clientes_seq):
        # Tiempo de viaje
        if prev_parada is None:
            t += tiempo_deposito[parada_id]
        else:
            t += matriz_tiempo[prev_parada * M + parada_id]

        hora_llegada = t

        # Retraso vs ventana del cliente más restrictivo
        retraso = 0.0
        hora_ini_max = 0.0
        for cid in clientes:
            cli = datos["clientes"][cid]
            if t > cli["hora_fin"]:
                retraso = max(retraso, t - cli["hora_fin"])
            hora_ini_max = max(hora_ini_max, cli["hora_ini"])

        # Si llegamos antes de la ventana, esperamos.
        t = max(t, hora_ini_max)

        # Tiempo de servicio
        vol = 0.0
        for cid in clientes:
            cli = datos["clientes"][cid]
            vol += cli["volumen_recoger"] + cli["volumen_devolver"]
        t += vol * minutos_por_volumen

        visitas.append({
            "parada_id": int(parada_id),
            "clientes_atendidos": [int(c) for c in clientes],
            "hora_llegada_estimada": int(round(hora_llegada)),
            "retraso_min": float(round(retraso, 1)),
        })
        prev_parada = parada_id

    return visitas
