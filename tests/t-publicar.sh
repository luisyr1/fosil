#!/bin/sh
. "$(dirname "$0")/lib.sh"
tmpdir T
mkdir -p "$T/bin" "$T/privado"
printf '#!/bin/sh\necho "gitleaks $*" >> "%s/llamadas"\nexit 0\n' "$T" > "$T/bin/gitleaks"; chmod +x "$T/bin/gitleaks"
SECRETA="clave$(date +%s)x"; printf '%s\n' "$SECRETA" > "$T/privado/prohibidas.txt"
ssh-keygen -q -t ed25519 -N '' -C pruebas -f "$T/k"
git init -q --bare -b main "$T/github.git"; git init -q --bare -b main "$T/gitea.git"
# Copia del repo real (sin .git) como repo de trabajo, con firmantes y llave de prueba.
mkdir "$T/r"; (cd "$RAIZ_REPO" && git ls-files -z | xargs -0 tar -cf - ) | tar -xf - -C "$T/r"
printf 'pruebas namespaces="git" %s\n' "$(cut -d' ' -f1,2 "$T/k.pub")" > "$T/r/fosil/firmantes"
repo_prueba "$T/r"; git -C "$T/r" config gpg.format ssh; git -C "$T/r" config user.signingkey "$T/k.pub"
git -C "$T/r" add -A; git -C "$T/r" commit -qm base
git -C "$T/r" remote add github "$T/github.git"; git -C "$T/r" remote add gitea "$T/gitea.git"
pub() { (cd "$T/r" && PATH="$T/bin:$PATH" FOSIL_PRIVADO="$T/privado" PUBLICAR_PRUEBAS=true sh ./publicar.sh "$@"); }
etiquetas() { git --git-dir="$1" tag -l; }

sale 0 "simulación" -- pub 0.4.0 "Primera" --simular
contiene "simulación" "$SALIDA" "dice que es una simulación"
# Las pruebas corren limpias: sin las variables de cargar fosil-actualizar como funciones.
printf '#!/bin/sh\n[ -z "${FOSIL_SOLO_FUNCIONES:-}${FOSIL_ETC:-}${FOSIL_CONF:-}" ]\n' > "$T/bin/pruebas-limpias"; chmod +x "$T/bin/pruebas-limpias"
pub_env() { (cd "$T/r" && PATH="$T/bin:$PATH" FOSIL_PRIVADO="$T/privado" PUBLICAR_PRUEBAS=pruebas-limpias sh ./publicar.sh "$@"); }
sale 0 "pruebas sin variables de más" -- pub_env 0.4.0 "Primera" --simular
igual "" "$(etiquetas "$T/github.git")" "simulación: nada en GitHub"
igual "" "$(git -C "$T/r" tag -l)" "simulación: borra la etiqueta local"

sale 0 "publica 0.4.0" -- pub 0.4.0 "Primera"
igual "v0.4.0" "$(etiquetas "$T/github.git")" "etiqueta en GitHub"
igual "v0.4.0" "$(etiquetas "$T/gitea.git")" "etiqueta en Gitea"
git -C "$T/r" -c gpg.ssh.allowedSignersFile="$T/r/fosil/firmantes" verify-tag v0.4.0 >/dev/null 2>&1 && _ok || _ko "etiqueta verificable"
contiene "gitleaks" "$(cat "$T/llamadas")" "pasa gitleaks"

sale 1 "versión menor" -- pub 0.3.9 "Vieja"
contiene "menor" "$SALIDA" "explica que es menor"

echo "$SECRETA" > "$T/r/nota.txt"; git -C "$T/r" add -A; git -C "$T/r" commit -qm nota
sale 1 "palabra prohibida" -- pub 0.5.0 "Con nota"
igual "v0.4.0" "$(git -C "$T/r" tag -l)" "sin etiqueta nueva"
git -C "$T/r" reset -q --hard HEAD~1

echo cambio >> "$T/r/README.md"
sale 1 "árbol sucio" -- pub 0.5.0 "Sucio"
contiene "cambios sin commit" "$SALIDA" "explica el árbol sucio"
fin
