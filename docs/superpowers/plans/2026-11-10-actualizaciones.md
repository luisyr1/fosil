# Actualizaciones de Fósil: plan de implementación

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Objetivo:** botón Actualizar en el móvil que instala versiones firmadas desde GitHub (Linux y Fósil), un repo de código sin datos personales y PRs desde el móvil.

**Arquitectura:** scripts POSIX `sh` en el móvil (`fosil-actualizar`, `fosil-proponer`, `instalar.sh`) que comparten una biblioteca de configuración (`comun.sh`). Son scripts que se pueden probar en el Mac porque todas las rutas se pueden cambiar con variables de entorno, y `apk`, `systemctl`, `nmcli`, `gh`, `make` y `fosil-foto` se sustituyen en las pruebas. La interfaz en C solo lee ficheros de estado y arranca units de systemd. En el PC, `publicar.sh` firma y sube.

**Tecnología:** POSIX sh (busybox ash en el móvil, `/bin/sh` en macOS), git con firmas SSH (`gpg.format=ssh`, `allowed_signers`), systemd, apk, C con SDL2/GLES2 (ya existe), gh CLI.

**Spec:** `docs/superpowers/specs/2026-11-10-actualizaciones-design.md`

## Restricciones globales

- Nada personal en el repo de código: ni nombre, apellidos, email, alias, SSIDs, IPs de Tailscale, nombres de servidores, la empresa ni el dominio de Gitea. Lo comprueba `herramientas/comprobar-privacidad.sh` con la lista `../fosil-privado/prohibidas.txt`.
- Scripts del móvil en POSIX sh, sin bashismos. Tienen que funcionar en busybox ash y en el `/bin/sh` de macOS (para las pruebas).
- Rutas que se pueden cambiar en las pruebas: `FOSIL_CONF` (`/etc/fosil/fosil.conf`), `FOSIL_ETC` (`/etc/fosil`), `FOSIL_VAR` (`/var/lib/fosil`), `FOSIL_RUN` (`/run/fosil`) y `FOSIL_RAIZ` (prefijo de destino de `instalar.sh`, `/` por defecto).
- Versiones: etiquetas `vMAYOR.MENOR.PARCHE` anotadas y firmadas por SSH. La primera es `v0.4.0`.
- Ficheros de estado (decisión de Claude, la spec lo deja al constructor):
  - `$FOSIL_VAR/version`: una línea, `0.4.0`.
  - `$FOSIL_VAR/actualizar/disponible`: líneas `CLAVE=valor`, con `VERSION=0.5.0` (vacía si no hay nada), `LINUX=<n paquetes>`, `COMPROBADO=<epoch>` y `ERROR=<texto>`.
  - `$FOSIL_VAR/actualizar/notas`: el mensaje de la etiqueta.
  - `$FOSIL_VAR/actualizar/estado`: una línea `paso|texto`. `paso` es uno de `esperando`, `comprobando`, `punto`, `cambios`, `compilando`, `linux`, `instalando`, `reiniciando`, `hecho` o `error`.
  - `$FOSIL_VAR/actualizar/reiniciar`: existe si hace falta reiniciar el móvil.
  - `$FOSIL_VAR/actualizar/ultimo.log`: el registro completo.
- Umbrales: batería ≥ 30 % o cargando, ≥ 300 MB libres en `/`, búsqueda cada 6 h (y 5 min después de arrancar), confirmación de que la versión funciona a los 60 s.
- Textos que verá la persona, en español, en el mismo tono que Fósil. Los fija la spec:
  - «Versión vX sin firma válida: no se instala»
  - «Hay cambios locales que chocan con vX: no se ha actualizado»
  - «Actualizado a vX»
  - «Reinicia el móvil para terminar»
- Licencia MIT (`LICENSE`, «Copyright (c) 2026 Los autores de Fósil»). `fosil/palabras.txt` sigue siendo CC BY-SA 4.0.
- Antes de editar `fosil.c` hay que tener presente que el móvil tiene cambios de agentes que el repo no recoge. Las ediciones de la tarea 10 se hacen sobre la base `050f0e9`, y en la tarea 11 se fusionan con lo que traiga el móvil.

## Puntos de revisión

1. **Etiqueta firmada con una llave que no está en `firmantes`, o etiqueta ligera sin firma.** Se tiene que rechazar igual que una firma rota (prueba en la tarea 5).
2. **Se va la red a mitad de `instalar` (después del fetch y antes de `apk upgrade`).** Tiene que terminar en `error` con el texto del fallo, sin haber tocado `/usr/local` (prueba en la tarea 6, con un `apk` sustituto que falla).
3. **Se pulsa Actualizar dos veces, o la búsqueda del timer coincide con una instalación.** Un único bloqueo (`$FOSIL_RUN/actualizar.lock`, con `mkdir` atómico) hace que la segunda ejecución salga con «Ya hay una actualización en marcha» (prueba en la tarea 6).
4. **`~/fosil` con ficheros sin commit, borrados o con permisos de root** (por ejemplo, un agente que compiló con sudo). El paso de cambios locales tiene que hacer commit de todo como el usuario y no fallar por propiedad (prueba en la tarea 6: un fichero sin seguimiento entra en el commit «Cambios locales del móvil»).
5. **Un PR desde el móvil cuyo diff incluye el SSID o la IP del dueño en un fichero nuevo.** `fosil-proponer` tiene que pararse antes de hacer push (prueba en la tarea 7).

