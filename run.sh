#!/bin/bash
set -e

echo "=== Compilando ==="
g++ -std=c++14 -O2 -o sa_distr codi/main.cc

echo "=== Ejecutando SA ==="
./sa_distr

echo "=== Visualizando ==="
python3 visualizar.py
