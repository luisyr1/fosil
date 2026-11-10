#!/bin/sh
. "$(dirname "$0")/lib.sh"
C="$RAIZ_REPO/herramientas/comprobar-privacidad.sh"
tmpdir T
printf '# comentario\nreddecasa\nejemplo.org\n' > "$T/lista"

repo_prueba "$T/sucio"; echo "conecta a RedDeCasa" > "$T/sucio/a.txt"; git -C "$T/sucio" add -A; git -C "$T/sucio" commit -qm uno
sale 1 "fichero con palabra prohibida" -- sh "$C" "$T/sucio" "$T/lista"
contiene "RedDeCasa" "$SALIDA" "dice qué encontró"
contiene "a.txt" "$SALIDA" "dice dónde"

repo_prueba "$T/limpio"; echo "hola" > "$T/limpio/a.txt"; git -C "$T/limpio" add -A; git -C "$T/limpio" commit -qm uno
sale 0 "repo limpio" -- sh "$C" "$T/limpio" "$T/lista"

repo_prueba "$T/autor"; git -C "$T/autor" config user.email x@ejemplo.org
echo "hola" > "$T/autor/a.txt"; git -C "$T/autor" add -A; git -C "$T/autor" commit -qm uno
sale 1 "autor con palabra prohibida" -- sh "$C" "$T/autor" "$T/lista"
contiene "commit:" "$SALIDA" "señala el commit"

repo_prueba "$T/hist"; echo "reddecasa" > "$T/hist/a.txt"; git -C "$T/hist" add -A; git -C "$T/hist" commit -qm uno
echo "hola" > "$T/hist/a.txt"; git -C "$T/hist" commit -qam dos
sale 1 "palabra prohibida solo en el historial" -- sh "$C" "$T/hist" "$T/lista"
contiene "historial:" "$SALIDA" "señala el historial"
repo_prueba "$T/cuerpo"; echo a > "$T/cuerpo/a"; git -C "$T/cuerpo" add -A; git -C "$T/cuerpo" commit -qm "uno" -m "probado en RedDeCasa"
echo b > "$T/cuerpo/a"; git -C "$T/cuerpo" commit -qam dos
sale 1 "palabra prohibida en el cuerpo de un commit antiguo" -- sh "$C" "$T/cuerpo" "$T/lista"
contiene "commit:" "$SALIDA" "señala el commit del cuerpo"
fin
