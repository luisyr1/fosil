// YouTube Music nativo para Fósil: SDL2 para la interfaz, mpv para el audio
// y ytm-servidor.py para buscar en YouTube Music y resolver el audio.
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define W 540
#define H 960
#define MAXP 200
#define SOCK "/tmp/ytmusic-mpv.sock"

typedef struct { char id[16], titulo[160], artista[120], dur[12]; } Pista;

static SDL_Renderer *r;
static TTF_Font *f_txt, *f_peq, *f_gran;
static int sucio = 1;

// ---------------------------------------------------------------- procesos
static pid_t pid_srv, pid_mpv;
static int srv_in = -1, srv_out = -1, mpv_fd = -1;
static char srv_buf[16384], mpv_buf[16384];
static int srv_len, mpv_len;

static void lanza_servidor(void) {
    int a[2], b[2];
    if (pipe(a) || pipe(b)) return;
    pid_srv = fork();
    if (pid_srv == 0) {
        dup2(a[0], 0); dup2(b[1], 1);
        close(a[1]); close(b[0]);
        execlp("python3", "python3", "-u", "ytm-servidor.py", (char *)NULL);
        _exit(127);
    }
    close(a[0]); close(b[1]);
    srv_in = a[1]; srv_out = b[0];
    fcntl(srv_out, F_SETFL, O_NONBLOCK);
}

static void al_servidor(const char *orden, const char *arg) {
    char l[400];
    int n = snprintf(l, sizeof l, "%s %s\n", orden, arg);
    if (srv_in >= 0 && write(srv_in, l, n) < 0) perror("servidor");
}

static void lanza_mpv(int volumen) {
    unlink(SOCK);
    pid_mpv = fork();
    if (pid_mpv == 0) {
        char v[32];
        snprintf(v, sizeof v, "--volume=%d", volumen);
        int nul = open("/dev/null", O_RDWR);
        dup2(nul, 0); dup2(nul, 1); dup2(nul, 2);
        execlp("mpv", "mpv", "--idle=yes", "--no-video", "--no-terminal", "--ytdl=no",
               "--input-ipc-server=" SOCK, v, "--cache=yes", "--demuxer-max-bytes=8MiB",
               "--demuxer-max-back-bytes=2MiB", "--audio-buffer=0.5", (char *)NULL);
        _exit(127);
    }
}

static void a_mpv(const char *json) {
    if (mpv_fd < 0) return;
    char l[4200];
    int n = snprintf(l, sizeof l, "%s\n", json);
    if (n > 0 && n < (int)sizeof l && write(mpv_fd, l, n) < 0) { close(mpv_fd); mpv_fd = -1; }
}

static void conecta_mpv(void) {
    if (mpv_fd >= 0 || !pid_mpv) return;
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un d = { .sun_family = AF_UNIX };
    strcpy(d.sun_path, SOCK);
    if (connect(s, (struct sockaddr *)&d, sizeof d)) { close(s); return; }
    fcntl(s, F_SETFL, O_NONBLOCK);
    mpv_fd = s;
    a_mpv("{\"command\":[\"observe_property\",1,\"time-pos\"]}");
    a_mpv("{\"command\":[\"observe_property\",2,\"duration\"]}");
    a_mpv("{\"command\":[\"observe_property\",3,\"pause\"]}");
}

// Lee lo disponible de fd y llama a f por cada línea completa.
static void lineas(int fd, char *buf, int *len, int cap, void (*f)(char *)) {
    if (fd < 0) return;
    for (;;) {
        int n = read(fd, buf + *len, cap - 1 - *len);
        if (n <= 0) break;
        *len += n;
        char *ini = buf, *nl;
        buf[*len] = 0;
        while ((nl = strchr(ini, '\n'))) { *nl = 0; f(ini); ini = nl + 1; }
        *len -= ini - buf;
        memmove(buf, ini, *len);
        if (*len >= cap - 1) *len = 0;
    }
}

// ---------------------------------------------------------------- estado
static Pista lista[MAXP], cola[MAXP];
static int n_lista, n_cola, actual = -1, ver_cola, cargado_ok;
static char consulta[200], estado[200];
static int buscando, pidiendo_radio, teclado = 1, volumen = 60;
static double pos_s, dur_s, scroll;
static int pausa, sonando;
static Uint32 t_pide;
// Respuestas R/FIN en orden de petición: 'b' búsqueda, 'r' radio.
static char pendientes[16];
static int n_pend;
static Pista recibidas[MAXP];
static int n_recibidas;

