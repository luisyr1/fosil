#!/bin/bash
# ctl.sh comando... : manda órdenes a la FIFO de Fósil con pausas
c=""; for a in "$@"; do c="$c echo '$a' > /run/fosil/ctl; sleep 0.3;"; done
timeout 30 ssh -o BatchMode=yes "${MOVIL:?Define MOVIL=usuario@host}" "sudo -n sh -c \"$c\"" </dev/null