---

### Tarea 1: repo limpio, separar lo privado y comprobación de privacidad

**Ficheros:**
- Crear: `~/proyectos/fosil/` (repo de código nuevo, sin historial) con el árbol actual menos `CONTINUAR.md` y `x/`.
- Crear: `~/proyectos/fosil-privado/` con `CONTINUAR.md`, `x/`, `prohibidas.txt` y `movil.env` (`MOVIL=...`).
- Crear: `LICENSE`, `herramientas/comprobar-privacidad.sh`, `tests/lib.sh`, `tests/ejecutar.sh` y `tests/t-privacidad.sh`.
- Modificar: `README.md` (qué es, instalación, contribuir con PR, licencias) y `.gitignore` (añadir `privado/`).
- Mover: `docs/superpowers/` al repo nuevo.

**Interfaces:**
- Produce: `comprobar-privacidad.sh <dir-repo> <lista>`. Sale con 0 si está limpio y con 1 si no, e imprime `fichero:línea:palabra` (o `commit:<hash>:palabra` para el autor y los mensajes). La búsqueda no distingue mayúsculas e ignora las líneas `#` de la lista. Revisa los ficheros de `git ls-files`, los autores y emails y los mensajes de commit.
- Produce: `tests/lib.sh` con `igual <esperado> <obtenido> <descr>`, `contiene <aguja> <pajar> <descr>`, `sale <código> <descr> -- cmd...`, `tmpdir` (crea y devuelve un directorio temporal que se borra al salir) y `fin` (resumen; sale con 1 si algo ha fallado). `tests/ejecutar.sh` ejecuta todos los `tests/t-*.sh` y suma los resultados.

- [ ] **Paso 1:** escribir `tests/t-privacidad.sh`. Crea un repo temporal con un fichero que contiene `RedDeCasa` y una lista con `reddecasa`, y comprueba que sale con 1 y que la salida contiene `RedDeCasa`. Con un fichero limpio sale con 0. Con un commit cuyo autor es `x@ejemplo.org` y la lista contiene `ejemplo.org`, sale con 1 y la salida contiene `commit:`.
- [ ] **Paso 2:** `sh tests/ejecutar.sh`. Esperado: falla porque no existe el script.
- [ ] **Paso 3:** implementar `comprobar-privacidad.sh` (`grep -inF -f` sobre `git ls-files -z`, y `git log --format='%H %an %ae %s%n%b'` para los commits).
- [ ] **Paso 4:** `sh tests/ejecutar.sh`. Esperado: todo bien.
- [ ] **Paso 5:** escribir `fosil-privado/prohibidas.txt` con el nombre, apellidos, email, alias y cuentas, los SSIDs de `CONTINUAR.md`, la IP de Tailscale, nombres de servidores, la empresa y el dominio de Gitea. Añadir también las rutas y usos del usuario fijo (home, `User=`, `usuario@`) **sin** el nombre suelto, porque cuadraría con muchas palabras de `palabras.txt`.
- [ ] **Paso 6:** ejecutar `comprobar-privacidad.sh <repo> <repo privado>/prohibidas.txt`. Esperado: solo los usos del nombre de usuario fijo en código, que las tareas 2 y 3 van a quitar. Apuntarlos como lista de trabajo.
- [ ] **Paso 7:** `git init`, configurar `user.name`/`user.email` con la dirección noreply de GitHub del dueño (si todavía no se conoce, `fosil <fosil@users.noreply.github.com>`, y se cambia antes de la tarea 11) y hacer commit. `fosil-privado` es otro repo git aparte.

### Tarea 2: configuración común y quitar el usuario fijo de los scripts y servicios

**Ficheros:**
- Crear: `fosil/comun.sh`, `fosil/fosil.conf.ejemplo` y `tests/t-comun.sh`.
- Renombrar a plantilla: `fosil/fosil-agente.service` y `fosil/fosil-consumo.service` → `*.service.in` (`User=@USUARIO@`, `HOME=@HOME@`, `WorkingDirectory=@HOME@/taller` y el PATH con `@HOME@`).
- Modificar: `fosil/fosil-sesion` (`su ... "$USUARIO"` y escribir `$FOSIL_RUN/binario` con la ruta del binario en cada vuelta del bucle), `fosil/fosil-foto` (exclusiones y `chown` con `$USUARIO`/`$HOME_U`), `fosil/fosil-agente` (comentario con el nombre → «decisión del dueño»), `fosil/AGENTES.md` (sin nombre, nueva estructura `~/fosil/fosil/fosil.c` y versión en `/var/lib/fosil/version`), y `herramientas/shot.sh` y `ctl.sh` (sin destino por defecto: si falta `MOVIL`, error «Define MOVIL=usuario@host»).