static Pista *pista_actual(void) { return actual >= 0 && actual < n_cola ? &cola[actual] : NULL; }

static void pide_url(int i) { if (i >= 0 && i < n_cola) al_servidor("url", cola[i].id); }

static void reproduce(int i) {
    if (i < 0 || i >= n_cola) return;
    actual = i;
    pos_s = dur_s = 0;
    sonando = 0;
    cargado_ok = 0;
    t_pide = SDL_GetTicks();
    snprintf(estado, sizeof estado, "Preparando…");
    a_mpv("{\"command\":[\"stop\"]}");
    pide_url(i);
    sucio = 1;
}

static void siguiente(void) {
    if (actual + 1 < n_cola) reproduce(actual + 1);
    else { a_mpv("{\"command\":[\"stop\"]}"); sonando = 0; actual = -1; snprintf(estado, sizeof estado, "Fin de la cola"); }
}

static void anterior(void) {
    if (pos_s > 5 || actual <= 0) a_mpv("{\"command\":[\"seek\",0,\"absolute\"]}");
    else reproduce(actual - 1);
}

static void pide(char tipo, const char *orden, const char *arg) {
    if (n_pend < (int)sizeof pendientes) pendientes[n_pend++] = tipo;
    al_servidor(orden, arg);
}

static void buscar(void) {
    if (!consulta[0]) return;
    pide('b', "buscar", consulta);
    buscando = 1;
    teclado = 0;
    ver_cola = 0;
    sucio = 1;
}

static void escapa_json(char *dst, int cap, const char *src) {
    int j = 0;
    for (; *src && j < cap - 2; src++) {
        if (*src == '"' || *src == '\\') dst[j++] = '\\';
        dst[j++] = *src;
    }
    dst[j] = 0;
}

static void del_servidor(char *l) {
    sucio = 1;
    if (l[0] == 'R' && l[1] == ' ' && n_recibidas < MAXP) {
        Pista *p = &recibidas[n_recibidas];
        memset(p, 0, sizeof *p);
        char *c[4] = { 0 }, *s = l + 2;
        for (int i = 0; i < 4 && s; i++) { c[i] = s; s = strchr(s, '\t'); if (s) *s++ = 0; }
        if (!c[1]) return;
        snprintf(p->id, sizeof p->id, "%s", c[0]);
        snprintf(p->titulo, sizeof p->titulo, "%s", c[1]);
        snprintf(p->artista, sizeof p->artista, "%s", c[2] ? c[2] : "");
        snprintf(p->dur, sizeof p->dur, "%s", c[3] ? c[3] : "");
        n_recibidas++;
    } else if (!strcmp(l, "FIN")) {
        char tipo = n_pend ? pendientes[0] : 'b';
        if (n_pend) memmove(pendientes, pendientes + 1, --n_pend);
        if (tipo == 'b') {
            memcpy(lista, recibidas, sizeof(Pista) * n_recibidas);
            n_lista = n_recibidas;
            scroll = 0;
            buscando = 0;
            if (!n_lista && !estado[0]) snprintf(estado, sizeof estado, "Sin resultados");
        } else {
            // Radio: añade a la cola lo que no esté ya.
            for (int i = 0; i < n_recibidas && n_cola < MAXP; i++) {
                int rep = 0;
                for (int j = 0; j < n_cola; j++) if (!strcmp(cola[j].id, recibidas[i].id)) rep = 1;
                if (!rep) cola[n_cola++] = recibidas[i];
            }
            if (actual >= 0 && actual + 1 < n_cola && sonando) pide_url(actual + 1);
        }
        n_recibidas = 0;
    } else if (l[0] == 'X') {
        snprintf(estado, sizeof estado, "Sin conexión con YouTube Music");
        fprintf(stderr, "servidor: %s\n", l);
    } else if (l[0] == 'U' || l[0] == 'E') {
        char *id = l + 2, *resto = strchr(id, '\t');
        if (!resto) return;
        *resto++ = 0;
        Pista *p = pista_actual();
        if (!p || strcmp(p->id, id) || sonando || cargado_ok) return;
        if (l[0] == 'E') {
            fprintf(stderr, "no se pudo resolver %s: %s\n", id, resto);
            snprintf(estado, sizeof estado, "No disponible, salto a la siguiente");
            siguiente();
            return;
        }
        static char url[3000], js[4100];
        escapa_json(url, sizeof url, resto);
        snprintf(js, sizeof js, "{\"command\":[\"loadfile\",\"%s\"]}", url);
        a_mpv(js);
        a_mpv("{\"command\":[\"set_property\",\"pause\",false]}");
        cargado_ok = 1;
        snprintf(estado, sizeof estado, "Cargando audio…");
    }
}

