#!/bin/sh
# Los ficheros generados del audio pasan de ~/taller/audio a /var/lib/fosil/audio.
o="$R$HOME_U/taller/audio"; d="$R$FOSIL_VAR/audio"
[ -d "$o" ] || exit 0
mkdir -p "$d"
for f in original.dtb surnia-audio.dtb extlinux.original.conf extlinux.audio.conf resultado.log; do
    [ -f "$o/$f" ] && [ ! -e "$d/$f" ] && cp -p "$o/$f" "$d/$f"
done
exit 0