**Interfaces:**
- Produce `comun.sh`, que se instala en `/usr/local/lib/fosil/comun.sh` y se carga con `. /usr/local/lib/fosil/comun.sh`. Define:
  - `USUARIO`: del conf, o si no `getent passwd 1000 | cut -d: -f1`.
  - `HOME_U`: el home de `USUARIO`.
  - `REPO`: URL https de GitHub.
  - `FORK`: vacío si no hay.
  - `DISPOSITIVO`: del conf, o si no `fosil_detectar_dispositivo`.
  - `AUTOR_EMAIL`: `fosil@localhost` por defecto.
  - `FOSIL_ETC`, `FOSIL_VAR` y `FOSIL_RUN` con sus valores por defecto.
  - `fosil_perfil <CLAVE> <defecto>`: lee `/usr/local/share/fosil/dispositivo/perfil.conf`, que se puede cambiar con `FOSIL_PERFIL`.
  - `como_usuario <cmd...>`: `su -s /bin/sh "$USUARIO" -c`, citando bien los argumentos.
  - `fosil_detectar_dispositivo`: compara `/proc/device-tree/compatible` con la línea `COMPATIBLE=` de cada `dispositivos/*/perfil.conf`.
- El conf se lee con un analizador `CLAVE=valor` propio: **sin** `.` del fichero (decisión de Claude, para que ese fichero no ejecute código como root). Las claves válidas son `USUARIO`, `REPO`, `FORK`, `DISPOSITIVO` y `AUTOR_EMAIL`.

- [ ] **Paso 1:** escribir `tests/t-comun.sh`. Un conf con `USUARIO=ana` y `REPO=https://ejemplo/fosil` da `USUARIO=ana`. Un conf con la línea `USUARIO=$(touch /tmp/x)` no ejecuta nada (el fichero no existe después). Sin conf, el usuario sale del sustituto de `getent` en el PATH (`ana`). `fosil_perfil BATERIA x` con un perfil de prueba devuelve su valor, y sin la clave devuelve `x`.
- [ ] **Paso 2:** ejecutar las pruebas. Esperado: fallan.
- [ ] **Paso 3:** implementar `comun.sh` y `fosil.conf.ejemplo` (`REPO=https://github.com/<dueño>/fosil`; el propietario real se pone en la tarea 11).
- [ ] **Paso 4:** ejecutar las pruebas. Esperado: pasan.
- [ ] **Paso 5:** hacer los cambios de la lista «Modificar» y volver a ejecutar `comprobar-privacidad.sh`. Esperado: solo quedan los usos de `fosil.c` y de `audio/`.
- [ ] **Paso 6:** commit.

### Tarea 3: perfil de dispositivo surnia

**Ficheros:**
- Crear: `dispositivos/surnia/perfil.conf`, `antes-kernel`, `despues-kernel` y `LEEME.md` (cómo volver al arranque anterior por USB).
- Mover: `audio/*` → `dispositivos/surnia/audio/`, y `fosil-audio.service` → `fosil-audio.service.in`.
- Modificar: `preparar.py` (base desde `sys.argv[1]` o desde `$FOSIL_AUDIO`), `iniciar-audio` y su `LEEME.txt` (rutas desde `comun.sh`: `$HOME_U/fosil/dispositivos/surnia/audio`) y `fosil/fosil-estado` (pantalla desde `fosil_perfil PANTALLA`).
- Test: `tests/t-perfil.sh`.

**Interfaces:**
- Produce `perfil.conf` con estas claves:

  ```
  COMPATIBLE=motorola,surnia
  NOMBRE=Moto E 2015
  BATERIA=/sys/class/power_supply/max170xx_battery
  TECLA_ENCENDIDO=pm8941_pwrkey
  TECLA_BAJAR=pm8941_resin
  PANTALLA=card0-DSI-1
  TERMICA=/sys/class/thermal/thermal_zone0
  DESCRIPCION=Nura · GPU FD307 · 540 × 960
  KERNEL_PAQUETE=<el que dé `apk info -W /boot/vmlinuz` en el móvil; se completa en la tarea 11>
  ```

- Produce `antes-kernel` y `despues-kernel`, que reciben las variables de `comun.sh` y salen con 0 si todo ha ido bien.
  - `antes-kernel`: copia `vmlinuz*`, `initramfs*`, `*.dtb` y `extlinux/extlinux.conf` de `/boot` (cambiable con `FOSIL_BOOT`) a `/boot/anterior/`, sustituyendo la copia previa.
  - `despues-kernel`: si el audio estaba activo (`extlinux.conf` contiene `audio-directo`), vuelve a ejecutar `preparar.py` y restaura su entrada.

- [ ] **Paso 1:** escribir `tests/t-perfil.sh`. Con `FOSIL_BOOT` apuntando a una carpeta temporal con `vmlinuz` y `extlinux/extlinux.conf`, `antes-kernel` deja una copia idéntica en `anterior/`. Si se ejecuta dos veces, sigue habiendo una sola copia. `fosil_detectar_dispositivo` devuelve `surnia` con un `compatible` de prueba que contiene `motorola,surnia\0qcom,msm8916`.
- [ ] **Paso 2:** ejecutar las pruebas. Esperado: fallan.
- [ ] **Paso 3:** implementarlo y mover el audio.
- [ ] **Paso 4:** ejecutar las pruebas. Esperado: pasan. `comprobar-privacidad.sh` solo encuentra ya `fosil.c`.
- [ ] **Paso 5:** commit.

### Tarea 4: `instalar.sh`

**Ficheros:**
- Crear: `fosil/instalar.sh`, `fosil/migraciones/` (vacía, con un `LEEME`) y `tests/t-instalar.sh`.

