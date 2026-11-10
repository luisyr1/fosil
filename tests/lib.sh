# Utilidades mínimas de prueba en POSIX sh. Uso: . "$(dirname "$0")/lib.sh"
RAIZ_REPO=$(cd "$(dirname "$0")/.." && pwd)
_bien=0; _mal=0; _tmps=
_ok() { _bien=$((_bien + 1)); }
_ko() { _mal=$((_mal + 1)); printf 'FALLA: %s\n' "$1" >&2; [ -n "$2" ] && printf '  %s\n' "$2" >&2; return 0; }
igual() { [ "$1" = "$2" ] && _ok || _ko "$3" "esperado [$1] obtenido [$2]"; }
contiene() { case "$2" in *"$1"*) _ok ;; *) _ko "$3" "no contiene [$1] en [$2]" ;; esac; }
no_contiene() { case "$2" in *"$1"*) _ko "$3" "contiene [$1]" ;; *) _ok ;; esac; }
existe() { [ -e "$1" ] && _ok || _ko "$2" "no existe $1"; }
no_existe() { [ -e "$1" ] && _ko "$2" "existe $1" || _ok; }
# sale <código> <descr> -- cmd...  (deja la salida en $SALIDA)
sale() { _c=$1; _d=$2; shift 3; SALIDA=$("$@" 2>&1); _r=$?; [ "$_r" = "$_c" ] && _ok || _ko "$_d" "código $_r, esperado $_c: $SALIDA"; }
# tmpdir VAR: crea un directorio temporal, lo guarda en VAR y lo borra al salir.
tmpdir() { _t=$(mktemp -d "${TMPDIR:-/tmp}/fosil-prueba.XXXXXX"); _tmps="$_tmps $_t"; eval "$1=\$_t"; }
# repo_prueba DIR: repo git con identidad fija y sin firma.
repo_prueba() { git init -q -b main "$1" && git -C "$1" config user.name prueba && git -C "$1" config user.email prueba@prueba.invalid && git -C "$1" config commit.gpgsign false && git -C "$1" config tag.gpgsign false; }
_limpia() { for _t in $_tmps; do rm -rf "$_t"; done; }
trap _limpia EXIT
fin() { printf '%s: %s bien, %s mal\n' "$(basename "$0")" "$_bien" "$_mal"; [ "$_mal" = 0 ]; }