static void de_mpv(char *l) {
    char *nombre = strstr(l, "\"name\":\""), *dato = strstr(l, "\"data\":");
    if (strstr(l, "\"event\":\"property-change\"") && nombre && dato) {
        nombre += 8; dato += 7;
        if (!strncmp(nombre, "time-pos\"", 9)) pos_s = strtod(dato, NULL);
        else if (!strncmp(nombre, "duration\"", 9)) dur_s = strtod(dato, NULL);
        else if (!strncmp(nombre, "pause\"", 6)) pausa = !strncmp(dato, "true", 4);
        sucio = 1;
    } else if (strstr(l, "\"event\":\"playback-restart\"") || strstr(l, "\"event\":\"file-loaded\"")) {
        if (!sonando && cargado_ok) {
            sonando = 1;
            estado[0] = 0;
            if (getenv("YTM_FINAL")) a_mpv("{\"command\":[\"seek\",-4,\"absolute\"]}");
            // Precarga la siguiente; si es la última, pide una radio para seguir.
            if (actual + 1 < n_cola) pide_url(actual + 1);
            if (actual >= n_cola - 2 && !pidiendo_radio) { pide('r', "radio", cola[actual].id); pidiendo_radio = 1; }
        }
        sucio = 1;
    } else if (strstr(l, "\"event\":\"end-file\"") && sonando) {
        if (strstr(l, "\"reason\":\"eof\"")) { pidiendo_radio = 0; siguiente(); }
        else if (strstr(l, "\"reason\":\"error\"")) { snprintf(estado, sizeof estado, "Error de audio"); pidiendo_radio = 0; siguiente(); }
    }
}

// ---------------------------------------------------------------- dibujo
typedef struct { char s[200]; TTF_Font *f; Uint32 c; int maxw; SDL_Texture *t; int w, h; Uint32 uso; } Txt;
static Txt cache[160];

static SDL_Texture *texto_tex(const char *s, TTF_Font *f, Uint32 c, int maxw, int *tw, int *th) {
    Uint32 ahora = SDL_GetTicks();
    int libre = 0;
    for (int i = 0; i < 160; i++) {
        Txt *e = &cache[i];
        if (e->t && e->f == f && e->c == c && e->maxw == maxw && !strcmp(e->s, s)) {
            e->uso = ahora; *tw = e->w; *th = e->h; return e->t;
        }
        if (!e->t || e->uso < cache[libre].uso) libre = i;
        if (!e->t) { libre = i; break; }
    }
    char b[204];
    snprintf(b, sizeof b, "%s", s);
    int w = 0, h = 0;
    TTF_SizeUTF8(f, b, &w, &h);
    if (maxw && w > maxw) {
        int n = strlen(b);
        while (n > 0) {
            do n--; while (n > 0 && (b[n] & 0xC0) == 0x80);
            strcpy(b + n, "…");
            TTF_SizeUTF8(f, b, &w, &h);
            if (w <= maxw) break;
        }
    }
    SDL_Color col = { c >> 16, (c >> 8) & 255, c & 255, 255 };
    SDL_Surface *sf = TTF_RenderUTF8_Blended(f, b[0] ? b : " ", col);
    if (!sf) return NULL;
    Txt *e = &cache[libre];
    if (e->t) SDL_DestroyTexture(e->t);
    snprintf(e->s, sizeof e->s, "%s", s);
    e->f = f; e->c = c; e->maxw = maxw; e->uso = ahora;
    e->t = SDL_CreateTextureFromSurface(r, sf);
    e->w = sf->w; e->h = sf->h;
    SDL_FreeSurface(sf);
    *tw = e->w; *th = e->h;
    return e->t;
}

// alin: 0 izquierda, 1 centro, 2 derecha
static void texto(const char *s, TTF_Font *f, int x, int y, Uint32 c, int maxw, int alin) {
    int w, h;
    SDL_Texture *t = texto_tex(s, f, c, maxw, &w, &h);
    if (!t) return;
    SDL_Rect d = { alin == 1 ? x - w / 2 : alin == 2 ? x - w : x, y, w, h };
    SDL_RenderCopy(r, t, NULL, &d);
}

