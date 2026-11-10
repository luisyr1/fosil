#!/bin/sh
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/montaje-actualizar.sh"
F() { PATH="$T/bin:$PATH" FOSIL_SOLO_FUNCIONES=1 FOSIL_CONF="$T/etc/fosil.conf" FOSIL_COMUN="$RAIZ_REPO/fosil/comun.sh" sh -c ". '$RAIZ_REPO/fosil/fosil-actualizar'; $1"; }
sale 0 "0.10.0 > 0.9.1" -- F "version_mayor 0.10.0 0.9.1"
sale 1 "0.4.0 no es > 0.4.0" -- F "version_mayor 0.4.0 0.4.0"
sale 1 "0.3.9 no es > 0.4.0" -- F "version_mayor 0.3.9 0.4.0"
sale 0 "1.0.0 > 0.99.99" -- F "version_mayor 1.0.0 0.99.99"

act comprobar
igual "" "$(disp VERSION)" "sin versión nueva, VERSION vacía"
igual "3" "$(disp LINUX)" "cuenta los paquetes de Linux"
existe "$T/var/actualizar/disponible" "escribe disponible"

version_nueva 0.5.0 "$T/k" "Arreglos de la 0.5"
act comprobar
igual "0.5.0" "$(disp VERSION)" "detecta v0.5.0 firmada"
contiene "Arreglos de la 0.5" "$(cat "$T/var/actualizar/notas")" "guarda las notas"
igual "" "$(disp ERROR)" "sin error"
[ -n "$(disp COMPROBADO)" ] && _ok || _ko "apunta cuándo comprobó"

echo 0.5.0 > "$T/var/version"; version_nueva 0.6.0 "$T/otra"
: > "$T/llamadas"; act comprobar
igual "" "$(disp VERSION)" "rechaza firma de otra llave"
contiene "Versión v0.6.0 sin firma válida: no se instala" "$(disp ERROR)" "explica el rechazo"
contiene "fosil-avisa" "$(cat "$T/llamadas")" "avisa del rechazo"

echo 0.6.0 > "$T/var/version"; echo x >> "$TRABAJO/fosil/a.txt"; git -C "$TRABAJO" commit -qam ligera
git -C "$TRABAJO" tag v0.7.0; git -C "$TRABAJO" push -q "$ORIGEN" main v0.7.0
act comprobar
igual "" "$(disp VERSION)" "rechaza etiqueta ligera"
contiene "sin firma válida" "$(disp ERROR)" "explica el rechazo de la ligera"

echo 0.7.0 > "$T/var/version"
git -C "$TRABAJO" push -q "$ORIGEN" refs/tags/v0.4.0:refs/tags/v0.9.0
act comprobar
igual "" "$(disp VERSION)" "rechaza etiqueta con otro nombre dentro"

echo 0.9.0 > "$T/var/version"
act comprobar
igual "" "$(disp VERSION)" "sin nada mayor que lo instalado"
mkdir -p "$T/run/actualizar.lock"; rm -f "$T/var/actualizar/disponible"
sale 0 "comprobar durante una instalación" -- act comprobar
no_existe "$T/var/actualizar/disponible" "no toca nada mientras se instala"
rmdir "$T/run/actualizar.lock"
# Sin REPO configurado lo dice claro.
cp "$T/etc/fosil.conf" "$T/etc/fosil.conf.antes"; echo "REPO=" > "$T/etc/fosil.conf"
act comprobar
contiene "Falta el repositorio de actualizaciones" "$(disp ERROR)" "explica que falta REPO"
mv "$T/etc/fosil.conf.antes" "$T/etc/fosil.conf"
fin
