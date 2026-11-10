#!/bin/bash
# shot.sh nombre [órdenes ctl...]: manda órdenes a Fósil y baja una captura en PNG a ./capturas-prueba
# Destino en MOVIL=usuario@host (por USB suele ser usuario@172.16.42.1).
A="$(cd "$(dirname "$0")" && pwd)"; O="$A/capturas-prueba"; mkdir -p "$O"
M=${MOVIL:?Define MOVIL=usuario@host}
n=$1; shift
c=""; for a in "$@"; do c="$c echo '$a' > /run/fosil/ctl; sleep 0.45;"; done
timeout 25 ssh -o BatchMode=yes "$M" "sudo -n sh -c \"$c rm -f /tmp/s.ppm; echo shot /tmp/s.ppm > /run/fosil/ctl; sleep 0.8; chmod 644 /tmp/s.ppm; cat /tmp/s.ppm\"" </dev/null > "$O/$n.ppm" && python3 -I "$A/ppm2png.py" "$O/$n.ppm" && rm "$O/$n.ppm" && echo "$O/$n.png"
