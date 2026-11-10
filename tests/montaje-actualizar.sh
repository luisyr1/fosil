# Montaje común para las pruebas de fosil-actualizar. Se carga después de lib.sh.
# Deja: $T, $ORIGEN (repo desnudo «GitHub»), $TRABAJO (donde se crean versiones), $HOME_PRUEBA/fosil (copia del móvil),
# llaves $T/k (válida) y $T/otra (no válida), sustitutos en $T/bin y la función act (ejecuta fosil-actualizar).
tmpdir T
mkdir -p "$T/bin" "$T/etc" "$T/var/chat" "$T/run" "$T/home"
HOME_PRUEBA="$T/home"; ORIGEN="$T/origen.git"; TRABAJO="$T/trabajo"
ssh-keygen -q -t ed25519 -N '' -C pruebas -f "$T/k"; ssh-keygen -q -t ed25519 -N '' -C otra -f "$T/otra"
printf 'pruebas namespaces="git" %s\n' "$(cut -d' ' -f1,2 "$T/k.pub")" > "$T/etc/firmantes"
printf 'REPO=%s\n' "$ORIGEN" > "$T/etc/fosil.conf"
yo=$(id -un)
cat > "$T/bin/getent" <<X
#!/bin/sh
echo "$yo:x:1000:1000::$HOME_PRUEBA:/bin/sh"
X
for c in fosil-avisa fosil-foto systemctl; do
    printf '#!/bin/sh\necho "%s $*" >> "%s/llamadas"\n' "$c" "$T" > "$T/bin/$c"
done
# apk: 3 paquetes pendientes; APK_FALLA=1 hace fallar upgrade; APK_KERNEL=1 cambia el kernel en upgrade.
cat > "$T/bin/apk" <<X
#!/bin/sh
echo "apk \$*" >> "$T/llamadas"
case "\$1" in
  update) exit 0 ;;
  version) printf 'Installed: Available:\nlinux-postmarketos-qcom-msm8916-6.12-r0 < 6.13-r0\nmesa-1 < 2\nsdl2-1 < 2\n' ;;
  upgrade) [ "\${APK_FALLA:-0}" = 1 ] && exit 1; [ "\${APK_KERNEL:-0}" = 1 ] && echo 6.13 > "$T/kernel"; exit 0 ;;
  info) echo "linux-postmarketos-qcom-msm8916-\$(cat "$T/kernel" 2>/dev/null || echo 6.12)-r0" ;;
esac
exit 0
X
chmod +x "$T/bin/"*
# Repo de trabajo con una versión firmada v0.4.0 y la copia del móvil en esa versión.
git init -q --bare -b main "$ORIGEN"
repo_prueba "$TRABAJO"; git -C "$TRABAJO" config gpg.format ssh; git -C "$TRABAJO" config user.signingkey "$T/k.pub"
mkdir -p "$TRABAJO/fosil"; echo "base" > "$TRABAJO/fosil/a.txt"
# instalar.sh sustituto dentro de la versión: apunta la llamada y deja el binario en la raíz de prueba.
printf '#!/bin/sh\necho "instalar $*" >> "%s/llamadas"\nmkdir -p "$FOSIL_RAIZ/usr/local/bin" && cp "$2" "$FOSIL_RAIZ/usr/local/bin/fosil"\n' "$T" > "$TRABAJO/fosil/instalar.sh"
git -C "$TRABAJO" add -A; git -C "$TRABAJO" commit -qm base
git -C "$TRABAJO" tag -s v0.4.0 -m "Versión 0.4.0"
git -C "$TRABAJO" push -q "$ORIGEN" main v0.4.0
git clone -q "$ORIGEN" "$HOME_PRUEBA/fosil"; repo_prueba "$HOME_PRUEBA/fosil" >/dev/null 2>&1
git -C "$HOME_PRUEBA/fosil" checkout -q -B main v0.4.0
echo 0.4.0 > "$T/var/version"; echo "listo claude si total" > "$T/var/chat/estado"
# version_nueva VERSION [llave] [notas]: crea un commit y una etiqueta firmada y los sube.
version_nueva() {
    echo "$1" >> "$TRABAJO/fosil/a.txt"; git -C "$TRABAJO" commit -qam "Cambio $1"
    git -C "$TRABAJO" -c user.signingkey="${2:-$T/k}.pub" tag -s "v$1" -m "${3:-Notas de $1}"
    git -C "$TRABAJO" push -q "$ORIGEN" main "v$1"
}
act() { PATH="$T/bin:$PATH" FOSIL_RAIZ="$T/raiz" FOSIL_ESPERA_CONFIRMAR=0 FOSIL_ESPERA_CHAT=0 FOSIL_MIN_VIVO=0 FOSIL_CONF="$T/etc/fosil.conf" FOSIL_ETC="$T/etc" FOSIL_VAR="$T/var" FOSIL_RUN="$T/run" \
        FOSIL_PERFIL="${PERFIL_PRUEBA:-$T/no-hay}" FOSIL_COMUN="$RAIZ_REPO/fosil/comun.sh" sh "$RAIZ_REPO/fosil/fosil-actualizar" "$@"; }
disp() { sed -n "s/^$1=//p" "$T/var/actualizar/disponible"; }
