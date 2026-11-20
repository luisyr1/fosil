#!/bin/sh
# fosil-foto con ~/fosil como clon de git: el punto guarda su historia y sus cambios sin commit.
. "$(dirname "$0")/lib.sh"
tmpdir T
RZ=$T/raiz; mkdir -p "$T/bin" "$RZ/etc" "$RZ/usr/local/bin" "$RZ/home/u" "$T/var"
yo=$(id -un)
printf '#!/bin/sh\necho "%s:x:1000:1000::/home/u:/bin/sh"\n' "$yo" > "$T/bin/getent"
for c in systemctl systemd-run apk chown; do printf '#!/bin/sh\nexit 0\n' > "$T/bin/$c"; done
printf '#!/bin/sh\nshift; "$@"\n' > "$T/bin/timeout"
chmod +x "$T/bin/"*
echo "conf 1" > "$RZ/etc/fosil.conf"
C=$RZ/home/u/fosil; repo_prueba "$C" >/dev/null 2>&1
echo base > "$C/a.txt"; git -C "$C" add -A; git -C "$C" commit -qm base
cab=$(git -C "$C" rev-parse HEAD)
echo "sin commit" > "$C/a.txt"; echo nuevo > "$C/b.txt"
foto() { PATH="$T/bin:$PATH" FOSIL_FOTOS_MAX="${MAXF:-}" FOSIL_RAIZ="$RZ" FOSIL_VAR="$T/var" FOSIL_CONF="$T/no-hay" FOSIL_COMUN="$RAIZ_REPO/fosil/comun.sh" sh "$RAIZ_REPO/fosil/fosil-foto" "$@"; }
sale 0 "crea un punto" -- foto crear "Antes de actualizar a v0.5.0"
p=$(foto listar 1 | cut -d'|' -f1)
contiene "Antes de actualizar a v0.5.0" "$(foto listar 1)" "el punto aparece en la lista"
# Después cambia todo: otro commit, otros ficheros y otra configuración.
echo "conf 2" > "$RZ/etc/fosil.conf"
git -C "$C" add -A; git -C "$C" commit -qm "Cambios locales del móvil"; echo roto > "$C/a.txt"
sale 0 "vuelve al punto" -- foto volver "$p"
igual "conf 1" "$(cat "$RZ/etc/fosil.conf")" "vuelve /etc"
igual "$cab" "$(git -C "$C" rev-parse HEAD)" "vuelve la historia de ~/fosil"
igual "sin commit" "$(cat "$C/a.txt")" "vuelve el cambio sin commit"
igual "nuevo" "$(cat "$C/b.txt")" "vuelve el fichero nuevo"
# Poda: con FOSIL_FOTOS_MAX=3 se queda en 3 puntos al pasar de 13; lo podado sale del repo y de ~/fosil.
G() { git --git-dir="$T/var/fotos/.git" "$@"; }
MAXF=3; pc=$(G rev-parse "$p"); cv=$(G log -1 --format=%b "$p" | sed -n 's/^Copia: [0-9a-f]* //p')
existe "$C/.git/refs/fosil-fotos/$cv" "el punto viejo tiene su ref en ~/fosil"
echo "otro árbol" > "$C/c.txt"   # que la copia de ~/fosil no coincida con la del primer punto
echo "solo en puntos viejos" > "$RZ/etc/viejo.txt"; foto crear "Con viejo"; rm "$RZ/etc/viejo.txt"
bv=$(G rev-parse HEAD:etc/viejo.txt)
n=$(G rev-list --count HEAD); while [ "$n" -lt 13 ]; do foto crear "Tarea $n"; n=$((n + 1)); done
igual 13 "$(G rev-list --count HEAD)" "no poda hasta pasar de MAX+10"
echo "conf 3" > "$RZ/etc/fosil.conf"; sale 0 "crea el punto que dispara la poda" -- foto crear "Tarea con conf 3"
igual 3 "$(G rev-list --count HEAD)" "deja los últimos 3 puntos"
igual 3 "$(foto listar 99 | wc -l | tr -d ' ')" "listar enseña 3 puntos"
G cat-file -e "$pc" 2>/dev/null && _ko "el punto podado sigue en el repo" || _ok
G cat-file -e "$bv" 2>/dev/null && _ko "un fichero solo de puntos podados sigue en el repo" || _ok
no_existe "$C/.git/refs/fosil-fotos/$cv" "borra la ref de ~/fosil del punto podado"
for c in $(G log --format=%b HEAD | sed -n 's/^Copia: [0-9a-f]* //p'); do existe "$C/.git/refs/fosil-fotos/$c" "conserva la ref de un punto que queda"; done
sale 2 "no vuelve a un punto podado" -- foto volver "$p"
k=$(foto listar 1 | cut -d'|' -f1)
foto crear "Una más"
igual 4 "$(G rev-list --count HEAD)" "no poda en cada crear"
echo "conf 4" > "$RZ/etc/fosil.conf"; echo cambio > "$C/a.txt"
sale 0 "vuelve a un punto conservado" -- foto volver "$k"
igual "conf 3" "$(cat "$RZ/etc/fosil.conf")" "vuelve /etc tras la poda"
fin