static void caja(int x, int y, int w, int h, Uint32 c) {
    SDL_SetRenderDrawColor(r, c >> 16, (c >> 8) & 255, c & 255, 255);
    SDL_Rect a = { x, y, w, h };
    SDL_RenderFillRect(r, &a);
}

static void triangulo(float x1, float y1, float x2, float y2, float x3, float y3, Uint32 c) {
    SDL_Color col = { c >> 16, (c >> 8) & 255, c & 255, 255 };
    SDL_Vertex v[3] = { { { x1, y1 }, col, { 0, 0 } }, { { x2, y2 }, col, { 0, 0 } }, { { x3, y3 }, col, { 0, 0 } } };
    SDL_RenderGeometry(r, NULL, v, 3, NULL, 0);
}

#define FONDO 0x0f0f0f
#define PANEL 0x1d1d1d
#define TEXTO 0xf1f1f1
#define GRIS  0xa0a0a0
#define ROJO  0xff0033

// Zonas de la pantalla
#define Y_BUSCA 78
#define Y_PEST 148
#define Y_LISTA 200
#define FILA 76
#define Y_REPR 720
#define Y_TECL 600
#define TECLA_H 72

static const char *filas_tecl[] = { "1234567890", "qwertyuiop", "asdfghjklñ", "zxcvbnm" };

static int fondo_lista(void) { return teclado ? Y_TECL : Y_REPR; }

static void hora(char *b, int n, double s) {
    if (s < 0) s = 0;
    snprintf(b, n, "%d:%02d", (int)s / 60, (int)s % 60);
}

