export PATH="$HOME/.local/bin:$PATH"
# Terminal de Fósil
if [ -n "$PS1" ]; then
    PS1='\[\e[38;5;223m\]\u@\h\[\e[0m\] \[\e[38;5;110m\]\w\[\e[0m\] \$ '
    case "$(tty 2>/dev/null)" in
        /dev/tty1) echo "Para volver a Fósil: escribe «inicio» o mantén Bajar volumen.";;
    esac
fi
