# Fósil: actualizaciones desde el móvil, repo publicable y PRs desde el móvil

Fecha: 10 de noviembre de 2026. Estado: borrador para aprobar.

## Por qué

Hoy, para cambiar Fósil hay que conectar el móvil por USB (o entrar por SSH) y desplegar a mano. El dueño quiere que, cada vez que haya una versión nueva, el móvil enseñe un botón **Actualizar** que lo ponga al día todo: Linux (paquetes de Nura/Alpine) y Fósil (interfaz, scripts y servicios). Después de una última conexión por USB para instalar el sistema, no debería volver a hacer falta.

También quiere liberar el proyecto. El código no puede llevar nada personal, y cualquiera con Fósil (empezando por los agentes del propio móvil) tiene que poder proponer cambios con un PR en GitHub.

## Objetivos

1. Botón **Actualizar** en el móvil cuando haya una versión nueva publicada. Actualiza Linux y Fósil de una vez.
2. Sin credenciales del repo oficial en el móvil. El móvil lee GitHub de forma anónima.
3. Solo se instalan versiones firmadas por el dueño. Ni una cuenta de GitHub robada ni un PR malicioso pueden hacer llegar código a un móvil.
4. Una actualización nunca deja el móvil inservible. Antes se guarda un punto de `fosil-foto`, si algo falla se vuelve atrás y se conserva la red de seguridad de `fosil.estable`.
5. Los cambios que hagan los agentes del móvil no se pierden al actualizar.
6. Los agentes de cualquier móvil Fósil pueden abrir un PR contra GitHub.
7. Un repo de código sin datos personales, con historial nuevo, listo para ser público.

## Fuera de alcance

- Actualizaciones automáticas sin pulsar el botón. Siempre decide la persona.
- Imágenes completas del sistema o instalación desde cero (sigue siendo con `pmbootstrap` y USB).
- Canales beta o varias ramas de versiones.
- Hermes y el resto de pendientes de `CONTINUAR.md`.

## Lo que sería un fracaso aunque funcione

- Que un dato personal (nombre, email, redes Wi-Fi, IPs, nombres de servidores, empresa) llegue a GitHub, sea en el código, en el historial o en un PR hecho desde el móvil.
- Que el móvil instale algo que no esté firmado por el dueño.
- Que actualizar borre trabajo de un agente sin avisar.
- Que el botón moleste a diario solo porque Nura edge ha publicado paquetes nuevos.

## Restricciones

- Moto E 2015: 874 MB de RAM y unos 1,8 GB libres. Compilar `fosil.c` en el móvil ya funciona (`make`).
- Nura edge es una distribución continua, y una actualización del kernel puede romper el DTS propio de audio del Moto E.
- Reiniciar Fósil corta la pantalla y pide PIN. El chat de agentes vive en su propio servicio (`fosil-agente`).
- Particiones `modemst1`, `modemst2`, `fsg` y `persist`: no se tocan nunca.
- Los agentes del móvil tienen acceso total (root). Cualquier credencial que se guarde en el móvil se considera leíble por ellos.

## Perfiles de dispositivo (decidido con el dueño)

Fósil es Linux genérico (Nura/postmarketOS). Lo propio de cada teléfono va en `dispositivos/<código>/` (el primero es `surnia`):
- `perfil.conf`: rutas y nombres que hoy están fijos en el código (batería `max170xx_battery`, botones `pm8941_pwrkey` y `pm8941_resin`, pantalla `card0-DSI-1`, zona térmica, resolución en `AGENTES.md`). Sin perfil, Fósil los detecta o usa valores genéricos.
- `antes-kernel` y `despues-kernel`: scripts opcionales que el actualizador ejecuta alrededor de un cambio de kernel.
- Lo que hoy está en `audio/` pasa a `dispositivos/surnia/audio/`.
- `fosil.conf` elige el perfil con `DISPOSITIVO=surnia`. Sin esa clave, se detecta con `/proc/device-tree/compatible`.
- Si un kernel nuevo rompe algo, se arregla en el perfil y llega en la siguiente versión. Un kernel que no arranca no se puede arreglar por el aire. Para ese caso, en `surnia` queda la copia del arranque anterior, y volver a ella por USB queda explicado en el LEEME del perfil.

## Licencia (decidido con el dueño)

MIT, en `LICENSE`, desde el primer commit de GitHub. `fosil/palabras.txt` conserva su CC BY-SA 4.0, y el README lo indica.

