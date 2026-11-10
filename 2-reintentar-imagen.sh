#!/usr/bin/env bash
# Compila el firmware archivado que faltaba y vuelve a generar la imagen.
# No toca el teléfono: solo trabaja en este ordenador.
set -euo pipefail
pmbootstrap build --arch aarch64 firmware-motorola-harpia
pmbootstrap install
echo
echo "LISTO. Vuelve a Claude y dile que ha terminado."
