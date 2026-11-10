#!/bin/sh
# Busca palabras prohibidas (datos personales) en un repo git antes de publicar.
# Uso: comprobar-privacidad.sh <dir-repo> <lista>
# Revisa los ficheros con seguimiento, los autores y mensajes de commit y el contenido del historial.
# La lista tiene una palabra o frase por línea; no distingue mayúsculas; ignora las líneas vacías o con #.
# Sale con 0 si está limpio y con 1 si encuentra algo (lo imprime como fichero:línea:texto).
[ $# -eq 2 ] || { echo "Uso: $0 <dir-repo> <lista>" >&2; exit 2; }
repo=$1; lista=$2
[ -r "$lista" ] || { echo "No puedo leer la lista $lista" >&2; exit 2; }
pat=$(mktemp "${TMPDIR:-/tmp}/prohibidas.XXXXXX") || exit 2
trap 'rm -f "$pat"' EXIT
grep -v '^[[:space:]]*#' "$lista" | grep -v '^[[:space:]]*$' > "$pat"
[ -s "$pat" ] || exit 0
encontrado=0
# Ficheros actuales (git grep salta los binarios).
if git -C "$repo" grep -I -n -i -F -f "$pat" -- . 2>/dev/null; then encontrado=1; fi
# Autores, emails y mensajes.
if git -C "$repo" rev-parse -q --verify HEAD >/dev/null 2>&1; then
    git -C "$repo" log --all --format='@@commit %H%n%an <%ae> %cn <%ce>%n%B' | awk '/^@@commit /{h=$2; next} {print "commit:" h ":" $0}' |
        grep -i -F -f "$pat" | cut -c1-200 | grep . && encontrado=1
    # Contenido de todo el historial.
    git -C "$repo" log --all -p --format='@@commit %H' | awk '/^@@commit /{h=$2; next} {print "historial:" h ":" $0}' |
        grep -i -F -f "$pat" | cut -c1-200 | grep . && encontrado=1
fi
exit $encontrado