**Interfaces:**
- Consume: `comun.sh` (tarea 2) y `dispositivos/<d>/` (tarea 3).
- Produce: `instalar.sh <raíz-del-repo> <binario-compilado>`, que se ejecuta como root y respeta `FOSIL_RAIZ`. Es idempotente. Hace lo siguiente:
  - Instala el binario en `usr/local/bin/fosil`, y también en `fosil.estable` si este no existe.
  - Instala en `usr/local/bin` los scripts `fosil-foto`, `fosil-agente`, `fosil-sesion`, `fosil-avisa`, `fosil-estado`, `fosil-proteger`, `fosil-consumo`, `wifi-sonda`, `inicio`, `fosil-actualizar` y `fosil-proponer`.
  - Instala `comun.sh` en `usr/local/lib/fosil/`.
  - Instala `*.service`, `*.timer` y los `*.service.in` (con `@USUARIO@` y `@HOME@` sustituidos) en `etc/systemd/system/`.
  - Crea `etc/fosil/fosil.conf` a partir del ejemplo **solo si no existe**.
  - Copia `etc/fosil/firmantes` desde `fosil/firmantes`.
  - Copia `dispositivos/$DISPOSITIVO/` a `usr/local/share/fosil/dispositivo/`.
  - Para cada `apps/<n>`, si `~/apps/<n>` no existe o ya es un enlace, crea el enlace `~/apps/<n>` → `~/fosil/apps/<n>`. Si es una carpeta de verdad, no la toca y escribe `AVISO apps/<n>`.
  - Ejecuta en orden las migraciones `fosil/migraciones/NNN-*.sh` que no estén apuntadas en `$FOSIL_VAR/migraciones-hechas`, y las apunta.
  - Si `FOSIL_RAIZ` es `/`: `systemctl daemon-reload`, `systemctl enable --now fosil-actualizar-comprobar.timer` y `systemctl try-restart fosil-agente fosil-consumo.timer`.
  - No reinicia `fosil`.

- [ ] **Paso 1:** escribir `tests/t-instalar.sh` con `FOSIL_RAIZ` en una carpeta temporal y `systemctl` sustituido. Comprueba que:
  - después de instalar, `etc/systemd/system/fosil-agente.service` contiene `User=ana` y no contiene `@USUARIO@`;
  - un `fosil.conf` que ya existía no cambia;
  - `~/apps/musica` es un enlace y `~/apps/mia` (carpeta propia) sigue igual;
  - una migración `001-x.sh` que añade una línea a un fichero se ejecuta una sola vez aunque se instale dos veces.
- [ ] **Paso 2:** ejecutar las pruebas. Esperado: fallan.
- [ ] **Paso 3:** implementar `instalar.sh`.
- [ ] **Paso 4:** ejecutar las pruebas. Esperado: pasan.
- [ ] **Paso 5:** commit.

### Tarea 5: `fosil-actualizar comprobar`

**Ficheros:**
- Crear: `fosil/fosil-actualizar` (solo el subcomando `comprobar` y las funciones comunes), `fosil/firmantes` (con la llave pública `~/.ssh/id_ed25519.pub` del dueño y `namespaces="git"`), `fosil/fosil-actualizar-comprobar.service`, `fosil/fosil-actualizar-comprobar.timer` (`OnBootSec=5min`, `OnUnitActiveSec=6h`) y `tests/t-comprobar.sh`.

**Interfaces:**
- Produce estas funciones dentro de `fosil-actualizar`, que reutiliza la tarea 6:
  - `version_mayor <a> <b>`: sale con 0 si a > b, comparando semver numérico en awk. No usa `sort -V`, porque no es seguro que busybox lo tenga.
  - `ultima_etiqueta`: imprime la `vX.Y.Z` más alta de `git ls-remote --tags --refs "$REPO"`.
  - `verificar_etiqueta <dir-git> <vX>`: sale con 0 solo si `git -c gpg.ssh.allowedSignersFile=$FOSIL_ETC/firmantes verify-tag` pasa **y** la cabecera `tag` del objeto es exactamente `<vX>`.
  - `traer_etiqueta <vX>`: como el usuario, `git -C $HOME_U/fosil fetch --no-tags $REPO refs/tags/<vX>:refs/tags/<vX>`.
- `comprobar` escribe `disponible` y `notas` como dicen las restricciones globales. Ignora las versiones iguales o menores que `$FOSIL_VAR/version`. Si la firma no es válida, escribe `VERSION=` vacía y `ERROR=Versión vX sin firma válida: no se instala`, y además ejecuta `fosil-avisa` con ese texto y el origen `actualizaciones`. Para Linux: `apk update -q` y `LINUX=$(apk version -l '<' | tail -n +2 | wc -l)`.

- [ ] **Paso 1:** escribir `tests/t-comprobar.sh`. Prepara un repo «remoto» desnudo y otro como `~/fosil`, una llave de pruebas (`ssh-keygen -t ed25519 -N ''`) y su `firmantes`, y sustitutos de `apk` (que imprime 3 líneas de paquetes) y `fosil-avisa`. Casos:
  - `version_mayor 0.10.0 0.9.1` sale con 0, y `version_mayor 0.4.0 0.4.0` sale con 1.
  - Con `v0.5.0` firmada y la versión instalada `0.4.0`, `disponible` contiene `VERSION=0.5.0` y `LINUX=3`, y `notas` es el mensaje de la etiqueta.
  - `v0.5.0` firmada con otra llave produce `VERSION=` y `ERROR=` con «sin firma válida», y el sustituto de `fosil-avisa` queda llamado.
  - Una etiqueta ligera `v0.6.0` se rechaza de la misma manera.
  - Una etiqueta firmada cuyo objeto dice `tag v0.3.0` pero se publica como `refs/tags/v0.9.0` se rechaza.
  - Si el remoto solo tiene `v0.3.0`, el resultado es `VERSION=`.