## Repos (decidido con el dueño)

| Repo | Dónde | Visibilidad | Contenido |
|---|---|---|---|
| `fosil` | GitHub | Público desde ya | Código limpio. Es la fuente de las actualizaciones y donde llegan los PRs. |
| `fosil` | Gitea | Privado | Copia del mismo repo de código, para trabajar. |
| `fosil-privado` | Gitea | Privado | `CONTINUAR.md`, `x/` (copys y capturas), configuración del móvil del dueño (usuario, IPs, Tailscale) y su lista de palabras prohibidas. |
| `fosil-archivo` | Gitea | Privado | El repo actual con todo su historial, renombrado y sin tocar. |

- El repo de código empieza con historial nuevo. El primer commit lo firma la dirección noreply de GitHub del dueño. Nada del historial actual pasa a él.
- GitHub y Gitea guardan los mismos commits. Los PRs se aceptan en GitHub, conservando a su autor, y el PC sincroniza los dos remotos.
- Publicar en GitHub, sincronizar y firmar versiones lo hace Claude desde el PC del dueño. La firma usa la llave SSH que git ya tiene configurada en ese PC.

## Limpieza del código

- **Usuario configurable.** `/etc/fosil/fosil.conf` (formato `CLAVE=valor`, que se puede leer desde shell) define `USUARIO`, `REPO` (URL de GitHub) y `FORK` (opcional, para PRs). Todo lo que hoy lleva el usuario o su home fijos lo lee de ahí: `fosil.c` (`auth_user`, `HOME_L`, rutas de apps, `su` con el usuario), `fosil-sesion`, `fosil-foto` (sus reglas de exclusión se generan con el usuario), los `.service` (con `User=` en un drop-in o una plantilla que genere el instalador), `audio/*` y `herramientas/*`. Sin `fosil.conf`, el valor por defecto es el usuario con uid 1000.
- `AGENTES.md` habla de «el usuario» o «la persona dueña del móvil». Nunca de un nombre.
- `README.md`: sin URL de Gitea ni nombres. Explica qué es, cómo se instala y cómo se contribuye.
- `herramientas/*.sh` y `sincronizar.sh`: el destino sale de la variable `MOVIL`, sin un valor por defecto con nombre de usuario.
- `CONTINUAR.md` y `x/` salen del repo de código y pasan a `fosil-privado`.
- **Comprobación obligatoria antes de cada subida a GitHub:** `gitleaks` y además una búsqueda de las palabras de `fosil-privado/prohibidas.txt` (nombre, apellidos, email, alias, SSIDs, IPs de Tailscale, nombres de servidores, la empresa, el dominio de Gitea). Si encuentra algo, no se sube.

## Versiones

- Una versión es una **etiqueta git anotada y firmada** con formato `vMAYOR.MENOR.PARCHE` (empezamos en `v0.4.0`). El mensaje de la etiqueta es la nota de versión que se enseña en el móvil (en español, corta).
- La llave pública de quien firma va en el repo (`fosil/firmantes`, con el formato `allowed_signers` de git) y se instala en `/etc/fosil/firmantes`. El móvil verifica siempre con el fichero que **ya tiene instalado**, nunca con el que trae la versión nueva. Así, una versión no puede cambiar quién firma, salvo que venga firmada por la llave anterior.
- `publicar.sh` (en el PC): comprobación de privacidad, etiqueta firmada y subida de la rama y la etiqueta a GitHub y a Gitea.

## Cómo busca versiones el móvil

- `fosil-actualizar comprobar` se ejecuta desde un timer de systemd al arrancar (con unos minutos de margen) y luego cada 6 horas. También desde el botón «Buscar ahora» de Ajustes.
- Se queda con la etiqueta `v*` más alta de `git ls-remote --tags $REPO`. Si es mayor que la instalada (`/var/lib/fosil/version`), la descarga en `~/fosil` (fetch como el usuario) y verifica la firma. Si la firma no es válida, aviso «Versión vX sin firma válida: no se instala» y no hace nada más.
- Linux: `apk update` y `apk version -l '<'` para contar los paquetes pendientes. Lo que se encuentra se escribe en `/var/lib/fosil/actualizar/disponible` (versión, notas, paquetes de Linux pendientes) para que la interfaz lo pinte.
- *Decisión de Claude:* **el botón solo aparece cuando hay una versión nueva de Fósil.** Esa actualización también pone al día Linux. Los paquetes de Linux pendientes sin versión nueva de Fósil se ven en Ajustes, con su propio botón secundario, pero no salen como aviso en el inicio. Así se cumple «una cosa nueva, un botón» sin que edge moleste a diario.

