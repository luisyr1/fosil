#!/bin/sh
# Trae los cambios que han hecho los agentes en ~/fosil del móvil a la rama movil/main, para revisarlos.
# No hace commit ni push: integrar lo del móvil lo decide quien revisa.
# Destino: MOVIL=usuario@host, o el fichero movil.env de ../fosil-privado.
set -eu
cd "$(dirname "$0")"
PRIV=${FOSIL_PRIVADO:-../fosil-privado}
[ -n "${MOVIL:-}" ] || { [ -r "$PRIV/movil.env" ] && MOVIL=$(sed -n 's/^MOVIL=//p' "$PRIV/movil.env"); }
[ -n "${MOVIL:-}" ] || { echo "Define MOVIL=usuario@host o crea $PRIV/movil.env" >&2; exit 2; }
git fetch -q "ssh://$MOVIL/~/fosil" main:refs/remotes/movil/main
echo "Commits del móvil que no están en main:"
git log --oneline main..movil/main
echo "Revisa con: git diff main...movil/main"