static void dibuja(void) {
    caja(0, 0, W, H, FONDO);
    // Cabecera
    triangulo(30, 26, 30, 50, 50, 38, ROJO);
    texto("YouTube Music", f_gran, 64, 18, TEXTO, 0, 0);
    texto("Salir", f_txt, W - 24, 26, GRIS, 0, 2);

    // Buscador
    caja(20, Y_BUSCA, W - 40, 56, teclado ? 0x2c2c2c : PANEL);
    if (consulta[0]) texto(consulta, f_txt, 40, Y_BUSCA + 14, TEXTO, W - 100, 0);
    else texto("Buscar canciones, artistas…", f_txt, 40, Y_BUSCA + 14, GRIS, 0, 0);
    if (teclado && (SDL_GetTicks() / 500) % 2) {
        int w = 0, h;
        if (consulta[0]) texto_tex(consulta, f_txt, TEXTO, W - 100, &w, &h);
        caja(42 + w, Y_BUSCA + 14, 2, 30, TEXTO);
    }

    // Pestañas
    const char *pest[2] = { "Resultados", "Cola" };
    for (int i = 0; i < 2; i++) {
        int x = 20 + i * 150;
        caja(x, Y_PEST, 138, 40, ver_cola == i ? TEXTO : PANEL);
        texto(pest[i], f_peq, x + 69, Y_PEST + 8, ver_cola == i ? FONDO : TEXTO, 0, 1);
    }

    // Lista
    Pista *ps = ver_cola ? cola : lista;
    int n = ver_cola ? n_cola : n_lista, abajo = fondo_lista();
    SDL_Rect clip = { 0, Y_LISTA, W, abajo - Y_LISTA };
    SDL_RenderSetClipRect(r, &clip);
    if (buscando && !ver_cola) texto("Buscando…", f_txt, W / 2, Y_LISTA + 40, GRIS, 0, 1);
    else if (!n) texto(ver_cola ? "La cola está vacía" : "Busca algo para empezar", f_txt, W / 2, Y_LISTA + 40, GRIS, 0, 1);
    for (int i = 0; i < n; i++) {
        int y = Y_LISTA + i * FILA - (int)scroll;
        if (y + FILA < Y_LISTA || y > abajo) continue;
        int es_actual = pista_actual() && !strcmp(ps[i].id, pista_actual()->id) && (!ver_cola || i == actual);
        if (es_actual) caja(0, y, W, FILA, 0x262626);
        caja(20, y + 10, 56, 56, es_actual ? ROJO : 0x303030);
        triangulo(40, y + 26, 40, y + 50, 60, y + 38, TEXTO);
        texto(ps[i].titulo, f_txt, 92, y + 8, es_actual ? ROJO : TEXTO, W - 92 - 80, 0);
        texto(ps[i].artista, f_peq, 92, y + 42, GRIS, W - 92 - 80, 0);
        texto(ps[i].dur, f_peq, W - 22, y + 26, GRIS, 0, 2);
    }
    SDL_RenderSetClipRect(r, NULL);

    if (teclado) {
        caja(0, Y_TECL, W, H - Y_TECL, 0x1a1a1a);
        for (int f = 0; f < 4; f++) {
            int len = SDL_utf8strlen(filas_tecl[f]);
            const char *s = filas_tecl[f];
            for (int k = 0; k < len; k++) {
                char c[5] = { 0 };
                int nb = (*s & 0x80) ? 2 : 1;
                memcpy(c, s, nb); s += nb;
                int x = 2 + k * 53.6, y = Y_TECL + 4 + f * TECLA_H;
                caja(x + 2, y + 3, 50, TECLA_H - 8, 0x3a3a3a);
                texto(c, f_txt, x + 27, y + 18, TEXTO, 0, 1);
            }
            if (f == 3) {
                caja(380, Y_TECL + 4 + 3 * TECLA_H + 3, 156, TECLA_H - 8, 0x2c2c2c);
                texto("Borrar", f_txt, 458, Y_TECL + 4 + 3 * TECLA_H + 18, TEXTO, 0, 1);
            }
        }
        int y = Y_TECL + 4 + 4 * TECLA_H;
        caja(4, y + 3, 120, H - y - 8, 0x2c2c2c);
        texto("Ocultar", f_txt, 64, y + 18, TEXTO, 0, 1);
        caja(130, y + 3, 250, H - y - 8, 0x3a3a3a);
        texto("espacio", f_peq, 255, y + 22, GRIS, 0, 1);
        caja(386, y + 3, 150, H - y - 8, ROJO);
        texto("Buscar", f_txt, 461, y + 18, TEXTO, 0, 1);
        return;
    }

    // Reproductor
    caja(0, Y_REPR, W, H - Y_REPR, PANEL);
    Pista *p = pista_actual();
    texto(p ? p->titulo : "Nada sonando", f_txt, 24, Y_REPR + 16, TEXTO, W - 48, 0);
    char linea[300];
    if (estado[0] && p && !sonando && cargado_ok == 0 && strstr(estado, "Preparando"))
        snprintf(linea, sizeof linea, "Preparando… %u s", (SDL_GetTicks() - t_pide) / 1000);
    else snprintf(linea, sizeof linea, "%s", estado[0] ? estado : p ? p->artista : "");
    texto(linea, f_peq, 24, Y_REPR + 50, estado[0] ? 0xffb0a0 : GRIS, W - 48, 0);
    int by = Y_REPR + 90;
    caja(24, by, W - 48, 4, 0x484848);
    if (dur_s > 0) caja(24, by, (int)((W - 48) * (pos_s / dur_s)), 4, ROJO);
    if (dur_s > 0) caja(24 + (int)((W - 48) * (pos_s / dur_s)) - 6, by - 4, 12, 12, ROJO);
    char a[16], b[16];
    hora(a, sizeof a, pos_s); hora(b, sizeof b, dur_s);
    texto(a, f_peq, 24, by + 12, GRIS, 0, 0);
    texto(b, f_peq, W - 24, by + 12, GRIS, 0, 2);

    // Botones: anterior, play/pausa, siguiente
    int cy = Y_REPR + 175, cx = W / 2;
    triangulo(cx - 100, cy, cx - 76, cy - 16, cx - 76, cy + 16, TEXTO);
    caja(cx - 106, cy - 16, 5, 32, TEXTO);
    caja(cx - 36, cy - 36, 72, 72, TEXTO);
    if (sonando && !pausa) { caja(cx - 13, cy - 16, 9, 32, FONDO); caja(cx + 4, cy - 16, 9, 32, FONDO); }
    else triangulo(cx - 10, cy - 18, cx - 10, cy + 18, cx + 18, cy, FONDO);
    triangulo(cx + 100, cy, cx + 76, cy - 16, cx + 76, cy + 16, TEXTO);
    caja(cx + 101, cy - 16, 5, 32, TEXTO);
    // Volumen
    texto("−", f_gran, 40, cy - 24, TEXTO, 0, 1);
    texto("+", f_gran, W - 40, cy - 24, TEXTO, 0, 1);
    snprintf(a, sizeof a, "%d%%", volumen);
    texto("vol", f_peq, 40, cy + 16, GRIS, 0, 1);
    texto(a, f_peq, W - 40, cy + 16, GRIS, 0, 1);
}

