#!/usr/bin/env bash
# Genera la imagen de Nura (ex postmarketOS) para el Moto E 2015 (surnia).
# No toca el teléfono: solo trabaja en este ordenador.
set -euo pipefail

echo "== 1/3 Configuración (responde a las preguntas) =="
pmbootstrap init

echo
echo "== 2/3 Comprobación del dispositivo =="
pmbootstrap config device
pmbootstrap config ui

echo
echo "== 3/3 Compilación e imagen (al final pide la contraseña del usuario del móvil) =="
pmbootstrap install

echo
echo "LISTO. Vuelve a Claude y dile que ha terminado."
