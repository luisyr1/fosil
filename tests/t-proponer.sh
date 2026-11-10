#!/bin/sh
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/montaje-actualizar.sh"
FORKR="$T/fork.git"; git init -q --bare -b main "$FORKR"
printf 'REPO=%s\nFORK=%s\nAUTOR_EMAIL=movil@prueba.invalid\n' "$ORIGEN" "$FORKR" > "$T/etc/fosil.conf"
printf '# privado\nRedDeCasa\n' > "$T/etc/prohibidas.txt"
cat > "$T/bin/gh" <<X
#!/bin/sh
echo "gh \$*" >> "$T/llamadas"
case "\$1 \$2" in
  "auth status") [ "\${GH_SIN:-0}" = 1 ] && exit 1; exit 0 ;;
  "pr create") echo "https://github.com/prueba/fosil/pull/7" ;;
esac
exit 0
X
# gitleaks y nmcli sustitutos: GITLEAKS_FALLA=1 encuentra un secreto; nmcli conoce la red de casa.
printf '#!/bin/sh\necho "gitleaks $*" >> "%s/llamadas"\n[ "${GITLEAKS_FALLA:-0}" = 1 ] && exit 1\nexit 0\n' "$T" > "$T/bin/gitleaks"
printf '#!/bin/sh\ncase "$*" in *connection*) echo WifiDelSalon ;; esac\nexit 0\n' > "$T/bin/nmcli"
chmod +x "$T/bin/gh" "$T/bin/gitleaks" "$T/bin/nmcli"
L="$HOME_PRUEBA/fosil"
pro() { PATH="$T/bin:$PATH" FOSIL_CONF="$T/etc/fosil.conf" FOSIL_ETC="$T/etc" FOSIL_VAR="$T/var" FOSIL_RUN="$T/run" \
        FOSIL_COMUN="$RAIZ_REPO/fosil/comun.sh" FOSIL_PRIVACIDAD="$RAIZ_REPO/herramientas/comprobar-privacidad.sh" \
        sh -c 'cd "$1" && shift && sh "$0" "$@"' "$RAIZ_REPO/fosil/fosil-proponer" "$L" "$@"; }
ramas_fork() { git --git-dir="$FORKR" for-each-ref --format='%(refname:short)' refs/heads/; }

sale 1 "sin cambios" -- pro "Nada"
contiene "No hay cambios que proponer" "$SALIDA" "explica que no hay cambios"

# Un cambio sin commit: se propone, sale de main y avisa con el enlace.
echo "mejora" > "$L/fosil/b.txt"
: > "$T/llamadas"; sale 0 "propone un cambio" -- pro "Mejora del teclado" "Teclas más grandes"
contiene "propuesta/" "$(ramas_fork)" "sube la rama al fork"
igual "$(git -C "$L" rev-parse 'v0.4.0^{commit}')" "$(git -C "$L" rev-parse main)" "saca el cambio de main"
contiene "pr create" "$(cat "$T/llamadas")" "abre el PR"
contiene "Mejora del teclado" "$(cat "$T/llamadas")" "con el título"
contiene "fosil-avisa PR abierto: https://github.com/prueba/fosil/pull/7" "$(cat "$T/llamadas")" "avisa con el enlace"

# Un cambio con una palabra prohibida: no sube nada y main lo conserva.
echo "red RedDeCasa" > "$L/fosil/c.txt"; git -C "$L" add -A; git -C "$L" commit -qm "Red"
antes=$(ramas_fork); cab=$(git -C "$L" rev-parse main)
sale 1 "palabra prohibida" -- pro "Red"
contiene "RedDeCasa" "$SALIDA" "dice qué encontró"
igual "$antes" "$(ramas_fork)" "no sube nada"
igual "$cab" "$(git -C "$L" rev-parse main)" "main conserva el commit"
igual "1" "$(git -C "$L" branch --list 'propuesta/*' | grep -c .)" "borra la rama local fallida (queda solo la anterior)"
git -C "$L" reset -q --hard v0.4.0

# Solo el primero de dos commits.
echo uno > "$L/fosil/d1.txt"; git -C "$L" add -A; git -C "$L" commit -qm "Uno"; c1=$(git -C "$L" rev-parse HEAD)
echo dos > "$L/fosil/d2.txt"; git -C "$L" add -A; git -C "$L" commit -qm "Dos"
sale 0 "propone solo un commit" -- pro "Solo uno" "" "$c1"
igual "Dos" "$(git -C "$L" log -1 --format=%s main)" "en main queda el segundo"
igual "$(git -C "$L" rev-parse 'v0.4.0^{commit}')" "$(git -C "$L" rev-parse main~1)" "y solo el segundo"

# Sin la lista de palabras (o sin poder leerla) no se propone nada.
echo "otra mejora" > "$L/fosil/d.txt"; antes=$(ramas_fork)
mv "$T/etc/prohibidas.txt" "$T/etc/prohibidas.aparte"
sale 2 "sin lista de palabras" -- pro "Sin lista"
contiene "prohibidas.txt" "$SALIDA" "explica que falta la lista"
igual "$antes" "$(ramas_fork)" "no sube nada sin lista"
: > "$T/etc/prohibidas.txt"; sale 2 "lista vacía" -- pro "Lista vacía"
mv "$T/etc/prohibidas.aparte" "$T/etc/prohibidas.txt"
# Sin gitleaks tampoco.
export GITLEAKS=no-hay-gitleaks; sale 2 "sin gitleaks" -- pro "Sin gitleaks"; unset GITLEAKS
contiene "gitleaks" "$SALIDA" "explica que falta gitleaks"
igual "$antes" "$(ramas_fork)" "no sube nada sin gitleaks"
# Las redes Wi-Fi del móvil cuentan como prohibidas aunque no estén en la lista.
echo "conecta a WifiDelSalon" > "$L/fosil/d.txt"
sale 1 "red del móvil" -- pro "Red del móvil"
contiene "WifiDelSalon" "$SALIDA" "encuentra la red del móvil"
igual "$antes" "$(ramas_fork)" "no sube la red del móvil"
git -C "$L" reset -q --hard main; git -C "$L" clean -qfd

# Sin FORK configurado.
printf 'REPO=%s\n' "$ORIGEN" > "$T/etc/fosil.conf"
sale 2 "sin FORK" -- pro "X"
contiene "Falta configurar FORK" "$SALIDA" "explica qué falta"
fin