// ---------------------------------------------------------------- entrada
static void borra_letra(void) {
    int n = strlen(consulta);
    if (!n) return;
    do n--; while (n > 0 && (consulta[n] & 0xC0) == 0x80);
    consulta[n] = 0;
}

static void escribe(const char *s) {
    if (strlen(consulta) + strlen(s) < sizeof consulta - 1) strcat(consulta, s);
}

static void cambia_volumen(int d) {
    volumen += d;
    if (volumen < 0) volumen = 0;
    if (volumen > 130) volumen = 130;
    char js[80];
    snprintf(js, sizeof js, "{\"command\":[\"set_property\",\"volume\",%d]}", volumen);
    a_mpv(js);
}

static void toque(int x, int y) {
    sucio = 1;
    if (y < 70 && x > W - 120) { SDL_Event e = { .type = SDL_QUIT }; SDL_PushEvent(&e); return; }
    if (y >= Y_BUSCA && y < Y_BUSCA + 56) { teclado = 1; return; }
    if (y >= Y_PEST && y < Y_PEST + 40) {
        if (x < 170) ver_cola = 0; else if (x < 320) ver_cola = 1;
        scroll = ver_cola && actual > 1 ? (actual - 1) * FILA : 0;
        return;
    }
    if (y >= Y_LISTA && y < fondo_lista()) {
        int i = (y - Y_LISTA + (int)scroll) / FILA;
        if (ver_cola) { if (i < n_cola) reproduce(i); }
        else if (i < n_lista) {
            memcpy(cola, lista, sizeof(Pista) * n_lista);
            n_cola = n_lista;
            pidiendo_radio = 0;
            reproduce(i);
        }
        return;
    }
    if (teclado && y >= Y_TECL) {
        int f = (y - Y_TECL - 4) / TECLA_H;
        if (f < 0) f = 0;
        if (f < 4) {
            int k = x / 53.6;
            if (f == 3 && x >= 380) { borra_letra(); return; }
            const char *s = filas_tecl[f];
            for (int i = 0; *s; i++) {
                int nb = (*s & 0x80) ? 2 : 1;
                if (i == k) { char c[5] = { 0 }; memcpy(c, s, nb); escribe(c); return; }
                s += nb;
            }
        } else {
            if (x < 127) teclado = 0;
            else if (x < 383) escribe(" ");
            else buscar();
        }
        return;
    }
    if (!teclado && y >= Y_REPR) {
        int by = Y_REPR + 90, cy = Y_REPR + 175, cx = W / 2;
        if (y > by - 25 && y < by + 30 && dur_s > 0) {
            double fr = (x - 24) / (double)(W - 48);
            if (fr < 0) fr = 0;
            if (fr > 1) fr = 1;
            char js[100];
            snprintf(js, sizeof js, "{\"command\":[\"seek\",%.1f,\"absolute\"]}", fr * dur_s);
            a_mpv(js);
        } else if (y > cy - 50 && y < cy + 50) {
            if (x < 80) cambia_volumen(-10);
            else if (x > W - 80) cambia_volumen(10);
            else if (x < cx - 50) anterior();
            else if (x > cx + 50) { if (actual >= 0) siguiente(); }
            else if (sonando) a_mpv("{\"command\":[\"cycle\",\"pause\"]}");
            else if (actual >= 0) reproduce(actual);
        }
    }
}

static void limita_scroll(void) {
    int n = ver_cola ? n_cola : n_lista;
    double max = n * FILA - (fondo_lista() - Y_LISTA);
    if (scroll > max) scroll = max;
    if (scroll < 0) scroll = 0;
}