- [ ] **Paso 2:** ejecutar las pruebas. Esperado: fallan.
- [ ] **Paso 3:** implementar.
- [ ] **Paso 4:** ejecutar las pruebas. Esperado: pasan.
- [ ] **Paso 5:** commit.

### Tarea 6: `fosil-actualizar instalar`, `linux` y `confirmar`

**Ficheros:**
- Modificar: `fosil/fosil-actualizar` (subcomandos `instalar`, `linux` y `confirmar`).
- Crear: `fosil/fosil-actualizar.service` (`ExecStart=/usr/local/bin/fosil-actualizar instalar`, `Type=oneshot`, sin `TimeoutStartSec`), `fosil/fosil-actualizar-linux.service` (`... linux`) y `tests/t-instalar-version.sh`.

**Interfaces:**
- Consume: las funciones de la tarea 5, `instalar.sh` (tarea 4), `fosil_perfil` (tarea 2), `antes-kernel` y `despues-kernel` (tarea 3) y `$FOSIL_RUN/binario` (tarea 2).
- `instalar` sigue el orden de la spec y escribe `estado` en cada paso. Todo va a `ultimo.log`.
  1. Bloqueo con `mkdir $FOSIL_RUN/actualizar.lock`. Si ya existe: «Ya hay una actualización en marcha» y sale con 1.
  2. Mientras `$FOSIL_VAR/chat/estado` no empiece por `listo`, estado `esperando|Esperando a que el agente termine` y vuelve a mirar cada 5 s.
  3. Comprobaciones previas:
     - batería: `$(fosil_perfil BATERIA)/capacity` ≥ 30, o `status` = `Charging` o `Full`;
     - disco: `df -Pm /`, columna 4, ≥ 300;
     - red: `nmcli -t -f TYPE,STATE dev` tiene que contener `wifi:connected`.

     Si falla alguna, `error|<qué falta>` y no hace nada más.
  4. Vuelve a obtener la versión con `ultima_etiqueta`, la trae y la verifica. Si no es válida, `error`.
  5. `fosil-foto crear "Antes de actualizar a vX"`.
  6. Cambios locales, como el usuario, en `~/fosil`:
     - `git add -A` y commit «Cambios locales del móvil» si hay algo, con autor `fosil <$AUTOR_EMAIL>`;
     - `git rebase --onto vNEW vOLD main`;
     - si falla, `git rebase --abort`, estado `error|Hay cambios locales que chocan con vX: no se ha actualizado`, aviso, y sale.

     Antes, `chown -R` al usuario de `~/fosil`, para el caso de que un agente haya creado ficheros como root.
  7. `make -C ~/fosil/fosil` como el usuario. Si falla, `git reset --hard` a lo que había antes del paso 6 (guardado en `ORIG`) y `error`. Nada instalado.
  8. Linux:
     - si `apk version -l '<'` incluye `$(fosil_perfil KERNEL_PAQUETE)-`, se ejecuta `antes-kernel`;
     - `apk upgrade -q`;
     - si la versión de `apk info -v` del kernel ha cambiado, `despues-kernel` y `touch reiniciar`;
     - si `apk upgrade` actualizó algún paquete, `make -B` otra vez. Es una decisión de Claude: protege de cambios de soname después de compilar en el paso 7.
  9. `instalar.sh ~/fosil ~/fosil/fosil/fosil`.
  10. `reiniciando`: `systemctl restart fosil` y, después, `confirmar vX`.
- `linux`: los pasos 1, 3 y 8, sin el `make`, y al final `comprobar`.
- `confirmar <vX>`: espera 60 s. Si `systemctl is-active fosil` está activo y `$FOSIL_RUN/binario` = `/usr/local/bin/fosil`, copia el binario a `fosil.estable`, escribe `version`, estado `hecho|Actualizado a vX` (o `hecho|Reinicia el móvil para terminar` si existe `reiniciar`), deja vacía la `VERSION` de `disponible`, quita el bloqueo y sale con 0. Si no, escribe `version` de todos modos (los scripts ya son los nuevos), estado `error|La versión vX ha fallado: se usa la estable`, aviso, y sale con 1. Los 60 s se pueden cambiar con `FOSIL_ESPERA_CONFIRMAR` para las pruebas.

