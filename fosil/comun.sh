# Configuración común de Fósil. Se carga con: . /usr/local/lib/fosil/comun.sh
# Lee /etc/fosil/fosil.conf (CLAVE=valor) sin ejecutarlo: solo acepta claves conocidas.
FOSIL_CONF=${FOSIL_CONF:-/etc/fosil/fosil.conf}
FOSIL_ETC=${FOSIL_ETC:-/etc/fosil}
FOSIL_VAR=${FOSIL_VAR:-/var/lib/fosil}
FOSIL_RUN=${FOSIL_RUN:-/run/fosil}
FOSIL_PERFIL=${FOSIL_PERFIL:-/usr/local/share/fosil/dispositivo/perfil.conf}
USUARIO=; REPO=; FORK=; DISPOSITIVO=; AUTOR_EMAIL=

# fosil_leer_conf FICHERO: asigna solo las claves permitidas; el valor nunca se evalúa.
fosil_leer_conf() {
    [ -r "$1" ] || return 0
    while IFS= read -r _l || [ -n "$_l" ]; do
        case "$_l" in ''|'#'*) continue ;; esac
        _k=${_l%%=*}; _v=${_l#*=}
        case "$_k" in USUARIO|REPO|FORK|DISPOSITIVO|AUTOR_EMAIL) eval "$_k=\$_v" ;; esac
    done < "$1"
}

# fosil_perfil CLAVE DEFECTO: valor del perfil del dispositivo instalado.
fosil_perfil() {
    if [ -r "$FOSIL_PERFIL" ]; then
        while IFS= read -r _l || [ -n "$_l" ]; do
            case "$_l" in "$1="*) printf '%s\n' "${_l#*=}"; return 0 ;; esac
        done < "$FOSIL_PERFIL"
    fi
    printf '%s\n' "$2"
}

# fosil_detectar_dispositivo: código del perfil cuyo COMPATIBLE aparece en el device tree.
fosil_detectar_dispositivo() {
    _c=$(tr '\0' '\n' < "${FOSIL_COMPATIBLE:-/proc/device-tree/compatible}" 2>/dev/null) || return 1
    for _p in "${FOSIL_DISPOSITIVOS:-$HOME_U/fosil/dispositivos}"/*/perfil.conf; do
        [ -r "$_p" ] || continue
        _x=$(FOSIL_PERFIL=$_p fosil_perfil COMPATIBLE '')
        [ -n "$_x" ] && printf '%s\n' "$_c" | grep -qxF "$_x" && { basename "$(dirname "$_p")"; return 0; }
    done
    return 1
}

# como_usuario CMD ARG...: ejecuta como USUARIO, conservando los argumentos tal cual.
como_usuario() {
    # Directo si no hay usuario, si ya somos él o si no somos root (su no serviría).
    if [ -z "$USUARIO" ] || [ "$(id -u)" != 0 ] || [ "$(id -un)" = "$USUARIO" ]; then "$@"; return; fi
    _cmd=
    for _a in "$@"; do _cmd="$_cmd '$(printf '%s' "$_a" | sed "s/'/'\\\\''/g")'"; done
    su -s /bin/sh "$USUARIO" -c "$_cmd"
}

fosil_leer_conf "$FOSIL_CONF"
[ -n "$USUARIO" ] || USUARIO=$(getent passwd 1000 2>/dev/null | cut -d: -f1)
HOME_U=$(getent passwd "$USUARIO" 2>/dev/null | cut -d: -f6)
AUTOR_EMAIL=${AUTOR_EMAIL:-fosil@localhost}
[ -n "$DISPOSITIVO" ] || DISPOSITIVO=$(fosil_detectar_dispositivo 2>/dev/null) || DISPOSITIVO=
