#!/bin/sh
# Instala Fósil desde una copia del repositorio. Idempotente. Se ejecuta como root.
# Uso: instalar.sh <raíz-del-repo> <binario-compilado>
# FOSIL_RAIZ cambia la raíz de destino (pruebas); con la raíz real recarga systemd.
set -u
[ $# -eq 2 ] || { echo "Uso: $0 <raíz-del-repo> <binario>" >&2; exit 2; }
repo=$(cd "$1" && pwd) || exit 2; bin=$2
[ -f "$bin" ] || { echo "No existe el binario $bin" >&2; exit 2; }
R=${FOSIL_RAIZ:-/}; R=${R%/}
FOSIL_DISPOSITIVOS=${FOSIL_DISPOSITIVOS:-$repo/dispositivos}
CONF_DESTINO=$R${FOSIL_CONF:-/etc/fosil/fosil.conf}; FOSIL_CONF=$CONF_DESTINO
. "$repo/fosil/comun.sh"
export R USUARIO HOME_U FOSIL_VAR FOSIL_ETC FOSIL_RUN DISPOSITIVO
f=$repo/fosil
pon() { mkdir -p "$(dirname "$2")" && cp "$1" "$2.nuevo" && chmod "$3" "$2.nuevo" && mv -f "$2.nuevo" "$2"; }

# Binario: el estable solo se crea si falta (fosil-actualizar confirmar lo fija después).
pon "$bin" "$R/usr/local/bin/fosil" 755 || exit 1
[ -e "$R/usr/local/bin/fosil.estable" ] || pon "$bin" "$R/usr/local/bin/fosil.estable" 755

for s in fosil-foto fosil-agente fosil-sesion fosil-avisa fosil-estado fosil-proteger fosil-consumo wifi-sonda inicio fosil-actualizar fosil-proponer; do
    [ -f "$f/$s" ] && { pon "$f/$s" "$R/usr/local/bin/$s" 755 || exit 1; }
done
pon "$f/comun.sh" "$R/usr/local/lib/fosil/comun.sh" 644 || exit 1
pon "$repo/herramientas/comprobar-privacidad.sh" "$R/usr/local/lib/fosil/comprobar-privacidad.sh" 755 || exit 1
pon "$f/palabras.txt" "$R/usr/local/share/fosil/palabras.txt" 644 || exit 1
pon "$f/AGENTES.md" "$R/usr/local/share/fosil/AGENTES.md" 644 || exit 1
pon "$f/firmantes" "$R$FOSIL_ETC/firmantes" 644 || exit 1
[ -e "$CONF_DESTINO" ] || pon "$f/fosil.conf.ejemplo" "$CONF_DESTINO" 644

# Units de systemd: normales, plantillas con el usuario y las del perfil del dispositivo.
u=$R/etc/systemd/system; mkdir -p "$u"
for x in "$f"/*.service "$f"/*.timer; do [ -f "$x" ] && pon "$x" "$u/$(basename "$x")" 644; done
for x in "$f"/*.service.in; do
    [ -f "$x" ] || continue
    sed -e "s|@USUARIO@|$USUARIO|g" -e "s|@HOME@|$HOME_U|g" "$x" > "$u/.$(basename "$x" .in)" && mv -f "$u/.$(basename "$x" .in)" "$u/$(basename "$x" .in)"
done

# Perfil del dispositivo.
p=$R/usr/local/share/fosil/dispositivo
if [ -n "$DISPOSITIVO" ] && [ -d "$repo/dispositivos/$DISPOSITIVO" ]; then
    rm -rf "$p.nuevo" && cp -R "$repo/dispositivos/$DISPOSITIVO" "$p.nuevo" && rm -rf "$p" && mv "$p.nuevo" "$p" || exit 1
    find "$p" -name '*.service' | while read -r x; do pon "$x" "$u/$(basename "$x")" 644; done
else
    echo "AVISO sin perfil de dispositivo (DISPOSITIVO=$DISPOSITIVO)"
fi

# Apps del repo: enlaces en ~/apps; nunca se tocan las carpetas propias.
a=$R$HOME_U/apps; mkdir -p "$a"
for d in "$repo"/apps/*/; do
    [ -d "$d" ] || continue; n=$(basename "$d")
    if [ -L "$a/$n" ] || [ ! -e "$a/$n" ]; then ln -sfn "$HOME_U/fosil/apps/$n" "$a/$n"
    else echo "AVISO apps/$n es una carpeta propia: no se enlaza"; fi
done
chown -h "$USUARIO:" "$a" "$a"/* 2>/dev/null

# Guía de los agentes: Claude y Codex trabajan en ~/taller y la cargan desde ~/AGENTES.md.
hu=$R$HOME_U; mkdir -p "$hu/taller"
ln -sfn /usr/local/share/fosil/AGENTES.md "$hu/AGENTES.md"
[ -e "$hu/taller/CLAUDE.md" ] || printf '@../AGENTES.md\n' > "$hu/taller/CLAUDE.md"
[ -e "$hu/taller/AGENTS.md" ] || [ -L "$hu/taller/AGENTS.md" ] || ln -s ../AGENTES.md "$hu/taller/AGENTS.md"
chown -h "$USUARIO:" "$hu/AGENTES.md" "$hu/taller" "$hu/taller/CLAUDE.md" "$hu/taller/AGENTS.md" 2>/dev/null

# Migraciones pendientes, en orden.
h=$R$FOSIL_VAR/migraciones-hechas; mkdir -p "$(dirname "$h")"; touch "$h"
for m in "$f"/migraciones/[0-9]*.sh; do
    [ -f "$m" ] || continue; n=$(basename "$m")
    grep -qxF "$n" "$h" && continue
    sh "$m" || { echo "Falló la migración $n" >&2; exit 1; }
    echo "$n" >> "$h"
done

if [ -z "$R" ]; then
    systemctl daemon-reload
    systemctl enable --now fosil-actualizar-comprobar.timer 2>/dev/null
    systemctl try-restart fosil-agente fosil-consumo.timer
fi
exit 0
