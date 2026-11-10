# Moto E 2015 (surnia)

Perfil de Fósil para el Motorola Moto E 2015 (MSM8916, 1 GB de RAM, 540 × 960).

- `perfil.conf`: rutas de batería, botones, pantalla y zona térmica, y el paquete del kernel.
- `antes-kernel`: antes de cambiar de kernel, guarda el arranque actual en `/boot/anterior/` y apunta si el audio directo estaba en uso.
- `despues-kernel`: si el audio directo estaba en uso, rehace su DTB a partir del kernel nuevo y restaura su entrada de arranque.
- `audio/`: audio directo sin módem (ver `audio/LEEME.txt`).

## Si un kernel nuevo no arranca

El arranque anterior está en `/boot/anterior/`. Hay que restaurarlo desde un sistema que pueda montar la partición de arranque del móvil (por USB, desde otro ordenador). Con esa partición montada como `/boot`:

```sh
cp /boot/anterior/vmlinuz* /boot/anterior/initramfs* /boot/anterior/*.dtb /boot/
cp /boot/anterior/extlinux/extlinux.conf /boot/extlinux/extlinux.conf
sync
```

Y bloquea el kernel con `apk add <paquete>=<versión anterior>` hasta que una versión de Fósil lo arregle.
