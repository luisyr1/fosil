#!/bin/sh
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/montaje-actualizar.sh"
# Más sustitutos: red, make, chown y un perfil con batería y ganchos del kernel.
cat > "$T/bin/nmcli" <<X
#!/bin/sh
[ "\${NMCLI_SIN:-0}" = 1 ] && echo "wifi:disconnected" || echo "wifi:connected"
X
cat > "$T/bin/make" <<X
#!/bin/sh
echo "make \$*" >> "$T/llamadas"
[ "\${MAKE_FALLA:-0}" = 1 ] && exit 2
while [ \$# -gt 0 ]; do [ "\$1" = -C ] && d=\$2; shift; done
echo "binario nuevo" > "\$d/fosil"
X
printf '#!/bin/sh\nexit 0\n' > "$T/bin/chown"
# systemctl: restart fosil simula que fosil-sesion arranca SESION_BINARIO.
cat > "$T/bin/systemctl" <<X
#!/bin/sh
echo "systemctl \$*" >> "$T/llamadas"
[ "\$*" = "restart fosil" ] && echo "\${SESION_BINARIO:-/usr/local/bin/fosil} \$(date +%s)" > "$T/run/binario"
exit 0
X
chmod +x "$T/bin/"*
mkdir -p "$T/perfil" "$T/bat"; echo 80 > "$T/bat/capacity"; echo Discharging > "$T/bat/status"
printf 'BATERIA=%s\nKERNEL_PAQUETE=linux-postmarketos-qcom-msm8916\n' "$T/bat" > "$T/perfil/perfil.conf"
for g in antes-kernel despues-kernel; do printf '#!/bin/sh\necho "%s" >> "%s/llamadas"\n' "$g" "$T" > "$T/perfil/$g"; chmod +x "$T/perfil/$g"; done
PERFIL_PRUEBA="$T/perfil/perfil.conf"
est() { cat "$T/var/actualizar/estado"; }
L="$HOME_PRUEBA/fosil"

# Camino feliz, con un cambio local sin commit en un fichero nuevo.
version_nueva 0.5.0
echo "mío" > "$L/fosil/x.txt"
: > "$T/llamadas"; sale 0 "actualiza a v0.5.0" -- act instalar
igual "hecho|Actualizado a v0.5.0" "$(est)" "estado final"
igual "0.5.0" "$(cat "$T/var/version")" "versión instalada"
contiene "fosil-foto crear Antes de actualizar a v0.5.0" "$(cat "$T/llamadas")" "punto para volver atrás"
existe "$T/raiz/usr/local/bin/fosil" "instala el binario"
igual "binario nuevo" "$(cat "$T/raiz/usr/local/bin/fosil.estable")" "fija la versión estable"
igual "Cambios locales del móvil" "$(git -C "$L" log -1 --format=%s)" "conserva el cambio local como commit"
igual "$(git -C "$L" rev-parse 'v0.5.0^{commit}')" "$(git -C "$L" rev-parse HEAD~1)" "el cambio va encima de v0.5.0"
igual "" "$(disp VERSION)" "ya no queda versión pendiente"
no_existe "$T/run/actualizar.lock" "suelta el bloqueo"

# Cambio local que choca con la versión nueva.
echo "local" > "$L/fosil/a.txt"; antes=$(git -C "$L" rev-parse HEAD)
version_nueva 0.6.0
: > "$T/llamadas"; sale 1 "choque con cambios locales" -- act instalar
igual "error|Hay cambios locales que chocan con v0.6.0: no se ha actualizado" "$(est)" "explica el choque"
igual "$antes" "$(git -C "$L" rev-parse HEAD)" "mismo HEAD"
igual "local" "$(cat "$L/fosil/a.txt")" "mismo contenido"
no_contiene "apk upgrade" "$(cat "$T/llamadas")" "no toca Linux"
git -C "$L" checkout -q -- fosil/a.txt

# No compila: no se instala nada ni se toca Linux.
rm -f "$T/raiz/usr/local/bin/fosil"
: > "$T/llamadas"; export MAKE_FALLA=1; sale 1 "no compila" -- act instalar; unset MAKE_FALLA
contiene "error|" "$(est)" "estado de error al no compilar"
no_existe "$T/raiz/usr/local/bin/fosil" "nada instalado"
no_contiene "apk upgrade" "$(cat "$T/llamadas")" "no toca Linux si no compila"
igual "$(git -C "$L" rev-parse 'v0.5.0^{commit}')" "$(git -C "$L" rev-parse HEAD~1)" "deja la copia como estaba"

# Batería baja y sin cargar.
echo 12 > "$T/bat/capacity"; sale 1 "batería baja" -- act instalar
contiene "batería" "$(est)" "explica la batería"
echo 80 > "$T/bat/capacity"

# Sin Wi-Fi.
export NMCLI_SIN=1; sale 1 "sin Wi-Fi" -- act instalar; unset NMCLI_SIN
contiene "Wi-Fi" "$(est)" "explica la Wi-Fi"

# Otra actualización en marcha.
mkdir -p "$T/run/actualizar.lock"; sale 1 "bloqueo" -- act instalar
contiene "Ya hay una actualización en marcha" "$SALIDA" "explica el bloqueo"
rmdir "$T/run/actualizar.lock"

# apk upgrade falla: nada instalado.
: > "$T/llamadas"; export APK_FALLA=1; sale 1 "apk falla" -- act instalar; unset APK_FALLA
contiene "error|" "$(est)" "estado de error si apk falla"
no_existe "$T/raiz/usr/local/bin/fosil" "nada instalado si apk falla"
contiene "Linux" "$(est)" "explica que falló Linux"
igual "$(git -C "$L" rev-parse 'v0.5.0^{commit}')" "$(git -C "$L" rev-parse HEAD~1)" "deja la copia como estaba si falla Linux"

# Kernel nuevo: ganchos y aviso de reinicio.
: > "$T/llamadas"; export APK_KERNEL=1; sale 0 "kernel nuevo" -- act instalar; unset APK_KERNEL
igual "hecho|Reinicia el móvil para terminar" "$(est)" "pide reiniciar"
existe "$T/var/actualizar/reiniciar" "apunta el reinicio"
contiene "antes-kernel" "$(cat "$T/llamadas")" "copia el arranque antes"
contiene "despues-kernel" "$(cat "$T/llamadas")" "rehace lo del dispositivo después"
contiene "make -B" "$(cat "$T/llamadas")" "recompila tras actualizar paquetes"

# La versión nueva falla al arrancar y fosil-sesion vuelve a la estable.
rm -f "$T/var/actualizar/reiniciar"; version_nueva 0.7.0; echo "estable previa" > "$T/raiz/usr/local/bin/fosil.estable"
: > "$T/llamadas"; export SESION_BINARIO=/usr/local/bin/fosil.estable; sale 1 "la versión nueva no arranca" -- act instalar; unset SESION_BINARIO
contiene "ha fallado" "$(est)" "explica el fallo"
igual "estable previa" "$(cat "$T/raiz/usr/local/bin/fosil.estable")" "no fija la versión rota como estable"
igual "estable previa" "$(cat "$T/raiz/usr/local/bin/fosil")" "vuelve a poner la estable en su sitio"
igual "0.6.0" "$(cat "$T/var/version")" "no apunta como instalada la versión que falló"
git -C "$L" merge-base --is-ancestor v0.7.0 HEAD && _ko "la copia no se queda en la versión que falló" || _ok
git -C "$L" merge-base --is-ancestor v0.6.0 HEAD && _ok || _ko "la copia vuelve a la versión que sigue instalada"
igual "2" "$(grep -c 'systemctl restart fosil' "$T/llamadas")" "reinicia Fósil con la estable"
# Solo Linux: actualiza paquetes sin tocar Fósil.
: > "$T/llamadas"; sale 0 "solo Linux" -- act linux
igual "hecho|Linux al día" "$(est)" "estado tras solo Linux"
contiene "apk upgrade" "$(cat "$T/llamadas")" "actualiza paquetes"
no_contiene "make" "$(cat "$T/llamadas")" "no compila en solo Linux"
# Como root (los servicios no tienen User=): git no trabaja en la copia del usuario si no es a través de su.
GIT_REAL=$(command -v git)
printf '#!/bin/sh\n[ "$1" = -un ] && { echo root; exit 0; }\n[ "$1" = -u ] && { echo 0; exit 0; }\nexec /usr/bin/id "$@"\n' > "$T/bin/id"
printf '#!/bin/sh\nCOMO_USUARIO=1 exec /bin/sh -c "$5"\n' > "$T/bin/su"
cat > "$T/bin/git" <<X
#!/bin/sh
case " \$* " in *" $L "*|*" $L/"*) [ -n "\${COMO_USUARIO:-}" ] || { echo "fatal: detected dubious ownership in repository at '$L'" >&2; exit 128; } ;; esac
exec "$GIT_REAL" "\$@"
X
chmod +x "$T/bin/id" "$T/bin/su" "$T/bin/git"
cp "$T/etc/fosil.conf" "$T/etc/fosil.conf.antes"; echo "USUARIO=otro" >> "$T/etc/fosil.conf"
rm -f "$T/var/actualizar/reiniciar"; version_nueva 0.8.0 "$T/k" "Notas de root"
act comprobar
igual "0.8.0" "$(disp VERSION)" "como root, verifica la firma en la copia del usuario"
contiene "Notas de root" "$(cat "$T/var/actualizar/notas")" "como root, lee las notas"
: > "$T/llamadas"; sale 0 "como root, instala" -- act instalar
igual "hecho|Actualizado a v0.8.0" "$(est)" "como root, termina bien"
rm -f "$T/bin/id" "$T/bin/su" "$T/bin/git"; mv "$T/etc/fosil.conf.antes" "$T/etc/fosil.conf"
# Las apps del repo también se compilan.
mkdir -p "$TRABAJO/apps/demo"; printf 'demo:\n\ttrue\n' > "$TRABAJO/apps/demo/Makefile"; git -C "$TRABAJO" add -A
version_nueva 0.9.0
: > "$T/llamadas"; sale 0 "actualiza con una app" -- act instalar
contiene "make -C $L/apps/demo" "$(cat "$T/llamadas")" "compila la app del repo"
fin