## En la interfaz

- **Inicio:** si hay versión nueva, un aviso fijo «Fósil v0.5.0 disponible», que al tocarlo lleva a la pantalla de Actualización. No se puede descartar mientras siga pendiente, pero tampoco tapa nada.
- **Ajustes → Actualizaciones:** versión instalada, versión disponible con sus notas, paquetes de Linux pendientes, botones **Actualizar**, «Buscar ahora» y «Solo Linux», y la fecha de la última comprobación.
- **Mientras actualiza:** pantalla con los pasos y el paso actual (puntos, Linux, descarga, compilación, instalación, reinicio). Lee el estado de `/var/lib/fosil/actualizar/estado`.
- Al terminar, Fósil se reinicia solo (pide PIN, como siempre) y enseña «Actualizado a v0.5.0». Si ha cambiado el kernel, el mensaje es «Reinicia el móvil para terminar» con un botón para reiniciar.
- El estilo visual sigue al resto de Fósil (las capturas de `x/capturas` son la referencia). El diseño concreto de las pantallas lo decide quien construya, con mockups para que el dueño los vea.

## Qué hace Actualizar

Lo ejecuta `fosil-actualizar instalar`, que corre como root en **su propio servicio de systemd** (`fosil-actualizar.service`). Así puede reiniciar Fósil sin matarse a sí mismo.

1. **Comprobaciones previas:** batería ≥ 30 % o cargando, ≥ 300 MB libres en disco y Wi-Fi conectada. Si falla alguna, aviso explicando qué falta y no se hace nada.
2. **Punto para volver atrás:** `fosil-foto crear "Antes de actualizar a vX"`.
3. **Cambios locales:** `~/fosil` es una copia git del repo. Si un agente ha cambiado algo, se guarda como commit local. Después, como el usuario, se aplican esos commits encima de la etiqueta nueva. Si chocan, se deshace todo en `~/fosil`, sale el aviso «Hay cambios locales que chocan con vX: no se ha actualizado» y la actualización se para antes de tocar Linux. Nunca se pierde nada.
4. **Compilar antes de tocar nada:** compila Fósil (y las apps del repo) en `~/fosil`. Si no compila, se para con un aviso y no se ha instalado nada.
5. **Linux:** `apk upgrade` sin preguntas, kernel incluido (decidido con el dueño). Si va a cambiar el kernel, antes se ejecuta el paso «antes del kernel» del perfil del dispositivo (en el Moto E, guardar una copia del arranque actual en `/boot`). Después se ejecuta su paso «después del kernel» (en el Moto E, volver a aplicar el parche de audio si estaba activo) y se anota que hay que reiniciar.
6. **Instalar Fósil:** lo hace `fosil/instalar.sh` desde la propia versión. Es idempotente: copia binarios y scripts a `/usr/local/bin`, servicios a `/etc/systemd/system`, la configuración por defecto sin pisar la que ya existe, ejecuta `daemon-reload` y las migraciones que traiga cada versión. Las apps de `apps/` del repo se enlazan en `~/apps/` (ver «Decisiones de Claude tras la revisión») sin tocar las apps que haya creado el usuario.
7. **Reiniciar Fósil.** Si la versión nueva arranca y lleva funcionando 60 s, pasa a ser `fosil.estable` y se escribe su número en `/var/lib/fosil/version`. Si falla tres veces, `fosil-sesion` vuelve a `fosil.estable` como hasta ahora, y `fosil-actualizar` lo detecta y avisa de que la versión ha fallado.
8. **Volver a la versión anterior:** desde Tareas, el punto «Antes de actualizar a vX» (con `fosil-foto volver`). Ya existe, solo hay que identificarlo bien.

El registro completo queda en `/var/lib/fosil/actualizar/ultimo.log`.

## PRs desde el móvil (decidido con el dueño)

