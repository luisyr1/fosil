#!/bin/sh
. "$(dirname "$0")/lib.sh"
tmpdir T
mkdir -p "$T/bin"
# getent sustituto: el usuario con uid 1000 es «ana».
cat > "$T/bin/getent" <<'X'
#!/bin/sh
case "$2" in 1000) echo "ana:x:1000:1000::/home/ana:/bin/sh" ;; ana) echo "ana:x:1000:1000::/home/ana:/bin/sh" ;; bea) echo "bea:x:1001:1001::/srv/bea:/bin/sh" ;; *) exit 2 ;; esac
X
chmod +x "$T/bin/getent"
C="$RAIZ_REPO/fosil/comun.sh"
carga() { PATH="$T/bin:$PATH" FOSIL_CONF="$1" FOSIL_PERFIL="${2:-$T/no-existe}" sh -c ". '$C'; $3"; }

printf 'USUARIO=bea\nREPO=https://ejemplo/fosil\n# comentario\nFORK=https://ejemplo/fork\n' > "$T/conf"
igual "bea" "$(carga "$T/conf" "" 'echo $USUARIO')" "USUARIO del conf"
igual "/srv/bea" "$(carga "$T/conf" "" 'echo $HOME_U')" "HOME_U del usuario"
igual "https://ejemplo/fosil" "$(carga "$T/conf" "" 'echo $REPO')" "REPO del conf"
igual "https://ejemplo/fork" "$(carga "$T/conf" "" 'echo $FORK')" "FORK del conf"
igual "fosil@localhost" "$(carga "$T/conf" "" 'echo $AUTOR_EMAIL')" "AUTOR_EMAIL por defecto"

printf 'USUARIO=$(touch %s/pwned)\nREPO=`touch %s/pwned2`\n' "$T" "$T" > "$T/malo"
carga "$T/malo" "" 'echo "$USUARIO"' >/dev/null
no_existe "$T/pwned" "el conf no ejecuta \$(...)"
no_existe "$T/pwned2" "el conf no ejecuta acentos graves"

igual "ana" "$(carga "$T/no-hay-conf" "" 'echo $USUARIO')" "sin conf, el usuario 1000"
igual "/var/lib/fosil" "$(carga "$T/no-hay-conf" "" 'echo $FOSIL_VAR')" "FOSIL_VAR por defecto"

printf 'BATERIA=/sys/x\nNOMBRE=Moto E 2015\n' > "$T/perfil"
igual "/sys/x" "$(carga "$T/conf" "$T/perfil" 'fosil_perfil BATERIA def')" "valor del perfil"
igual "Moto E 2015" "$(carga "$T/conf" "$T/perfil" 'fosil_perfil NOMBRE def')" "valor con espacios"
igual "def" "$(carga "$T/conf" "$T/perfil" 'fosil_perfil TERMICA def')" "valor por defecto"

# como_usuario cita bien los argumentos (su sustituto ejecuta -c con sh).
cat > "$T/bin/su" <<'X'
#!/bin/sh
# su -s /bin/sh USUARIO -c CMD
echo "usuario=$3" > "$SU_LOG"; shift 4; sh -c "$1"
X
printf '#!/bin/sh\ncase "$1" in -u) echo 0 ;; -un) echo root ;; *) exec /usr/bin/id "$@" ;; esac\n' > "$T/bin/id"
chmod +x "$T/bin/su" "$T/bin/id"
r=$(SU_LOG="$T/su.log" carga "$T/conf" "" "como_usuario printf '%s|' 'a b' \"c'd\"")
igual "a b|c'd|" "$r" "como_usuario conserva argumentos"
contiene "usuario=bea" "$(cat "$T/su.log")" "como_usuario usa USUARIO"
rm -f "$T/bin/id"; : > "$T/su.log"
igual "x" "$(SU_LOG="$T/su.log" carga "$T/conf" "" "como_usuario echo x")" "sin ser root, directo"
igual "" "$(cat "$T/su.log")" "sin ser root no usa su"
fin