- [ ] **Paso 1:** escribir `tests/t-instalar-version.sh`, que reutiliza lo que monta la tarea 5 y sustituye `systemctl`, `apk`, `nmcli`, `make` (crea el binario o falla según `MAKE_FALLA`), `fosil-foto`, `fosil-avisa` y `chown`. Usa `FOSIL_ESPERA_CONFIRMAR=0`. Casos:
  - **Camino feliz:** `estado` termina en `hecho|Actualizado a v0.5.0`, `version` es `0.5.0`, `fosil-foto` se llamó con «Antes de actualizar a v0.5.0» y existe `FOSIL_RAIZ/usr/local/bin/fosil`.
  - **Cambio local sin commit en `fosil/x.txt`:** después hay un commit «Cambios locales del móvil» encima de `v0.5.0`.
  - **Cambio local que choca con `v0.5.0`:** `estado` es `error|Hay cambios locales que chocan con v0.5.0: no se ha actualizado`, `~/fosil` tiene el mismo HEAD y el mismo contenido que antes, y no se ha llamado a `apk upgrade`.
  - **`MAKE_FALLA=1`:** `error`, no existe `usr/local/bin/fosil` y no se ha llamado a `apk upgrade`.
  - **Batería al 12 % y descargando:** `error` cuyo texto contiene `batería`.
  - **Bloqueo ya existente:** sale con 1 y el texto contiene «Ya hay una actualización en marcha».
  - **`apk upgrade` que falla:** `error` y no existe `usr/local/bin/fosil`.
  - **El sustituto de `apk` cambia la versión del kernel:** existe `reiniciar`, el estado final es «Reinicia el móvil para terminar» y se ha llamado a `antes-kernel` y a `despues-kernel` (sustitutos en un perfil de prueba).
  - **`confirmar` con `$FOSIL_RUN/binario` = `/usr/local/bin/fosil.estable`:** sale con 1 y el texto contiene «ha fallado».
- [ ] **Paso 2:** ejecutar las pruebas. Esperado: fallan.
- [ ] **Paso 3:** implementar.
- [ ] **Paso 4:** ejecutar las pruebas. Esperado: pasan.
- [ ] **Paso 5:** commit.

### Tarea 7: `fosil-proponer`

**Ficheros:**
- Crear: `fosil/fosil-proponer` y `tests/t-proponer.sh`.
- Modificar: `fosil/AGENTES.md`, con una sección «Proponer cambios» que explique cuándo usarlo, que lo que sube es público y que saca los cambios de la copia local hasta la siguiente versión.

**Interfaces:**
- Consume: `comun.sh` (`FORK`, `REPO`, `AUTOR_EMAIL`) y `comprobar-privacidad.sh`, que se instala también en el móvil como `/usr/local/lib/fosil/comprobar-privacidad.sh` (hay que añadirlo a la lista de `instalar.sh`).
- `fosil-proponer "<título>" ["<descripción>"] [<commit>...]` se ejecuta como el usuario dentro de `~/fosil`. Hace lo siguiente:
  1. Si `FORK` está vacío o `gh auth status` falla, sale con 2 y el mensaje «Falta configurar FORK y gh auth login».
  2. Commit de lo pendiente, como en la tarea 6.
  3. Elige los commits: los que se le pasen, o si no todos los de `v$(cat $FOSIL_VAR/version)..main`. Si no hay ninguno, «No hay cambios que proponer» y sale con 1.
  4. `git fetch $REPO main` y crea la rama `propuesta/AAAAMMDD-<slug del título>` desde `FETCH_HEAD` con `git cherry-pick`. Si choca, aborta y sale con 1 explicando cuál ha chocado.
  5. Pasa `gitleaks git --log-opts=FETCH_HEAD..HEAD` (si está instalado; si no, avisa de que falta) y una búsqueda de `$FOSIL_ETC/prohibidas.txt` en las líneas añadidas por `git diff FETCH_HEAD..HEAD` y en los autores. Si encuentra algo, borra la rama, imprime lo encontrado y sale con 1 **sin hacer push**.
  6. Hace push a `FORK` y abre el PR con `gh pr create --repo <dueño/repo de REPO> --head <dueño del fork>:<rama> --title --body`.
  7. Quita los commits propuestos de `main` con `git rebase --onto` (o, si los propuso todos, con `git reset --hard v<versión>`).
  8. `fosil-avisa "PR abierto: <url>" proponer`.

- [ ] **Paso 1:** escribir `tests/t-proponer.sh`, con un remoto oficial desnudo, otro de fork y `gh` sustituido (con `pr create`, devuelve una URL de prueba). Casos:
  - Un commit local produce un push de la rama al fork, `main` local igual a `v0.4.0` y el aviso con la URL.
  - Un cambio que añade la palabra `RedDeCasa`, que está en `prohibidas.txt`: sale con 1, el fork no tiene la rama y `main` sigue conservando el commit.
  - Sin `FORK`: sale con 2.
  - Sin cambios: sale con 1 con «No hay cambios que proponer».
  - Proponer solo el primero de dos commits: en `main` queda solo el segundo.
- [ ] **Paso 2:** ejecutar las pruebas. Esperado: fallan.
- [ ] **Paso 3:** implementar y actualizar `AGENTES.md`.
- [ ] **Paso 4:** ejecutar las pruebas. Esperado: pasan.
- [ ] **Paso 5:** commit.

### Tarea 8: `publicar.sh` y `sincronizar.sh` (en el PC)

**Ficheros:**
- Crear: `publicar.sh` y `tests/t-publicar.sh`.
- Reescribir: `sincronizar.sh`.