- **Cuenta de GitHub aparte para el móvil** (por ejemplo `fosil-movil`), que no es la del dueño. Solo tiene su fork de `fosil`. Su token va en el móvil (`gh auth login` como el usuario) y se da por hecho que los agentes pueden leerlo. Lo peor que puede pasar si se filtra es que aparezcan PRs basura. Cualquier otra persona con Fósil usa su propia cuenta y su propio fork.
- El token tendrá los permisos mínimos que permitan subir al fork y abrir un PR en el repo oficial. Quien construya debe comprobar qué tipo de token lo permite.
- Comando `fosil-proponer "título" ["descripción"]`, que ejecuta el usuario (los agentes lo usan desde el chat):
  1. Crea una rama `propuesta/<fecha>-<titulo>` desde el `main` de GitHub con los cambios locales de `~/fosil` (o los commits que elija el agente).
  2. Pasa `gitleaks` y la lista de palabras prohibidas **local del móvil** (`/etc/fosil/prohibidas.txt`, que no va en el repo y escribe cada dueño). Si encuentra algo, no sube nada y explica qué ha encontrado.
  3. Sube la rama al fork (`FORK` en `fosil.conf`) y abre el PR contra el repo oficial con `gh pr create`.
  4. Publica un aviso en Fósil con el enlace al PR.
- `AGENTES.md` explica cuándo y cómo usar `fosil-proponer`, y que lo que sube es público.
- Los PRs los revisa y acepta el dueño (o Claude por encargo suyo) en GitHub. Ningún móvil los instala hasta que salen en una versión firmada.

## Primera instalación (una sola vez, por USB o SSH)

1. Traer al PC el estado actual de `~/fosil` del móvil, que puede tener cambios de agentes que no están en el repo, y revisarlo.
2. Crear los repos de la tabla y hacer la limpieza.
3. En el móvil: escribir `fosil.conf`, `firmantes` y `prohibidas.txt`, convertir `~/fosil` en una copia git del repo de GitHub (conservando lo que haya), instalar `fosil-actualizar`, su timer y su servicio, y hacer de `v0.4.0` la versión instalada.
4. Publicar una `v0.4.1` de prueba y actualizar el móvil con el botón, sin cable, para comprobar todo el circuito.

## Decisiones de Claude tras la revisión

- **Estructura en el móvil:** `~/fosil` pasa a ser la copia completa del repo (`~/fosil/fosil/fosil.c`, `~/fosil/apps/`, `~/fosil/audio/`). Las apps del repo se enlazan desde `~/apps/<nombre>` a `~/fosil/apps/<nombre>` (con un enlace simbólico), y el audio se ejecuta desde `~/fosil/audio`. Así, lo que cambie un agente en las apps o en el audio también queda en git y lo protege el paso 3. Las apps propias del usuario siguen siendo carpetas normales en `~/apps`, y el actualizador nunca las toca. `AGENTES.md`, `sincronizar.sh` y las rutas de audio se cambian a esta estructura, y la primera instalación migra lo que haya hoy.
- **PRs y cambios locales:** `fosil-proponer` **saca** los commits propuestos de la rama local y los deja solo en la rama de la propuesta. Así, la copia local nunca arrastra cambios ya propuestos, y da igual cómo se acepte el PR en GitHub (squash, rebase o merge): la siguiente actualización no se encuentra con choques. Mientras el PR no salga en una versión, ese cambio no está en el binario instalado. El aviso del PR lo explica.
- **Si falla la instalación de Fósil:** no se ejecuta `fosil-foto volver` de forma automática, porque mezclaría un `/etc` antiguo con paquetes ya actualizados. Se vuelve a `fosil.estable`, se avisa y se ofrece el punto en Tareas para volver a mano.
- **Etiquetas:** se rechaza cualquier etiqueta cuyo nombre interno no coincida con la referencia remota y cualquier versión menor o igual que la instalada. Así no se puede forzar una vuelta a una versión antigua.
- **Mientras un agente trabaja:** Actualizar espera a que el chat esté `listo` y lo dice en pantalla.

## Lo que decide quien construya

- El diseño exacto de las pantallas de Actualización y de Ajustes, dentro del estilo de Fósil.
- La estrategia de git para aplicar los cambios locales (rebase o merge), siempre que se cumpla «si choca, se deshace y se avisa».
- Cómo se generan los `.service` con el usuario configurado.
- El formato interno de `disponible`, `estado` y `version`.
- Las pruebas. Como mínimo, una versión sin firma válida que se rechace, una que no compile y se pare antes de instalar, y un choque con cambios locales.
