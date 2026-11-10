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
foto() { PATH="$T/bin:$PATH" FOSIL_RAIZ="$RZ" FOSIL_VAR="$T/var" FOSIL_CONF="$T/no-hay" FOSIL_COMUN="$RAIZ_REPO/fosil/comun.sh" sh "$RAIZ_REPO/fosil/fosil-foto" "$@"; }
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
fin