**Interfaces:**
- `publicar.sh <X.Y.Z> "<notas>" [--simular]` se ejecuta en la raíz del repo de código. Las variables `FOSIL_PRIVADO` (por defecto `../fosil-privado`), `REMOTO_GITHUB` (`github`) y `REMOTO_GITEA` (`gitea`) se pueden cambiar. Hace lo siguiente, y para con un mensaje claro en el primer fallo:
  1. Comprueba que el árbol está limpio y en `main`.
  2. `git pull --ff-only` de GitHub (trae los PRs aceptados) y push a Gitea.
  3. Comprueba que `vX` > la última etiqueta.
  4. `sh tests/ejecutar.sh`.
  5. `comprobar-privacidad.sh . $FOSIL_PRIVADO/prohibidas.txt`.
  6. `gitleaks git --redact --no-banner .`, con `mise x gitleaks@latest --` o, si no hay mise, el binario `gitleaks`.
  7. `git tag -s vX -m "<notas>"`.
  8. `verificar_etiqueta . vX`, reutilizando la función de `fosil-actualizar` con `FOSIL_ETC=fosil`.
  9. Push de `main` y `vX` a los dos remotos. Con `--simular` se para antes del push y borra la etiqueta.
- `sincronizar.sh` necesita `MOVIL`, que sale de `$FOSIL_PRIVADO/movil.env`. Hace `git fetch "ssh://$MOVIL/~/fosil" main:refs/remotes/movil/main` e imprime `git log --oneline main..movil/main`. **No** hace commit ni push: integrar cambios del móvil es una decisión de quien revisa.

- [ ] **Paso 1:** escribir `tests/t-publicar.sh`, con un repo temporal con remotos `github` y `gitea` desnudos, una llave de pruebas como `user.signingkey`, `firmantes` de pruebas y `gitleaks` sustituido. Casos:
  - `--simular 0.4.0` sale con 0, no hay etiqueta en ningún remoto ni en local, y la salida contiene `simulación`.
  - Publicar `0.4.0` deja `v0.4.0` en los dos remotos y verificable.
  - Publicar `0.3.9` después sale con 1 con «menor».
  - Una palabra prohibida en un fichero da salida 1, sin etiqueta.
  - Un árbol con cambios sin commit da salida 1.
- [ ] **Paso 2:** ejecutar las pruebas. Esperado: fallan.
- [ ] **Paso 3:** implementar los dos scripts.
- [ ] **Paso 4:** ejecutar las pruebas. Esperado: pasan.
- [ ] **Paso 5:** commit.

### Tarea 9: mockups de las pantallas para el dueño

**Ficheros:**
- Crear: un artifact HTML (fuera del repo) a 540 × 960 con el estilo de `x/capturas/` (fondo con brillos, tarjetas redondeadas, color de acento de cada tema). Enseña:
  1. Inicio con el aviso fijo «Fósil v0.5.0 disponible» encima de los avisos (y≈760-800), sin tapar los accesos.
  2. Ajustes: la tarjeta «Acerca de» pasa a ser «Fósil v0.4.0» con «Al día» o «v0.5.0 disponible», y al tocarla se abre la pantalla nueva. Es una decisión de Claude: Ajustes ya llena los 960 px.
  3. La pantalla Actualizaciones (`S_UPDATE`), en reposo: versión instalada, disponible y sus notas, «Linux: N paquetes pendientes», la última comprobación y los botones Actualizar, «Buscar ahora» y «Solo Linux».
  4. La misma pantalla durante la actualización: la lista de pasos con el actual resaltado y el texto de `estado`.
  5. El final: «Actualizado a v0.5.0», o «Reinicia el móvil para terminar» con su botón.

- [ ] **Paso 1:** hacer el artifact y publicarlo.
- [ ] **Paso 2:** el dueño lo aprueba o pide cambios. **La tarea 10 no empieza sin esa aprobación.**

### Tarea 10: `fosil.c`: configuración, perfil y pantalla de actualizaciones

**Ficheros:**
- Modificar: `fosil/fosil.c`, sobre la base `050f0e9`:
  - `auth_user` y `HOME_L` (l.1103, 2468);
  - apps (l.474-476) y `su ...` con el usuario fijo (l.2541);
  - batería (l.439), térmica (l.454), teclas (l.590, 2643) y DSI;
  - `draw_settings` (l.1981-2030), `draw_home` (l.1683) y el enum `Screen` (l.690);
  - el despacho del dibujo (l.2580).

**Interfaces:**
- Consume: `/etc/fosil/fosil.conf` (tarea 2), `/usr/local/share/fosil/dispositivo/perfil.conf` (tarea 3), los ficheros de estado de las restricciones globales y las units `fosil-actualizar.service`, `fosil-actualizar-linux.service` y `fosil-actualizar-comprobar.service` (tareas 5 y 6).
- Produce:
  - `static const char *conf(const char *fichero, const char *clave, const char *defecto)`: lee una sola vez y guarda en caché.
  - Al arrancar, `usuario` y `home_u` (con `getpwnam`; si no hay usuario en el conf, el usuario con uid 1000) sustituyen al usuario y al home fijos.
  - `perfil(clave, defecto)` con los valores por defecto genéricos:
    - batería: el primer `/sys/class/power_supply/*` con `type` = `Battery`;
    - teclas: buscar en `/proc/bus/input/devices` por el nombre del perfil o, si no, el primer dispositivo con `KEY_POWER` o `KEY_VOLUMEDOWN`;
    - pantalla: el primer `/sys/class/drm/card0-*` con `status` = `connected`.
  - Pantalla `S_UPDATE`, añadida antes de `S_NCOUNT`. Los botones ejecutan, en un hilo, `systemctl start --no-block <unit>`.
  - El hilo de estado que ya existe lee `version`, `disponible` y `estado` como mucho cada 2 s.
  - Durante la actualización (`estado` distinto de `hecho` y `error`), Fósil no deja pulsar Actualizar otra vez.

