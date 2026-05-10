#!/bin/bash

# Mapeo de índices
DIAS_MAP=("lunes" "martes" "miercoles" "jueves" "viernes")

# Validación: Si no hay parámetro ($1), mostrar uso y salir
if [ -z "$1" ]; then
    echo "Uso: $0 [numero] [solo_turno]"
    echo "  numero:     1=lunes  2=martes  3=miercoles  4=jueves  5=viernes"
    echo "  solo_turno: opcional (1 o 2). Si se omite, se ejecutan los 2 turnos."
    echo ""
    echo "Genera resultado_turno1.json y resultado_turno2.json en out_<dia>/"
    echo "(franja partida en 14:00 por defecto)."
    exit 1
fi

# Ajustar índice (el usuario pone 1, el array empieza en 0)
INDEX=$(($1 - 1))

# Validar rango
if [ $INDEX -lt 0 ] || [ $INDEX -ge ${#DIAS_MAP[@]} ]; then
    echo "Error: El parámetro debe estar entre 1 y 5."
    exit 1
fi

DIA=${DIAS_MAP[$INDEX]}

# Argumento opcional: SOLO_TURNO (1 o 2)
SOLO_TURNO_ARG=""
if [ -n "$2" ]; then
    if [ "$2" != "1" ] && [ "$2" != "2" ]; then
        echo "Error: solo_turno debe ser 1 o 2."
        exit 1
    fi
    SOLO_TURNO_ARG="SOLO_TURNO=$2"
fi

echo "=============================================="
echo "EJECUTANDO TEST ÚNICO: $DIA"
if [ -n "$SOLO_TURNO_ARG" ]; then
    echo "  (solo turno $2)"
else
    echo "  (turnos 1 y 2)"
fi
echo "=============================================="

# Comandos solicitados
make clean
make damm damm-dist
make pipeline JUEGO=test/juego_$DIA.csv OUT_DIR=out_$DIA $SOLO_TURNO_ARG

echo ""
echo "=============================================="
echo "Resultados generados en out_$DIA/:"
ls -1 out_$DIA/resultado_turno*.json 2>/dev/null | sed 's/^/  /'
echo "Cárgalos en el frontend uno a uno (file picker)."
echo "=============================================="

if [ -d "frontend" ]; then
    cd frontend
    npm install
    echo "Lanzando npm run dev para $DIA en segundo plano..."
    npm run dev &
    cd ..
fi