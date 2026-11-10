#!/bin/sh
. "$(dirname "$0")/lib.sh"
D="$RAIZ_REPO/dispositivos/surnia"
tmpdir T
mkdir -p "$T/boot/extlinux" "$T/var" "$T/bin"
printf 'kernel-viejo' > "$T/boot/vmlinuz"; printf 'initrd' > "$T/boot/initramfs"; printf 'dtb' > "$T/boot/msm8916-motorola-surnia.dtb"
printf 'default audio-directo\nlabel postmarketOS\nlabel audio-directo\n' > "$T/boot/extlinux/extlinux.conf"
# Sustitutos: python3 apunta su llamada y genera lo que generaría preparar.py.
cat > "$T/bin/python3" <<'X'
#!/bin/sh
echo "$@" >> "$LLAMADAS"; o=$2; printf 'nuevo-dtb' > "$o/surnia-audio.dtb"; printf 'label audio-directo\n' > "$o/extlinux.audio.conf"
X
cat > "$T/bin/getent" <<'X'
#!/bin/sh
echo "ana:x:1000:1000::/home/ana:/bin/sh"
X
chmod +x "$T/bin/"*
ent() { PATH="$T/bin:$PATH" FOSIL_COMUN="$RAIZ_REPO/fosil/comun.sh" FOSIL_CONF=/nada FOSIL_BOOT="$T/boot" FOSIL_VAR="$T/var" LLAMADAS="$T/llamadas" "$@"; }

ent sh "$D/antes-kernel"
igual "kernel-viejo" "$(cat "$T/boot/anterior/vmlinuz")" "copia el kernel anterior"
existe "$T/boot/anterior/extlinux/extlinux.conf" "copia extlinux"
existe "$T/var/audio/activo" "apunta que el audio estaba activo"
printf 'kernel-2' > "$T/boot/vmlinuz"
ent sh "$D/antes-kernel"
igual "kernel-2" "$(cat "$T/boot/anterior/vmlinuz")" "la segunda vez sustituye la copia"
igual "0" "$(find "$T/boot/anterior" -mindepth 1 -name 'anterior*' | wc -l | tr -d ' ')" "no anida copias"

# El kernel nuevo regenera extlinux sin la entrada de audio.
printf 'default postmarketOS\nlabel postmarketOS\n' > "$T/boot/extlinux/extlinux.conf"
printf 'viejo' > "$T/var/audio/original.dtb"; printf 'viejo' > "$T/var/audio/extlinux.original.conf"
ent sh "$D/despues-kernel"
contiene "preparar.py" "$(cat "$T/llamadas")" "vuelve a preparar el audio"
no_existe "$T/var/audio/original.dtb" "descarta el DTB original antiguo"
igual "nuevo-dtb" "$(cat "$T/boot/msm8916-surnia-audio.dtb")" "instala el DTB de audio nuevo"
igual "label audio-directo" "$(cat "$T/boot/extlinux/extlinux.conf")" "restaura la entrada de audio"

# Si el audio no estaba activo, despues-kernel no hace nada.
rm -f "$T/var/audio/activo" "$T/llamadas"
ent sh "$D/despues-kernel"
no_existe "$T/llamadas" "sin audio activo no toca nada"

# Detección del dispositivo con el device tree.
printf 'motorola,surnia\0qcom,msm8916\0' > "$T/compatible"
r=$(PATH="$T/bin:$PATH" FOSIL_CONF=/nada FOSIL_COMPATIBLE="$T/compatible" FOSIL_DISPOSITIVOS="$RAIZ_REPO/dispositivos" sh -c ". '$RAIZ_REPO/fosil/comun.sh'; echo \$DISPOSITIVO")
igual "surnia" "$r" "detecta surnia"
printf 'otro,movil\0' > "$T/compatible"
r=$(PATH="$T/bin:$PATH" FOSIL_CONF=/nada FOSIL_COMPATIBLE="$T/compatible" FOSIL_DISPOSITIVOS="$RAIZ_REPO/dispositivos" sh -c ". '$RAIZ_REPO/fosil/comun.sh'; echo \"[\$DISPOSITIVO]\"")
igual "[]" "$r" "sin perfil, vacío"
fin
