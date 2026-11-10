#!/bin/sh
# Ejecuta todas las pruebas: sh tests/ejecutar.sh
cd "$(dirname "$0")" || exit 1
mal=0
for t in t-*.sh; do sh "$t" || mal=$((mal + 1)); done
[ "$mal" = 0 ] && echo "TODO BIEN" || { echo "$mal ficheros con fallos"; exit 1; }