- [ ] **Paso 1:** implementar siguiendo los mockups aprobados.
- [ ] **Paso 2:** `comprobar-privacidad.sh`. Esperado: limpio, sin ninguna coincidencia.
- [ ] **Paso 3:** comprobación de sintaxis en el Mac con cabeceras sustitutas, si es posible (`cc -fsyntax-only` con `-I` a stubs mínimos). Si no lo es, se deja para la tarea 11 y se dice.
- [ ] **Paso 4:** commit. La verificación de verdad (compilar y probar en pantalla) es parte de la tarea 11.

### Tarea 11: puesta en marcha (necesita al dueño, el móvil por USB y GitHub)

**Lo que hace el dueño** (se le pide en este orden):
- `! gh auth login -h github.com`;
- crear la cuenta de GitHub del móvil;
- conectar el móvil por USB.

- [ ] **Paso 1:** traer el estado del móvil:
  - `scp` de todo `~/fosil`, `~/apps/*` (solo fuentes) y `~/taller/audio`;
  - `ssh` con `apk info -W /boot/vmlinuz` (para `KERNEL_PAQUETE`) y `cat /proc/device-tree/compatible`.

  Con eso se crea en el repo nuevo la rama `movil` desde un commit con el árbol de `050f0e9` y se le aplica lo traído. Después se fusiona con `main`, se revisa lo que hicieron los agentes y se resuelven los choques en `fosil.c`.
- [ ] **Paso 2:** en el móvil, `make` en una copia temporal. Esperado: compila sin warnings nuevos. Ejecutar `sh tests/ejecutar.sh` en el móvil. Esperado: todo bien con busybox.
- [ ] **Paso 3:** repos.
  - **Gitea** (con las herramientas MCP de Gitea; antes de renombrar, enseñar al dueño lo que se va a hacer): renombrar el repo actual → `fosil-archivo`, crear `fosil` y `fosil-privado`, privados, y hacer push.
  - **GitHub:** `gh repo create fosil --public` y poner `REPO` en `fosil.conf.ejemplo` y el README.
  - Cambiar el autor del repo a la dirección noreply real (`gh api user --jq '"\(.id)+\(.login)@users.noreply.github.com"'`). Mientras GitHub esté vacío se puede rehacer el commit inicial con ese autor.
- [ ] **Paso 4:** `publicar.sh 0.4.0 "Primera versión con actualizaciones" --simular` y, si va bien, sin `--simular`. Comprobar en GitHub (`gh api repos/<dueño>/fosil/contents`) que no están `CONTINUAR.md` ni `x/`.
- [ ] **Paso 5:** primera instalación en el móvil:
  - `fosil-foto crear "Antes de instalar actualizaciones"`;
  - convertir `~/fosil` en un clon: mover el viejo a `~/fosil.antiguo`, `git clone $REPO ~/fosil`, `git checkout -B main v0.4.0`;
  - si hay diferencias con `~/fosil.antiguo`, revisarlas;
  - mover `~/apps/musica` y `~/apps/ytmusic` (las del repo) a `~/apps.antiguo/` para que `instalar.sh` las enlace;
  - escribir `/etc/fosil/fosil.conf` (`USUARIO`, `REPO`, `DISPOSITIVO=surnia`) y `/etc/fosil/prohibidas.txt` (una copia de la lista privada);
  - `echo 0.4.0 > /var/lib/fosil/version`;
  - `make` e `instalar.sh`;
  - con `fosil-agente` en `listo`, reiniciar Fósil, avisando antes con `fosil-avisa`.
- [ ] **Paso 6:** cuenta del móvil:
  - fork del repo y `gh auth login` como el usuario del móvil, con el token mínimo que permita push al fork y PR al repo oficial (comprobarlo con un PR de prueba y cerrarlo);
  - `FORK=` en `fosil.conf`;
  - instalar `gitleaks` en el móvil (`apk add gitleaks`; si no está en el repositorio, el binario estático de aarch64).
- [ ] **Paso 7:** verificación sin cable. Se desconecta el USB y se publica `v0.4.1` (un cambio visible y pequeño, por ejemplo el texto «Acerca de»). En el móvil se pulsa «Buscar ahora», debe salir el aviso en el inicio, se pulsa Actualizar y debe acabar en «Actualizado a v0.4.1». Capturas con `demo 1` de cada pantalla. Después, una etiqueta `v0.4.2` firmada con otra llave en una rama de prueba, que debe dar el aviso «sin firma válida». Se borra después.
- [ ] **Paso 8:** `fosil-proponer` desde el chat del móvil con un cambio de prueba, que debe abrir un PR en GitHub. Se cierra sin aceptarlo.
- [ ] **Paso 9:** actualizar `fosil-privado/CONTINUAR.md` con el nuevo flujo y hacer push a Gitea.
