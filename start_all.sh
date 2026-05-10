#!/bin/bash

# Mapeo de índices
DIAS_MAP=("lunes" "martes" "miercoles" "jueves" "viernes")

# Validación: Si no hay parámetro ($1), mostrar uso y salir
if [ -z "$1" ]; then
    echo "Uso: $0 [numero]"
    echo "  1 - lunes"
    echo "  2 - martes"
    echo "  3 - miercoles"
    echo "  4 - jueves"
    echo "  5 - viernes"
    exit 1
fi

# Ajustar índice (el usuario pone 1, el array empieza en 0)
INDEX=$(($1 - 1))

# Validar rango
if [ $INDEX -lt 0 ] || [ $INDEX -ge ${#DIAS_MAP[@]} ]; then
    echo "Error: El parámetro debe estar entre 1 y 6."
    exit 1
fi

DIA=${DIAS_MAP[$INDEX]}

echo "=============================================="
echo "EJECUTANDO TEST ÚNICO: $DIA"
echo "=============================================="

# Comandos solicitados
make clean
make damm damm-dist
make pipeline JUEGO=test/juego_$DIA.csv OUT_DIR=out_$DIA

if [ -d "frontend" ]; then
    cd frontend
    npm install
    echo "Lanzando npm run dev para $DIA en segundo plano..."
    npm run dev &
    cd ..
fi