# Fósil

Sistema operativo móvil a medida sobre Nura (heredero de postmarketOS): Alpine, musl y systemd.
La interfaz es un único programa en C con SDL2 y GLES2, y lleva un chat integrado para que agentes de IA (Claude Code, Codex) programen el propio móvil.

El primer teléfono soportado es el Motorola Moto E 2015 (`surnia`, MSM8916, 1 GB de RAM). El resto de Fósil es Linux genérico: lo propio de cada teléfono vive en `dispositivos/<código>/`.

## Qué hay aquí

- `fosil/`: interfaz, servicios y scripts que se instalan en el móvil.
- `dispositivos/`: perfiles por teléfono (rutas de batería, botones, pantalla y pasos antes y después de cambiar el kernel).
- `apps/`: apps de ejemplo para **Creaciones**.
- `herramientas/`: pruebas por la FIFO de Fósil y la comprobación de privacidad.
- `tests/`: pruebas de los scripts (`sh tests/ejecutar.sh`).
- `publicar.sh`: crea y sube una versión firmada (lo usa quien mantiene el proyecto).

## Actualizaciones

El móvil busca versiones nuevas en este repositorio cada 6 horas. Cuando hay una, aparece **Actualizar** en el inicio: pone al día Linux (`apk upgrade`) y Fósil (compila en el móvil e instala). Antes guarda un punto para volver atrás en **Tareas**.

Solo se instalan etiquetas `vX.Y.Z` firmadas con una llave de `fosil/firmantes`.

## Contribuir

Los PRs son bienvenidos. Desde un móvil con Fósil, sus agentes pueden proponer cambios con `fosil-proponer "título"`. Antes de subir nada revisa que no haya secretos ni datos personales, y necesita una cuenta de GitHub con su propio fork (ver `fosil/AGENTES.md`).

Todo lo que se sube es público: no incluyas nombres de redes, IPs ni datos personales.

## Licencia

MIT (ver `LICENSE`). `fosil/palabras.txt` procede de [FrequencyWords](https://github.com/hermitdave/FrequencyWords) y mantiene su licencia CC BY-SA 4.0. Está filtrada para dejar solo las palabras en minúscula.
