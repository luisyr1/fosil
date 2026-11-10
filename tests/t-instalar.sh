#!/bin/sh
. "$(dirname "$0")/lib.sh"
tmpdir T
R="$T/raiz"; mkdir -p "$R" "$T/bin" "$T/var"
cat > "$T/bin/getent" <<'X'
#!/bin/sh
echo "ana:x:1000:1000::/home/ana:/bin/sh"
X
cat > "$T/bin/systemctl" <<'X'
#!/bin/sh
echo "systemctl $*" >> "$LLAMADAS"
X
cat > "$T/bin/chown" <<'X'
#!/bin/sh
exit 0
X
chmod +x "$T/bin/"*
echo binario > "$T/fosil-bin"; printf "motorola,surnia\0" > "$T/compatible"
# Migración de prueba en una copia del repo.
cp -R "$RAIZ_REPO" "$T/repo"; rm -rf "$T/repo/.git"
printf '#!/bin/sh\necho una >> "$FOSIL_VAR/migrado"\n' > "$T/repo/fosil/migraciones/900-prueba.sh"
inst() { PATH="$T/bin:$PATH" FOSIL_RAIZ="$R" FOSIL_VAR="$T/var" FOSIL_COMPATIBLE="$T/compatible" LLAMADAS="$T/llamadas" sh "$T/repo/fosil/instalar.sh" "$T/repo" "$T/fosil-bin"; }
mkdir -p "$R/home/ana/apps/mia"; echo "nombre=Mía" > "$R/home/ana/apps/mia/app.conf"

mkdir -p "$R/home/ana/taller/audio"; echo dtb > "$R/home/ana/taller/audio/original.dtb"
sale 0 "primera instalación" -- inst
igual "dtb" "$(cat "$R$T/var/audio/original.dtb" 2>/dev/null)" "migra los ficheros del audio"
igual "binario" "$(cat "$R/usr/local/bin/fosil")" "instala el binario"
igual "binario" "$(cat "$R/usr/local/bin/fosil.estable")" "crea fosil.estable si no existe"
existe "$R/usr/local/bin/fosil-foto" "instala los scripts"
existe "$R/usr/local/lib/fosil/comun.sh" "instala comun.sh"
existe "$R/usr/local/lib/fosil/comprobar-privacidad.sh" "instala comprobar-privacidad.sh"
existe "$R/usr/local/share/fosil/palabras.txt" "instala el diccionario"
s=$(cat "$R/etc/systemd/system/fosil-agente.service")
contiene "User=ana" "$s" "plantilla con el usuario"
contiene "HOME=/home/ana" "$s" "plantilla con el home"
no_contiene "@USUARIO@" "$s" "sin marcadores"
existe "$R/etc/systemd/system/fosil.service" "instala servicios normales"
existe "$R/etc/systemd/system/fosil-audio.service" "instala servicios del perfil"
existe "$R/usr/local/share/fosil/dispositivo/perfil.conf" "instala el perfil"
existe "$R/etc/fosil/firmantes" "instala firmantes"
contiene "REPO=" "$(cat "$R/etc/fosil/fosil.conf")" "crea fosil.conf"
[ -L "$R/home/ana/apps/musica" ] && _ok || _ko "enlaza apps/musica"
igual "/home/ana/fosil/apps/musica" "$(readlink "$R/home/ana/apps/musica")" "destino del enlace"
igual "nombre=Mía" "$(cat "$R/home/ana/apps/mia/app.conf")" "no toca apps propias"
igual "una" "$(cat "$T/var/migrado")" "ejecuta la migración"

echo "REPO=https://mio" > "$R/etc/fosil/fosil.conf"
echo binario2 > "$T/fosil-bin"
rm -f "$R/home/ana/apps/ytmusic"; mkdir -p "$R/home/ana/apps/ytmusic"; echo propia > "$R/home/ana/apps/ytmusic/x"
sale 0 "segunda instalación" -- inst
igual "REPO=https://mio" "$(cat "$R/etc/fosil/fosil.conf")" "no pisa fosil.conf"
igual "binario2" "$(cat "$R/usr/local/bin/fosil")" "actualiza el binario"
igual "binario" "$(cat "$R/usr/local/bin/fosil.estable")" "no pisa fosil.estable"
igual "una" "$(cat "$T/var/migrado")" "la migración no se repite"
igual "propia" "$(cat "$R/home/ana/apps/ytmusic/x")" "no sustituye una carpeta real"
contiene "AVISO apps/ytmusic" "$SALIDA" "avisa de la carpeta real"
no_existe "$T/llamadas" "con FOSIL_RAIZ no llama a systemctl"
# La guía de los agentes: ~/AGENTES.md apunta a la instalada y ~/taller la carga.
rm -f "$R/home/ana/AGENTES.md"; echo "guía vieja" > "$R/home/ana/AGENTES.md"
sale 0 "instala otra vez" -- inst
igual "/usr/local/share/fosil/AGENTES.md" "$(readlink "$R/home/ana/AGENTES.md")" "~/AGENTES.md apunta a la guía instalada"
contiene "@../AGENTES.md" "$(cat "$R/home/ana/taller/CLAUDE.md")" "CLAUDE.md de ~/taller carga la guía"
igual "../AGENTES.md" "$(readlink "$R/home/ana/taller/AGENTS.md")" "AGENTS.md de ~/taller (Codex)"
fin