static void termina(int s) { (void)s; if (pid_mpv) kill(pid_mpv, SIGTERM); if (pid_srv) kill(pid_srv, SIGTERM); _exit(0); }

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, termina);
    lanza_servidor();
    lanza_mpv(volumen);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) { fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
    TTF_Init();
    f_txt = TTF_OpenFont("/usr/share/fonts/opensans/OpenSans-Regular.ttf", 24);
    f_peq = TTF_OpenFont("/usr/share/fonts/opensans/OpenSans-Regular.ttf", 19);
    f_gran = TTF_OpenFont("/usr/share/fonts/opensans/OpenSans-Regular.ttf", 32);
    if (!f_txt || !f_peq || !f_gran) { fprintf(stderr, "faltan fuentes\n"); return 1; }
    SDL_Window *w = SDL_CreateWindow("YouTube Music", 0, 0, W, H, SDL_WINDOW_FULLSCREEN);
    if (!w) { fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
    r = SDL_CreateRenderer(w, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!r) r = SDL_CreateRenderer(w, -1, SDL_RENDERER_SOFTWARE);
    if (!r) return 1;
    SDL_RenderSetLogicalSize(r, W, H);
    SDL_ShowCursor(SDL_DISABLE);
    SDL_StartTextInput();

    // Pruebas: YTM_BUSCAR="texto" busca al arrancar, YTM_TOCAR=n reproduce el resultado n,
    // YTM_CAPTURA=fichero.bmp:segundos guarda la pantalla y sale.
    const char *auto_busca = getenv("YTM_BUSCAR"), *auto_toca = getenv("YTM_TOCAR"), *captura = getenv("YTM_CAPTURA");
    if (auto_busca) { snprintf(consulta, sizeof consulta, "%s", auto_busca); buscar(); }
    Uint32 t_captura = captura && strchr(captura, ':') ? SDL_GetTicks() + 1000 * atoi(strchr(captura, ':') + 1) : 0;

    int hecho = 0, toc_y = 0, toc_x = 0, arrastra = 0;
    double scroll0 = 0;
    Uint32 ult_dibujo = 0;
    while (!hecho) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            int y;
            switch (e.type) {
            case SDL_QUIT: hecho = 1; break;
            case SDL_KEYDOWN:
                if (e.key.keysym.sym == SDLK_VOLUMEDOWN || e.key.keysym.sym == SDLK_ESCAPE) hecho = 1;
                else if (e.key.keysym.sym == SDLK_BACKSPACE) { borra_letra(); sucio = 1; }
                else if (e.key.keysym.sym == SDLK_RETURN) buscar();
                break;
            case SDL_TEXTINPUT: escribe(e.text.text); teclado = 1; sucio = 1; break;
            case SDL_FINGERDOWN:
                toc_x = e.tfinger.x * W; toc_y = e.tfinger.y * H; arrastra = 0; scroll0 = scroll;
                break;
            case SDL_FINGERMOTION:
                y = e.tfinger.y * H;
                if (toc_y >= Y_LISTA && toc_y < fondo_lista() && (arrastra || abs(y - toc_y) > 14)) {
                    arrastra = 1; scroll = scroll0 - (y - toc_y); limita_scroll(); sucio = 1;
                }
                break;
            case SDL_FINGERUP:
                if (!arrastra) toque(toc_x, toc_y);
                break;
            case SDL_MOUSEBUTTONUP:
                if (e.button.which != SDL_TOUCH_MOUSEID) toque(e.button.x, e.button.y);
                break;
            case SDL_MOUSEWHEEL:
                scroll -= e.wheel.y * FILA; limita_scroll(); sucio = 1;
                break;
            }
        }
        conecta_mpv();
        lineas(srv_out, srv_buf, &srv_len, sizeof srv_buf, del_servidor);
        lineas(mpv_fd, mpv_buf, &mpv_len, sizeof mpv_buf, de_mpv);
        if (auto_toca && n_lista > atoi(auto_toca)) {
            int i = atoi(auto_toca);
            auto_toca = NULL;
            memcpy(cola, lista, sizeof(Pista) * n_lista);
            n_cola = n_lista;
            reproduce(i);
        }
        Uint32 ahora = SDL_GetTicks();
        // Redibuja al cambiar algo o cada medio segundo (cursor, contador, progreso).
        if (sucio || ahora - ult_dibujo > 500) {
            dibuja();
            if (t_captura && ahora >= t_captura) {
                SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
                SDL_RenderReadPixels(r, NULL, s->format->format, s->pixels, s->pitch);
                char fich[200];
                snprintf(fich, sizeof fich, "%.*s", (int)(strchr(captura, ':') - captura), captura);
                SDL_SaveBMP(s, fich);
                SDL_FreeSurface(s);
                hecho = 1;
            }
            SDL_RenderPresent(r);
            sucio = 0;
            ult_dibujo = ahora;
        }
        SDL_Delay(30);
    }
    termina(0);
    return 0;
}
