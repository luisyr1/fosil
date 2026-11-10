#!/bin/sh
# Publica una versión firmada de Fósil en GitHub (público) y Gitea (privado).
# Uso: ./publicar.sh X.Y.Z "notas de la versión" [--simular]
# Para en el primer fallo: árbol sucio, versión no mayor, pruebas, privacidad, gitleaks o firma.
set -u
cd "$(dirname "$0")" || exit 1
[ $# -ge 2 ] || { echo "Uso: $0 X.Y.Z \"notas\" [--simular]" >&2; exit 2; }
v=$1; notas=$2; simular=${3:-}
PRIV=${FOSIL_PRIVADO:-../fosil-privado}; GH=${REMOTO_GITHUB:-github}; GT=${REMOTO_GITEA:-gitea}
para() { echo "PARADO: $*" >&2; exit 1; }
printf '%s' "$v" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$' || para "versión no válida: $v"
FOSIL_SOLO_FUNCIONES=1 FOSIL_COMUN=./fosil/comun.sh FOSIL_CONF=/nada FOSIL_ETC=./fosil . ./fosil/fosil-actualizar
unset FOSIL_SOLO_FUNCIONES FOSIL_COMUN FOSIL_CONF   # que no lleguen a las pruebas (FOSIL_ETC lo usa verificar_etiqueta)

[ "$(git symbolic-ref -q --short HEAD)" = main ] || para "no estás en main"
[ -z "$(git status --porcelain)" ] || para "hay cambios sin commit"
if git ls-remote --exit-code "$GH" refs/heads/main >/dev/null 2>&1; then
    git pull -q --ff-only "$GH" main || para "main no avanza en línea recta desde GitHub: integra a mano"
fi
ult=$(git tag -l 'v*' | sed -n 's/^v\([0-9]*\.[0-9]*\.[0-9]*\)$/\1/p' | while read -r t; do echo "$t"; done | awk -F. '{printf "%05d%05d%05d %s\n",$1,$2,$3,$0}' | sort | tail -1 | cut -d' ' -f2)
[ -z "$ult" ] || version_mayor "$v" "$ult" || para "v$v es menor o igual que la última (v$ult)"
env -u FOSIL_ETC ${PUBLICAR_PRUEBAS:-sh tests/ejecutar.sh} >/dev/null 2>&1 || para "fallan las pruebas (sh tests/ejecutar.sh)"
[ -r "$PRIV/prohibidas.txt" ] || para "falta $PRIV/prohibidas.txt"
sh herramientas/comprobar-privacidad.sh . "$PRIV/prohibidas.txt" >&2 || para "hay datos personales (arriba)"
if command -v gitleaks >/dev/null 2>&1; then gitleaks git --redact --no-banner . >&2 || para "gitleaks ha encontrado secretos"
elif command -v mise >/dev/null 2>&1; then mise x gitleaks@latest -- gitleaks git --redact --no-banner . >&2 || para "gitleaks ha encontrado secretos"
else para "instala gitleaks"; fi
git -c gpg.format=ssh tag -s "v$v" -m "$notas" || para "no se pudo firmar la etiqueta"
verificar_etiqueta . "v$v" || { git tag -d "v$v" >/dev/null; para "la firma no se verifica con fosil/firmantes"; }
if [ "$simular" = --simular ]; then git tag -d "v$v" >/dev/null; echo "simulación correcta: v$v se publicaría"; exit 0; fi
git push -q "$GH" main "v$v" || para "no se pudo subir a GitHub"
git push -q "$GT" main "v$v" || para "subido a GitHub, pero no a Gitea"
echo "Publicada v$v"
