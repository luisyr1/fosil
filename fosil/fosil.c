// Fósil: interfaz propia para el Moto E (surnia). C + SDL2 + OpenGL ES 2.
// Dibuja directamente sobre la GPU y solo cuando hace falta.
#define _GNU_SOURCE
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <GLES2/gl2.h>
#include <security/pam_appl.h>
#include <crypt.h>
#include <linux/input.h>
#include <dirent.h>
#include <sys/wait.h>
#include <errno.h>
#include <pwd.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define EXIT_TERMINAL 42
#define STATE_DIR "/var/lib/fosil"
#define CTL_DIR "/run/fosil"
#define CTL_FIFO CTL_DIR "/ctl"
#define BACK_FIFO CTL_DIR "/volver"

// ---------------------------------------------------------------- utilidades
typedef struct { float r, g, b, a; } C;
static C rgba(float r, float g, float b, float a) { return (C){r, g, b, a}; }
static C alpha(C c, float a) { c.a *= a; return c; }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float ease_out(float t) { t = 1 - t; return 1 - t * t * t; }
static double now_s(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static void write_file(const char *path, const char *s) {
    int fd = open(path, O_WRONLY);
    if (fd >= 0) { ssize_t n = write(fd, s, strlen(s)); (void)n; close(fd); }
}
static bool read_file(const char *path, char *buf, size_t len) {
    FILE *f = fopen(path, "r");
    if (!f) { buf[0] = 0; return false; }
    size_t n = fread(buf, 1, len - 1, f); buf[n] = 0; fclose(f);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == ' ')) buf[--n] = 0;
    return true;
}

// ---------------------------------------------------------------- configuración
// Usuario de /etc/fosil/fosil.conf (o el de uid 1000) y perfil del teléfono instalado por instalar.sh.
// Sin perfil, valores genéricos: así Fósil arranca en teléfonos que aún no tienen uno.
#define PERFIL "/usr/local/share/fosil/dispositivo/perfil.conf"
static char usuario[64] = "", home_u[96] = "";
static char p_bat[160], p_ter[160], p_pwr[64], p_vol[64], p_desc[96];
static bool conf_valor(const char *path, const char *key, char *out, size_t n) {
    FILE *f = fopen(path, "r"); if (!f) return false;
    char l[512]; size_t k = strlen(key); bool ok = false;
    while (fgets(l, sizeof l, f))
        if (!strncmp(l, key, k) && l[k] == '=') { l[strcspn(l, "\r\n")] = 0; snprintf(out, n, "%s", l + k + 1); ok = *out != 0; break; }
    fclose(f); return ok;
}
static void perfil_o(const char *key, char *out, size_t n, const char *def) { if (!conf_valor(PERFIL, key, out, n)) snprintf(out, n, "%s", def); }
static void config_cargar(void) {
    struct passwd *pw = NULL;
    if (conf_valor("/etc/fosil/fosil.conf", "USUARIO", usuario, sizeof usuario)) pw = getpwnam(usuario);
    if (!pw) pw = getpwuid(1000);
    snprintf(usuario, sizeof usuario, "%s", pw ? pw->pw_name : "user");
    snprintf(home_u, sizeof home_u, "%s", pw ? pw->pw_dir : "/home/user");
    if (!conf_valor(PERFIL, "BATERIA", p_bat, sizeof p_bat)) {   // la primera fuente de tipo Battery
        snprintf(p_bat, sizeof p_bat, "/sys/class/power_supply/battery");
        DIR *d = opendir("/sys/class/power_supply"); struct dirent *e; char t[32], q[320];
        while (d && (e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            snprintf(q, sizeof q, "/sys/class/power_supply/%.255s/type", e->d_name);
            if (read_file(q, t, sizeof t) && !strcmp(t, "Battery")) { snprintf(p_bat, sizeof p_bat, "/sys/class/power_supply/%.120s", e->d_name); break; }
        }
        if (d) closedir(d);
    }
    perfil_o("TERMICA", p_ter, sizeof p_ter, "/sys/class/thermal/thermal_zone0");
    perfil_o("TECLA_ENCENDIDO", p_pwr, sizeof p_pwr, "pm8941_pwrkey");
    perfil_o("TECLA_BAJAR", p_vol, sizeof p_vol, "pm8941_resin");
    perfil_o("DESCRIPCION", p_desc, sizeof p_desc, "Nura");
}

// ---------------------------------------------------------------- temas
typedef struct {
    const char *name;
    float bg[3], glow1[3], glow2[3];
    C accent, text, sub, card, line;
    bool metro;  // baldosas planas, fondo negro, Open Sans y transición de torniquete
} Theme;
static const Theme THEMES[] = {
    {"Noche", {.016f, .027f, .075f}, {.10f, .17f, .42f}, {.42f, .17f, .28f},
     {.98f, .78f, .62f, 1}, {.97f, .95f, .93f, 1}, {.68f, .71f, .80f, 1},
     {1, 1, 1, .07f}, {1, 1, 1, .11f}, false},
    {"Brasa", {.035f, .018f, .016f}, {.46f, .13f, .03f}, {.30f, .05f, .10f},
     {1.f, .62f, .25f, 1}, {.99f, .95f, .90f, 1}, {.78f, .67f, .60f, 1},
     {1, .9f, .8f, .07f}, {1, .85f, .7f, .12f}, false},
    {"Menta", {.012f, .040f, .040f}, {.04f, .34f, .29f}, {.05f, .16f, .36f},
     {.42f, .96f, .76f, 1}, {.93f, .99f, .97f, 1}, {.60f, .76f, .72f, 1},
     {.8f, 1, .95f, .07f}, {.7f, 1, .9f, .12f}, false},
    {"Metro", {0, 0, 0}, {0, 0, 0}, {0, 0, 0},
     {0, .47f, .84f, 1}, {1, 1, 1, 1}, {.65f, .65f, .65f, 1},
     {1, 1, 1, .12f}, {1, 1, 1, 0}, true},
};
#define NTHEMES 4
static int theme_idx = 0;
#define T (THEMES[theme_idx])
#define ON_ACC (T.metro ? rgba(1, 1, 1, 1) : rgba(0, 0, 0, .9f))

// ---------------------------------------------------------------- GPU
static int W = 540, H = 960;
static GLuint prog_rect, prog_tex, prog_bg, prog_blit, vbo, bg_tex, bg_fbo;
static GLint bl_res;
#define BG_W 135
#define BG_H 240
static GLint r_res, r_rect, r_rad, r_c1, r_c2, r_bw, t_res, t_col, b_res, b_t, b_bg, b_g1, b_g2;
static float OX = 0, OY = 0, GA = 1, TURN = 0, FLX = 0, FLY = 0, FLA = 0;
static GLint r_turn, t_turn, r_fl, t_fl;

static const char *VS =
    "attribute vec2 a;attribute vec2 u;uniform mediump vec2 res;uniform mediump float k;uniform mediump vec3 fl;varying vec2 p;varying vec2 v;"
    "void main(){p=a;v=u;float h=a.x/res.x*2.;float w=1.+h*sin(k)*.25;"
    "float d=a.y-fl.x;float e=sin(fl.y)*d/res.y*3.;"  // fl: centro y, ángulo, centro x de la baldosa
    "vec2 c=vec2(fl.z/res.x*2.-1.,1.-fl.x/res.y*2.);"
    "gl_Position=vec4(h*cos(k)-w+c.x*e,1.-(fl.x+d*cos(fl.y))/res.y*2.+c.y*e,0.,w+e);}";
static const char *FS_RECT =
    "precision mediump float;varying vec2 p;uniform vec4 rect;uniform float rad;uniform float bw;"
    "uniform vec4 c1;uniform vec4 c2;"
    "void main(){vec2 d=abs(p-rect.xy)-rect.zw+vec2(rad);"
    "float s=length(max(d,0.))+min(max(d.x,d.y),0.)-rad;"
    "float a=1.-smoothstep(-.6,.6,s);"
    "if(bw>0.)a*=smoothstep(-.6,.6,s+bw);"
    "float g=clamp((p.y-rect.y+rect.w)/(2.*rect.w),0.,1.);"
    "vec4 c=mix(c1,c2,g);gl_FragColor=vec4(c.rgb,c.a*a);}";
static const char *FS_TEX =
    "precision mediump float;varying vec2 v;uniform sampler2D tex;uniform vec4 col;"
    "void main(){gl_FragColor=vec4(col.rgb,col.a*texture2D(tex,v).a);}";
static const char *FS_BG =
    "precision mediump float;varying vec2 p;uniform vec2 res;uniform float t;"
    "uniform vec3 bg;uniform vec3 g1;uniform vec3 g2;"
    "void main(){vec2 u=p/res;float ar=res.y/res.x;vec2 q=vec2(u.x,u.y*ar);"
    "vec2 a=vec2(.25+.22*sin(t*.11),.35*ar+.16*sin(t*.083+1.));"
    "vec2 b=vec2(.78+.20*sin(t*.097+2.),.72*ar+.22*cos(t*.071));"
    "float w=.03*sin(q.y*5.+t*.15)+.02*sin(q.x*7.-t*.12);"
    "float d1=length(q-a)+w;float d2=length(q-b)-w;"
    "float f1=exp(-d1*d1*5.);float f2=exp(-d2*d2*4.);"
    "vec3 c=bg+g1*f1+g2*f2;"
    "gl_FragColor=vec4(c,1.);}";

static const char *FS_BLIT =
    "precision mediump float;varying vec2 v;uniform sampler2D tex;"
    "void main(){float d=mod(floor(gl_FragCoord.x)+floor(gl_FragCoord.y)*2.,4.)/(4.*255.);"
    "gl_FragColor=vec4(texture2D(tex,vec2(v.x,1.-v.y)).rgb+d,1.);}";

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL); glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char l[1024]; glGetShaderInfoLog(s, sizeof l, NULL, l); fprintf(stderr, "shader: %s\n", l); exit(1); }
    return s;
}
static GLuint program(const char *fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, compile(GL_VERTEX_SHADER, VS));
    glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
    glBindAttribLocation(p, 0, "a"); glBindAttribLocation(p, 1, "u");
    glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char l[1024]; glGetProgramInfoLog(p, sizeof l, NULL, l); fprintf(stderr, "link: %s\n", l); exit(1); }
    return p;
}
static void gpu_init(void) {
    prog_rect = program(FS_RECT); prog_tex = program(FS_TEX); prog_bg = program(FS_BG); prog_blit = program(FS_BLIT);
    bl_res = glGetUniformLocation(prog_blit, "res");
    r_turn = glGetUniformLocation(prog_rect, "k"); t_turn = glGetUniformLocation(prog_tex, "k");
    r_fl = glGetUniformLocation(prog_rect, "fl"); t_fl = glGetUniformLocation(prog_tex, "fl");
    r_res = glGetUniformLocation(prog_rect, "res"); r_rect = glGetUniformLocation(prog_rect, "rect");
    r_rad = glGetUniformLocation(prog_rect, "rad"); r_c1 = glGetUniformLocation(prog_rect, "c1");
    r_c2 = glGetUniformLocation(prog_rect, "c2"); r_bw = glGetUniformLocation(prog_rect, "bw");
    t_res = glGetUniformLocation(prog_tex, "res"); t_col = glGetUniformLocation(prog_tex, "col");
    b_res = glGetUniformLocation(prog_bg, "res"); b_t = glGetUniformLocation(prog_bg, "t");
    b_bg = glGetUniformLocation(prog_bg, "bg"); b_g1 = glGetUniformLocation(prog_bg, "g1");
    b_g2 = glGetUniformLocation(prog_bg, "g2");
    glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void *)8);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glGenTextures(1, &bg_tex); glBindTexture(GL_TEXTURE_2D, bg_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, BG_W, BG_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &bg_fbo); glBindFramebuffer(GL_FRAMEBUFFER, bg_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bg_tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) fprintf(stderr, "FBO incompleto\n");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
static void quad(float x, float y, float w, float h) {
    float v[] = {x, y, 0, 0, x + w, y, 1, 0, x, y + h, 0, 1, x + w, y + h, 1, 1};
    glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
static void rect_ex(float x, float y, float w, float h, float rad, C a, C b, float bw) {
    x += OX; y += OY;
    if (T.metro) {  // todo plano y en ángulo recto; solo se libran los círculos
        if (fabsf(w - h) > .5f || rad < w / 2 - .5f) rad = 0;
        b = a;
    }
    glUseProgram(prog_rect);
    glUniform2f(r_res, W, H); glUniform1f(r_turn, TURN); glUniform3f(r_fl, FLY, FLA, FLX);
    glUniform4f(r_rect, x + w / 2, y + h / 2, w / 2, h / 2);
    glUniform1f(r_rad, rad); glUniform1f(r_bw, bw);
    glUniform4f(r_c1, a.r, a.g, a.b, a.a * GA);
    glUniform4f(r_c2, b.r, b.g, b.b, b.a * GA);
    quad(x - 1, y - 1, w + 2, h + 2);
}
static void rr(float x, float y, float w, float h, float rad, C c) { rect_ex(x, y, w, h, rad, c, c, 0); }
static void rgrad(float x, float y, float w, float h, float rad, C a, C b) { rect_ex(x, y, w, h, rad, a, b, 0); }
static void ring(float x, float y, float w, float h, float rad, float bw, C c) { rect_ex(x, y, w, h, rad, c, c, bw); }
static void card(float x, float y, float w, float h, float rad) {
    if (T.metro) { rr(x, y, w, h, 0, T.card); return; }
    C top = T.card; top.a *= 1.5f;
    rgrad(x, y, w, h, rad, top, T.card);
    ring(x, y, w, h, rad, 1.2f, T.line);
}
static void background(float t) {
    if (T.metro) { glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT); return; }
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, bg_fbo); glViewport(0, 0, BG_W, BG_H);
    glUseProgram(prog_bg);
    glUniform2f(b_res, BG_W, BG_H); glUniform1f(b_t, t);
    glUniform3fv(b_bg, 1, T.bg); glUniform3fv(b_g1, 1, T.glow1); glUniform3fv(b_g2, 1, T.glow2);
    quad(0, 0, BG_W, BG_H);
    glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, W, H);
    glUseProgram(prog_blit); glUniform2f(bl_res, W, H);
    glBindTexture(GL_TEXTURE_2D, bg_tex);
    quad(0, 0, W, H);
    glEnable(GL_BLEND);
}

// ---------------------------------------------------------------- texto
enum { F_SANS, F_MONO, F_METRO, F_METRO_LIGHT };
#define MAX_FONTS 64
#define MAX_TEX 256
static struct { int kind, size; TTF_Font *f; } fonts[MAX_FONTS];
static int nfonts;
typedef struct { unsigned hash; int kind, size, w, h; float dy; GLuint tex; double used; char *s; } TexEnt;
static TexEnt texs[MAX_TEX];

// En Metro, F_SANS es Open Sans: ligera en los tamaños grandes, como los títulos del móvil.
static int font_kind(int kind, int size) { return kind == F_SANS && T.metro ? (size >= 36 ? F_METRO_LIGHT : F_METRO) : kind; }
static TTF_Font *font_raw(int kind, int size) {
    for (int i = 0; i < nfonts; i++) if (fonts[i].kind == kind && fonts[i].size == size) return fonts[i].f;
    const char *path = kind == F_MONO ? "/usr/share/fonts/jetbrains-mono/JetBrainsMono-Regular.ttf"
                     : kind == F_METRO ? "/usr/share/fonts/opensans/OpenSans-Regular.ttf"
                     : kind == F_METRO_LIGHT ? "/usr/share/fonts/opensans/OpenSans-Light.ttf"
                                             : "/usr/share/fonts/inter/InterVariable.ttf";
    TTF_Font *f = TTF_OpenFont(path, size);
    if (!f) { fprintf(stderr, "fuente: %s\n", TTF_GetError()); exit(1); }
    if (nfonts < MAX_FONTS) { fonts[nfonts].kind = kind; fonts[nfonts].size = size; fonts[nfonts++].f = f; }
    return f;
}
static TTF_Font *font(int kind, int size) { return font_raw(font_kind(kind, size), size); }
// Open Sans no trae todos los símbolos de la interfaz (⌫, ↻, ⇧): esos textos siguen en Inter.
static bool font_has(TTF_Font *f, const char *s) {
    for (const unsigned char *p = (const unsigned char *)s; *p;) {
        unsigned c = *p; int n = c < 0x80 ? 0 : c < 0xE0 ? 1 : c < 0xF0 ? 2 : 3;
        if (n) c &= 0x3F >> n;
        p++;
        for (int i = 0; i < n && *p; i++) c = c << 6 | (*p++ & 0x3F);
        if (c > 0xFFFF || !TTF_GlyphIsProvided(f, c)) return false;
    }
    return true;
}
static TexEnt *text_tex(int kind, int size, const char *s) {
    kind = font_kind(kind, size);
    unsigned h = 5381 + kind * 33 + size * 977;
    for (const char *p = s; *p; p++) h = h * 33 + (unsigned char)*p;
    TexEnt *oldest = &texs[0];
    for (int i = 0; i < MAX_TEX; i++) {
        TexEnt *e = &texs[i];
        if (e->tex && e->hash == h && e->kind == kind && e->size == size && !strcmp(e->s, s)) { e->used = now_s(); return e; }
        if (!e->tex) { oldest = e; oldest->used = -1; }
        else if (e->used < oldest->used) oldest = e;
    }
    SDL_Color white = {255, 255, 255, 255};
    TTF_Font *tf = font_raw(kind, size); float dy = 0;
    if (kind >= F_METRO) { if (font_has(tf, s)) dy = size * .1f; else tf = font_raw(F_SANS, size); }
    SDL_Surface *a = TTF_RenderUTF8_Blended(tf, s, white);
    if (!a) return NULL;
    SDL_Surface *b = SDL_ConvertSurfaceFormat(a, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(a);
    if (!b) return NULL;
    if (oldest->tex) { glDeleteTextures(1, &oldest->tex); free(oldest->s); }
    glGenTextures(1, &oldest->tex); glBindTexture(GL_TEXTURE_2D, oldest->tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (b->pitch != b->w * 4) {  // normaliza filas
        unsigned char *tmp = malloc(b->w * b->h * 4);
        for (int y = 0; y < b->h; y++) memcpy(tmp + y * b->w * 4, (unsigned char *)b->pixels + y * b->pitch, b->w * 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, b->w, b->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, tmp); free(tmp);
    } else glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, b->w, b->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, b->pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    oldest->hash = h; oldest->kind = kind; oldest->size = size; oldest->w = b->w; oldest->h = b->h; oldest->dy = dy;
    oldest->used = now_s(); oldest->s = strdup(s);
    SDL_FreeSurface(b);
    return oldest;
}
// align: 0 izquierda, 1 centro, 2 derecha. Devuelve el ancho.
static float text(int kind, int size, const char *s, float x, float y, C c, int align) {
    if (!s || !*s) return 0;
    TexEnt *e = text_tex(kind, size, s);
    if (!e) return 0;
    float dx = align == 1 ? -e->w / 2.f : align == 2 ? -(float)e->w : 0;
    glUseProgram(prog_tex);
    glUniform2f(t_res, W, H); glUniform1f(t_turn, TURN); glUniform3f(t_fl, FLY, FLA, FLX); glUniform4f(t_col, c.r, c.g, c.b, c.a * GA);
    glBindTexture(GL_TEXTURE_2D, e->tex);
    quad(x + dx + OX, y - e->dy + OY, e->w, e->h);  // dy: Open Sans deja más aire arriba que Inter
    return e->w;
}
// Cachea el recorte: medir fuentes por cada carácter y fotograma bloquea el táctil.
static void text_fit(int kind, int size, const char *s, float x, float y, float maxw, C c) {
    static struct { int kind, size; float width; bool valid; char input[320], output[324]; } cache[128];
    static unsigned next;
    kind = font_kind(kind, size);
    char input[320]; snprintf(input, sizeof input, "%s", s);
    // snprintf puede cortar a mitad de un carácter UTF-8.
    size_t len = strlen(input);
    if (len == sizeof input - 1) {
        size_t start = len - 1;
        while (start && ((unsigned char)input[start] & 0xC0) == 0x80) start--;
        unsigned char lead = input[start];
        size_t need = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        if (len - start < need) input[start] = 0;
    }
    for (int i = 0; i < 128; i++) {
        if (cache[i].valid && cache[i].kind == kind && cache[i].size == size &&
            cache[i].width == maxw && !strcmp(cache[i].input, input)) {
            text(kind, size, cache[i].output, x, y, c, 0); return;
        }
    }
    unsigned slot = next++ % 128;
    cache[slot].valid = true; cache[slot].kind = kind; cache[slot].size = size; cache[slot].width = maxw;
    strcpy(cache[slot].input, input); strcpy(cache[slot].output, input);
    int width = 0; TTF_Font *f = font(kind, size);
    TTF_SizeUTF8(f, input, &width, NULL);
    if (width > maxw) {
        size_t offsets[321], count = 0;
        for (size_t i = 0; input[i]; i++)
            if (((unsigned char)input[i] & 0xC0) != 0x80) offsets[count++] = i;
        size_t lo = 0, hi = count;
        while (lo < hi) {
            size_t mid = (lo + hi + 1) / 2;
            size_t cut = mid < count ? offsets[mid] : strlen(input);
            memcpy(cache[slot].output, input, cut); strcpy(cache[slot].output + cut, "…");
            TTF_SizeUTF8(f, cache[slot].output, &width, NULL);
            if (width <= maxw) lo = mid; else hi = mid - 1;
        }
        size_t cut = lo < count ? offsets[lo] : strlen(input);
        memcpy(cache[slot].output, input, cut); strcpy(cache[slot].output + cut, "…");
    }
    text(kind, size, cache[slot].output, x, y, c, 0);
}
// Dos líneas como máximo, cortando por palabras; la segunda acaba en … si no cabe.
static void text_wrap2(int kind, int size, const char *s, float x, float y, float lh, float maxw, C c) {
    char a[320]; snprintf(a, sizeof a, "%s", s);
    int w = 0; TTF_SizeUTF8(font(kind, size), a, &w, NULL);
    if (w <= maxw) { text(kind, size, a, x, y, c, 0); return; }
    size_t cut = 0;
    for (size_t i = 1; a[i]; i++) {
        if (a[i] != ' ') continue;
        char t = a[i]; a[i] = 0; TTF_SizeUTF8(font(kind, size), a, &w, NULL); a[i] = t;
        if (w > maxw) break;
        cut = i;
    }
    if (!cut) { text_fit(kind, size, a, x, y, maxw, c); return; }
    a[cut] = 0;
    text(kind, size, a, x, y, c, 0);
    text_fit(kind, size, a + cut + 1, x, y + lh, maxw, c);
}

// ---------------------------------------------------------------- estado del sistema
#define HIST 56
#define MAX_NETS 24
#define MAX_FEED 40
typedef struct { char ssid[64]; int signal; bool secure, active, saved; } Net;
static struct {
    pthread_mutex_t m;
    int batt, mem_used, mem_total, temp, uptime, disk_used, disk_total, batt_ma, batt_min; char tiempo[24]; char ia[2][120];
    float cpu, hist[HIST];
    bool wifi; char ssid[64]; int signal; char ip[48]; char ts_ip[48]; bool ts_login;
    char kernel[64];
    Net nets[MAX_NETS]; int nnets; bool scanning;
    char feed[MAX_FEED][240]; int nfeed;
    char chat_titulo[64], chat_hora[16]; int nfotos, napps;
    int version;
    // actualizaciones: ficheros de /var/lib/fosil (version, actualizar/disponible, estado, notas)
    char upd_inst[24], upd_disp[24], upd_paso[16], upd_texto[120], upd_err[120], upd_notas[600];
    int upd_linux; long upd_comprobado, upd_estado_t; bool upd_lock, upd_reiniciar;
} st = {.m = PTHREAD_MUTEX_INITIALIZER, .batt = -1};
static Uint32 EV_WAKE;
static volatile bool blanked = false; static volatile bool dirty_req = false; static volatile int demo = 0;
static int wake_pipe[2] = {-1, -1};
static void wake(void) {
    SDL_Event e; SDL_zero(e); e.type = EV_WAKE; SDL_PushEvent(&e);
    if (wake_pipe[1] >= 0) { ssize_t n = write(wake_pipe[1], "x", 1); (void)n; }
}

static int run_line(const char *cmd, char *out, size_t len) {
    FILE *p = popen(cmd, "r");
    if (!p) { if (out) out[0] = 0; return -1; }
    if (out) { if (!fgets(out, len, p)) out[0] = 0; else out[strcspn(out, "\n")] = 0; }
    char tmp[256]; while (fgets(tmp, sizeof tmp, p)) {}
    return pclose(p);
}
static void read_feed(void) {
    FILE *f = fopen(STATE_DIR "/feed.log", "r");
    char lines[MAX_FEED][240]; int n = 0;
    if (f) {
        char b[240];
        while (fgets(b, sizeof b, f)) {
            b[strcspn(b, "\n")] = 0;
            if (!*b) continue;
            memcpy(lines[n % MAX_FEED], b, sizeof b); n++;
        }
        fclose(f);
    }
    int cnt = n < MAX_FEED ? n : MAX_FEED;
    pthread_mutex_lock(&st.m);
    for (int i = 0; i < cnt; i++) memcpy(st.feed[i], lines[(n - 1 - i) % MAX_FEED], 240);  // recientes primero
    st.nfeed = cnt;
    pthread_mutex_unlock(&st.m);
}
#define RUN_DIR "/run/fosil"
static void leer_actualizacion(void) {
    char inst[24], b[200], paso[16] = "", texto[120] = "", disp[24] = "", err[120] = "", notas[600] = ""; int lin = -1; long comp = 0, et = 0;
    read_file(STATE_DIR "/version", inst, sizeof inst);
    FILE *f = fopen(STATE_DIR "/actualizar/disponible", "r");
    if (f) {
        while (fgets(b, sizeof b, f)) {
            b[strcspn(b, "\n")] = 0;
            if (!strncmp(b, "VERSION=", 8)) snprintf(disp, sizeof disp, "%s", b + 8);
            else if (!strncmp(b, "LINUX=", 6)) lin = *(b + 6) ? atoi(b + 6) : -1;
            else if (!strncmp(b, "COMPROBADO=", 11)) comp = atol(b + 11);
            else if (!strncmp(b, "ERROR=", 6)) snprintf(err, sizeof err, "%s", b + 6);
        }
        fclose(f);
    }
    struct stat sb;
    if (read_file(STATE_DIR "/actualizar/estado", b, sizeof b) && !stat(STATE_DIR "/actualizar/estado", &sb)) {
        char *bar = strchr(b, '|');
        if (bar) { *bar = 0; snprintf(paso, sizeof paso, "%s", b); snprintf(texto, sizeof texto, "%s", bar + 1); et = sb.st_mtime; }
    }
    if (*disp) read_file(STATE_DIR "/actualizar/notas", notas, sizeof notas);
    bool lock = !access(RUN_DIR "/actualizar.lock", F_OK), rein = !access(STATE_DIR "/actualizar/reiniciar", F_OK);
    pthread_mutex_lock(&st.m);
    snprintf(st.upd_inst, sizeof st.upd_inst, "%s", *inst ? inst : "0.0.0");
    snprintf(st.upd_disp, sizeof st.upd_disp, "%s", disp); snprintf(st.upd_err, sizeof st.upd_err, "%s", err);
    snprintf(st.upd_paso, sizeof st.upd_paso, "%s", paso); snprintf(st.upd_texto, sizeof st.upd_texto, "%s", texto);
    memcpy(st.upd_notas, notas, sizeof notas);
    st.upd_linux = lin; st.upd_comprobado = comp; st.upd_estado_t = et; st.upd_lock = lock; st.upd_reiniciar = rein;
    pthread_mutex_unlock(&st.m);
}
static void *stats_thread(void *arg) {
    (void)arg;
    unsigned long long pidle = 0, ptotal = 0;
    int tick = 0;
    { char k[64]; read_file("/proc/sys/kernel/osrelease", k, sizeof k); pthread_mutex_lock(&st.m); snprintf(st.kernel, sizeof st.kernel, "%s", k); pthread_mutex_unlock(&st.m); }
    for (;;) {
        if (blanked) { sleep(5); continue; }
        char b[256];
        read_feed();  // los avisos aparecen al momento
        leer_actualizacion();  // cada vuelta (3 s): versión, disponible y estado
        { FILE *fi = fopen(STATE_DIR "/consumo", "r");
          if (fi) { char ia[2][120] = {"", ""};
              for (int i = 0; i < 2 && fgets(ia[i], sizeof ia[i], fi); i++) ia[i][strcspn(ia[i], "\n")] = 0;
              fclose(fi); pthread_mutex_lock(&st.m); memcpy(st.ia, ia, sizeof ia); pthread_mutex_unlock(&st.m); } }
        FILE *f = fopen("/proc/stat", "r");
        float cpu = 0;
        if (f) {
            unsigned long long u, n, s, i, w, q, si, sl;
            if (fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &u, &n, &s, &i, &w, &q, &si, &sl) == 8) {
                unsigned long long idle = i + w, total = u + n + s + i + w + q + si + sl;
                if (total > ptotal) cpu = 1.f - (float)(idle - pidle) / (float)(total - ptotal);
                pidle = idle; ptotal = total;
            }
            fclose(f);
        }
        long mt = 0, ma = 0;
        if ((f = fopen("/proc/meminfo", "r"))) {
            while (fgets(b, sizeof b, f)) { sscanf(b, "MemTotal: %ld", &mt); sscanf(b, "MemAvailable: %ld", &ma); }
            fclose(f);
        }
        char pth[200];
        snprintf(pth, sizeof pth, "%s/capacity", p_bat); read_file(pth, b, sizeof b);
        int batt = *b ? atoi(b) : -1;
        snprintf(pth, sizeof pth, "%s/current_avg", p_bat); read_file(pth, b, sizeof b);
        int batt_ma = atoi(b) / 1000;
        snprintf(pth, sizeof pth, "%s/charge_now", p_bat); read_file(pth, b, sizeof b);
        int charge_mah = atoi(b) / 1000;
        int batt_min = batt_ma < -15 ? charge_mah * 60 / -batt_ma : -1;
        { static bool avisada = false;   // aviso una sola vez por descarga
          if (batt >= 0 && batt <= 15 && batt_ma < 0 && !avisada) { avisada = true; int rc = system("/usr/local/bin/fosil-avisa 'Batería baja: conéctalo a un cargador' sistema"); (void)rc; }
          if (batt_ma > 0) avisada = false; }
        if (tick % 600 == 0) {   // el tiempo, cada media hora
            char w[32] = ""; FILE *pw = popen("curl -fsS -m 8 'https://wttr.in/?format=%t' 2>/dev/null", "r");
            if (pw) { if (!fgets(w, sizeof w, pw)) w[0] = 0; pclose(pw); }
            if (strstr(w, "°C")) { char *q = w[0] == '+' ? w + 1 : w; q[strcspn(q, "\n")] = 0; pthread_mutex_lock(&st.m); snprintf(st.tiempo, sizeof st.tiempo, "%s", q); pthread_mutex_unlock(&st.m); }
        }
        snprintf(pth, sizeof pth, "%s/temp", p_ter); read_file(pth, b, sizeof b);
        int temp = atoi(b) / 1000;
        read_file("/proc/uptime", b, sizeof b);
        int up = atoi(b);
        pthread_mutex_lock(&st.m);
        st.cpu = cpu; memmove(st.hist, st.hist + 1, sizeof(float) * (HIST - 1)); st.hist[HIST - 1] = cpu;
        st.mem_total = mt / 1024; st.mem_used = (mt - ma) / 1024; st.batt = batt; st.batt_ma = batt_ma; st.batt_min = batt_min; st.temp = temp; st.uptime = up;
        st.version++;
        pthread_mutex_unlock(&st.m);
        static bool vivas = false;  // al pasar a Metro, los datos se piden al momento
        bool vivas_ya = T.metro && !vivas; vivas = T.metro;
        if (T.metro && (vivas_ya || tick % 4 == 0)) {  // baldosas vivas: la conversación más reciente es la primera línea
            char l[200] = "", *a = NULL, *t = NULL; FILE *fl = fopen(STATE_DIR "/chat/lista", "r");
            if (fl) { if (fgets(l, sizeof l, fl)) { l[strcspn(l, "\n")] = 0; a = strchr(l, '|'); if (a) { *a++ = 0; t = strchr(a, '|'); if (t) *t++ = 0; } } fclose(fl); }
            pthread_mutex_lock(&st.m);
            snprintf(st.chat_titulo, sizeof st.chat_titulo, "%s", t ? t : ""); snprintf(st.chat_hora, sizeof st.chat_hora, "%s", t ? a : "");
            pthread_mutex_unlock(&st.m);
        }
        if (T.metro && (vivas_ya || tick % 20 == 0)) {
            char n[16] = ""; run_line("/usr/local/bin/fosil-foto listar 99 2>/dev/null | wc -l", n, sizeof n);
            int apps = 0; char ad[120]; snprintf(ad, sizeof ad, "%s/apps", home_u); DIR *d = opendir(ad);
            if (d) { struct dirent *e; char p[320];
                while ((e = readdir(d))) { if (e->d_name[0] == '.') continue; snprintf(p, sizeof p, "%s/%.180s/app.conf", ad, e->d_name); if (!access(p, R_OK)) apps++; }
                closedir(d); }
            pthread_mutex_lock(&st.m); st.nfotos = atoi(n); st.napps = apps; pthread_mutex_unlock(&st.m);
        }
        if (tick % 4 == 0) {
            char line[256] = "";
            bool wifi = false; char ssid[64] = ""; int sig = 0; char ip[48] = "", ts[48] = ""; bool login = false;
            FILE *p = popen("LC_ALL=C nmcli -t -f ACTIVE,SIGNAL,SSID dev wifi list --rescan no 2>/dev/null", "r");
            if (p) {
                while (fgets(line, sizeof line, p)) {
                    line[strcspn(line, "\n")] = 0;
                    if (!strncmp(line, "yes:", 4)) { sig = atoi(line + 4); char *c = strchr(line + 4, ':'); if (c) snprintf(ssid, sizeof ssid, "%s", c + 1); wifi = true; }
                }
                pclose(p);
            }
            run_line("ip -4 -o addr show wlan0 2>/dev/null | awk '{print $4}' | cut -d/ -f1", ip, sizeof ip);
            run_line("/usr/sbin/tailscale ip -4 2>/dev/null", ts, sizeof ts);
            if (!*ts) { char s[64] = ""; run_line("LC_ALL=C /usr/sbin/tailscale status --peers=false 2>&1 | head -1", s, sizeof s); login = strstr(s, "Logged out") != NULL; }
            pthread_mutex_lock(&st.m);
            st.wifi = wifi; snprintf(st.ssid, sizeof st.ssid, "%s", ssid); st.signal = sig;
            snprintf(st.ip, sizeof st.ip, "%s", ip); snprintf(st.ts_ip, sizeof st.ts_ip, "%s", ts); st.ts_login = login;
            pthread_mutex_unlock(&st.m);
        }
        if (tick % 20 == 0) {
            char s[64] = "";
            run_line("df -m / | awk 'NR==2{print $3,$2}'", s, sizeof s);
            int a = 0, t = 0; sscanf(s, "%d %d", &a, &t);
            pthread_mutex_lock(&st.m); st.disk_used = a; st.disk_total = t; pthread_mutex_unlock(&st.m);
        }
        tick++;
        wake();
        sleep(3);
    }
    return NULL;
}
static void *scan_thread(void *arg) {
    (void)arg;
    Net n[MAX_NETS]; int cnt = 0;
    char saved[32][64]; int ns = 0; char line[256];
    FILE *p = popen("LC_ALL=C nmcli -t -f NAME,TYPE con show 2>/dev/null", "r");
    if (p) {
        while (fgets(line, sizeof line, p) && ns < 32) {
            char *t = strstr(line, ":802-11-wireless");
            if (t) { *t = 0; snprintf(saved[ns++], 64, "%s", line); }
        }
        pclose(p);
    }
    p = popen("LC_ALL=C nmcli -t -f IN-USE,SIGNAL,SECURITY,SSID dev wifi list --rescan yes 2>/dev/null", "r");
    if (p) {
        while (fgets(line, sizeof line, p) && cnt < MAX_NETS) {
            line[strcspn(line, "\n")] = 0;
            char *a = line, *b = strchr(a, ':'); if (!b) continue; *b++ = 0;
            char *c = strchr(b, ':'); if (!c) continue; *c++ = 0;
            char *d = strchr(c, ':'); if (!d) continue; *d++ = 0;
            if (!*d) continue;
            bool dup = false;
            for (int i = 0; i < cnt; i++) if (!strcmp(n[i].ssid, d)) { dup = true; if (*a == '*') n[i].active = true; break; }
            if (dup) continue;
            snprintf(n[cnt].ssid, sizeof n[cnt].ssid, "%s", d);
            n[cnt].signal = atoi(b); n[cnt].secure = *c != 0 && strcmp(c, "--"); n[cnt].active = *a == '*'; n[cnt].saved = false;
            for (int i = 0; i < ns; i++) if (!strcmp(saved[i], d)) n[cnt].saved = true;
            cnt++;
        }
        pclose(p);
    }
    pthread_mutex_lock(&st.m);
    memcpy(st.nets, n, sizeof(Net) * cnt); st.nnets = cnt; st.scanning = false;
    pthread_mutex_unlock(&st.m);
    wake();
    return NULL;
}
static void start_scan(void) {
    pthread_mutex_lock(&st.m);
    bool busy = st.scanning; st.scanning = true;
    pthread_mutex_unlock(&st.m);
    if (busy) return;
    pthread_t th; pthread_create(&th, NULL, scan_thread, NULL); pthread_detach(th);
}

// ---------------------------------------------------------------- háptica y botones
static int vib_fd = -1, vib_id = -1;
static int find_input(const char *name) {
    DIR *d = opendir("/dev/input");
    if (!d) return -1;
    struct dirent *e; int fd = -1;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "event", 5)) continue;
        char path[300]; snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        int f = open(path, O_RDWR | O_NONBLOCK);
        if (f < 0) continue;
        char n[128] = ""; ioctl(f, EVIOCGNAME(sizeof n), n);
        if (!strcmp(n, name)) { fd = f; break; }
        close(f);
    }
    closedir(d);
    return fd;
}
static void vib_init(void) {
    vib_fd = find_input("pm8xxx_vib_ffmemless");
    if (vib_fd < 0) return;
    struct ff_effect fx; memset(&fx, 0, sizeof fx);
    fx.type = FF_RUMBLE; fx.id = -1; fx.u.rumble.strong_magnitude = 0xC000;
    fx.replay.length = 14;
    if (ioctl(vib_fd, EVIOCSFF, &fx) == 0) vib_id = fx.id;
}
static void vibrate(void) {
    if (vib_fd < 0 || vib_id < 0) return;
    struct input_event ev; memset(&ev, 0, sizeof ev);
    ev.type = EV_FF; ev.code = vib_id; ev.value = 1;
    ssize_t n = write(vib_fd, &ev, sizeof ev); (void)n;
}
static Uint32 EV_POWER;
static void *power_thread(void *arg) {
    (void)arg;
    int fd = find_input(p_pwr);
    if (fd < 0) return NULL;
    struct pollfd pf = {fd, POLLIN, 0};
    for (;;) {
        if (poll(&pf, 1, -1) <= 0) continue;
        struct input_event ev;
        while (read(fd, &ev, sizeof ev) == sizeof ev) {
            if (ev.type == EV_KEY && ev.code == KEY_POWER && ev.value == 1) {
                SDL_Event e; SDL_zero(e); e.type = EV_POWER; SDL_PushEvent(&e);
                if (wake_pipe[1] >= 0) { ssize_t n = write(wake_pipe[1], "x", 1); (void)n; }
            }
        }
    }
    return NULL;
}

// ---------------------------------------------------------------- brillo y ajustes
static char bl_path[300]; static int bl_max = 255; static float brightness = 1.f;
static void bl_init(void) {
    DIR *d = opendir("/sys/class/backlight");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        snprintf(bl_path, sizeof bl_path, "/sys/class/backlight/%s", e->d_name);
        char p[340], b[32]; snprintf(p, sizeof p, "%s/max_brightness", bl_path);
        if (read_file(p, b, sizeof b) && atoi(b) > 0) bl_max = atoi(b);
        break;
    }
    closedir(d);
}
static void bl_set(float v) {
    if (!*bl_path) return;
    char p[340], b[32];
    snprintf(p, sizeof p, "%s/brightness", bl_path);
    snprintf(b, sizeof b, "%d", (int)(v * bl_max + .5f));
    write_file(p, b);
}
static int drm_fd = -1; static uint32_t dpms_conn = 0, dpms_prop = 0;
static void panel_init(SDL_Window *win) {
    (void)win;
    // SDL ya tiene abierto el dispositivo y es su maestro: reutilizamos ese descriptor.
    DIR *d = opendir("/proc/self/fd");
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            char link[64], dst[128] = ""; snprintf(link, sizeof link, "/proc/self/fd/%s", e->d_name);
            ssize_t n = readlink(link, dst, sizeof dst - 1);
            if (n > 0) { dst[n] = 0; if (!strncmp(dst, "/dev/dri/card", 13)) { int f = atoi(e->d_name); if (drmIsMaster(f)) { drm_fd = f; break; } if (drm_fd < 0) drm_fd = f; } }
        }
        closedir(d);
    }
    if (drm_fd < 0) { fprintf(stderr, "panel: sin descriptor DRM\n"); return; }
    drmModeRes *r = drmModeGetResources(drm_fd);
    if (!r) { fprintf(stderr, "panel: sin recursos (fd %d)\n", drm_fd); return; }
    for (int i = 0; i < r->count_connectors && !dpms_prop; i++) {
        drmModeConnector *c = drmModeGetConnector(drm_fd, r->connectors[i]);
        if (!c) continue;
        if (c->connection == DRM_MODE_CONNECTED)
            for (int p = 0; p < c->count_props; p++) {
                drmModePropertyRes *pr = drmModeGetProperty(drm_fd, c->props[p]);
                if (pr) {
                    if (!strcmp(pr->name, "DPMS")) { dpms_conn = c->connector_id; dpms_prop = pr->prop_id; }
                    drmModeFreeProperty(pr);
                }
            }
        drmModeFreeConnector(c);
    }
    drmModeFreeResources(r);
    fprintf(stderr, "panel: fd %d maestro %d conector %u DPMS %u\n", drm_fd, drmIsMaster(drm_fd), dpms_conn, dpms_prop);
}
static void panel_set(bool on) {
    if (dpms_prop) {
        int rc = drmModeConnectorSetProperty(drm_fd, dpms_conn, dpms_prop, on ? DRM_MODE_DPMS_ON : DRM_MODE_DPMS_OFF);
        if (rc) fprintf(stderr, "DPMS: %s\n", strerror(-rc));
    }
    if (*bl_path) bl_set(on ? brightness : 0);
}
static int home_widgets[4] = {0, 1, 2, 3};
static int widget_edit = -1;
static int consumo_pos = 3;
static const char *widget_names[] = {"Batería", "Memoria", "Wi-Fi", "Tailscale", "CPU", "Temperatura", "Almacenamiento", "Tiempo encendido"};
static void config_save(void) {
    FILE *f = fopen(STATE_DIR "/config", "w");
    if (f) { fprintf(f, "%d %.2f\n%d %d %d %d %d\n", theme_idx, brightness, home_widgets[0], home_widgets[1], home_widgets[2], home_widgets[3], consumo_pos); fclose(f); }
}
static void config_load(void) {
    FILE *f = fopen(STATE_DIR "/config", "r");
    if (f) {
        int t; float b;
        if (fscanf(f, "%d %f", &t, &b) == 2) { if (t >= 0 && t < NTHEMES) theme_idx = t; brightness = clampf(b, .15f, 1); }
        for (int i = 0, v; i < 4 && fscanf(f, "%d", &v) == 1; i++)
            if (v >= 0 && v < 8) home_widgets[i] = v;
        int pos;
        if (fscanf(f, "%d", &pos) == 1 && pos >= 0 && pos < 4) consumo_pos = pos;
        fclose(f);
    }
}

// ---------------------------------------------------------------- entrada inmediata
typedef enum { S_LOCK, S_HOME, S_SYSTEM, S_NET, S_AGENTS, S_SETTINGS, S_POWER, S_CHAT, S_PIN, S_CONVS, S_TAREAS, S_FILES, S_VIEW, S_APPS, S_UPDATE, S_NCOUNT } Screen;
static struct {
    float x, y, sx, sy; bool down, press, release, dragged, enabled;
    unsigned active;
    double down_at;
} ui;
static Screen screen = S_LOCK, prev_screen = S_LOCK;
static double trans_start = -10; static int trans_dir = 1; 
#define TRANS (T.metro ? .34 : .26)
static double last_touch; static double shake_start = -10;
static float scroll_y[S_NCOUNT], scroll_v = 0; static bool scrolling = false;
static float scroll_max = 0;
static char toast_msg[160]; static double toast_t = -10;
static void toast(const char *fmt, ...) {
    va_list a; va_start(a, fmt); vsnprintf(toast_msg, sizeof toast_msg, fmt, a); va_end(a);
    toast_t = now_s();
}
static bool kb_open; static void chat_entered(void); static void tareas_entered(void); static void convs_load(void); static void apps_load(void); static void files_load(void);
static void go(Screen s, int dir) {
    if (s == screen) return;
    widget_edit = -1;
    prev_screen = screen; screen = s; trans_start = now_s(); trans_dir = dir;
    scroll_v = 0; scrolling = false; ui.active = 0;
    if (s == S_NET) start_scan();
    if (prev_screen == S_CHAT || prev_screen == S_PIN) kb_open = false;
    if (s == S_CHAT) chat_entered();
    if (s == S_TAREAS) tareas_entered();
    if (s == S_CONVS) convs_load();
    if (s == S_APPS) apps_load();
    if (s == S_FILES) files_load();
}
#define ID() ((unsigned)__LINE__)
static bool inside(float x, float y, float w, float h) {
    float px = ui.x - OX, py = ui.y - OY;
    return px >= x && px <= x + w && py >= y && py <= y + h;
}
static bool kb_touch;  // el gesto empezó sobre el teclado
// Tecla: vale la que hay bajo el dedo al soltar, aunque se haya movido. Sin huecos entre teclas.
static bool kb_btn(unsigned id, float x, float y, float w, float h);
static bool btn(unsigned id, float x, float y, float w, float h) {
    if (!ui.enabled) return false;
    if (ui.press && inside(x, y, w, h)) ui.active = id;
    if (ui.release && ui.active == id && inside(x, y, w, h) && !ui.dragged) { vibrate(); return true; }
    return false;
}
static bool kb_btn(unsigned id, float x, float y, float w, float h) {
    if (!ui.enabled || !kb_touch || !inside(x, y, w, h)) return false;
    if (ui.down) ui.active = id;
    if (ui.release) { vibrate(); return true; }
    return false;
}
static bool kb_on(unsigned id) { return ui.active == id && ui.down; }
static float pressed(unsigned id) { return ui.active == id && ui.down && !ui.dragged ? 1.f : 0.f; }

// ---------------------------------------------------------------- teclado en pantalla
static char *kb_target; static size_t kb_cap; static int kb_layer; static bool kb_shift;
static double kb_open_t = -10; static bool kb_open = false;
static const char *KB[3][3] = {
    {"q w e r t y u i o p", "a s d f g h j k l ñ", "z x c v b n m"},
    {"1 2 3 4 5 6 7 8 9 0", "- / : ; ( ) € & @ \"", ". , ? ! '"},
    {"[ ] { } # % ^ * + =", "_ \\ | ~ < > $ £ ¥ •", ". , ? ! '"},
};
static void kb_append(const char *s) {
    size_t n = strlen(kb_target);
    if (n + strlen(s) + 1 < kb_cap) strcat(kb_target, s);
}
static void kb_back(void) {
    size_t n = strlen(kb_target);
    if (!n) return;
    n--; while (n > 0 && ((unsigned char)kb_target[n] & 0xC0) == 0x80) n--;
    kb_target[n] = 0;
}
#define KB_H 330.f
#define KB_BAR 50.f
// ---- Sugerencias. Solo se usan en el chat: nunca con contraseñas.
#define DIC_PATH "/usr/local/share/fosil/palabras.txt"
static bool kb_sugerir; static char *dic_pool, *dic_fold; static unsigned *dic_off; static int dic_n = 0; static bool dic_ok = false;
static char kb_sug[3][64]; static int kb_nsug = 0; static char kb_prev[64] = "\x01"; static bool kb_hay_fix = false;
static size_t fold_utf8(const char *s, char *o, size_t cap) {
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p && n + 1 < cap; p++) {
        if (*p == 0xC3 && p[1]) {
            p++; char c = 0;
            switch (*p | 0x20) {
                case 0xA1: c = 'a'; break; case 0xA9: c = 'e'; break; case 0xAD: c = 'i'; break;
                case 0xB3: c = 'o'; break; case 0xBA: case 0xBC: c = 'u'; break; case 0xB1: c = 'n'; break;
            }
            if (!c) return 0;
            o[n++] = c;
        } else if (*p >= 'A' && *p <= 'Z') o[n++] = *p + 32;
        else if (*p >= 'a' && *p <= 'z') o[n++] = *p;
        else return 0;   // números, rutas o comandos: sin sugerencias
    }
    o[n] = 0; return n;
}
static void dic_load(void) {
    dic_ok = true;
    FILE *f = fopen(DIC_PATH, "r"); if (!f) return;
    fseek(f, 0, SEEK_END); long z = ftell(f); fseek(f, 0, SEEK_SET);
    dic_pool = malloc(z + 1); dic_fold = malloc(z + 1);
    if (!dic_pool || !dic_fold || fread(dic_pool, 1, z, f) != (size_t)z) { fclose(f); dic_n = 0; return; }
    fclose(f); dic_pool[z] = 0;
    int lines = 0; for (long i = 0; i < z; i++) if (dic_pool[i] == '\n') lines++;
    dic_off = malloc(sizeof(unsigned) * (lines + 1)); size_t fo = 0;
    for (char *sv, *t = strtok_r(dic_pool, "\n", &sv); t; t = strtok_r(NULL, "\n", &sv)) {
        size_t n = fold_utf8(t, dic_fold + fo, 64); if (!n) continue;
        dic_off[dic_n++] = fo | ((unsigned)0); fo += n + 1;
    }
    // Para recuperar la forma original se guarda en paralelo el desplazamiento en el pool.
    unsigned *orig = malloc(sizeof(unsigned) * (dic_n + 1)); int k = 0; char *p = dic_pool, *end = dic_pool + z;
    while (p < end && k < dic_n) { size_t l = strlen(p); char tmp[64]; if (l && l < 64 && fold_utf8(p, tmp, 64)) orig[k++] = p - dic_pool; p += l + 1; }
    dic_off = realloc(dic_off, sizeof(unsigned) * (dic_n * 2 + 1)); memcpy(dic_off + dic_n, orig, sizeof(unsigned) * dic_n); free(orig);
}
static int key_xy(char c, float *x, float *y) {
    static const char *r[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"}; static const float ox[3] = {0, 0, 1.5f};
    for (int j = 0; j < 3; j++) { const char *q = strchr(r[j], c); if (q) { *x = q - r[j] + ox[j]; *y = j; return 1; } }
    return 0;
}
static int key_cost(char a, char b) {
    if (a == b) return 0;
    if ((a == 'n' && strchr("lop", b)) || (b == 'n' && strchr("lop", a))) return 1;   // la ñ se pliega a n
    float ax, ay, bx, by; if (!key_xy(a, &ax, &ay) || !key_xy(b, &bx, &by)) return 3;
    return fabsf(ax - bx) <= 1.2f && fabsf(ay - by) <= 1 ? 1 : 3;   // tecla vecina: casi seguro un error del dedo
}
// ---- Autocorrector: elige la palabra más probable según lo cerca que está de lo escrito
// y lo frecuente que es (palabras.txt va ordenado por frecuencia).
#define MIAS_PATH STATE_DIR "/palabras-mias.txt"
#define MAX_MIAS 256
static char kb_mias[MAX_MIAS][64]; static int kb_nmias = 0;
static unsigned char sust[26][26]; static unsigned *dic_mask; static bool sust_ok = false;
static unsigned letras(const char *w) { unsigned m = 0; for (; *w; w++) m |= 1u << (*w - 'a'); return m; }
static void sust_init(void) {
    static const char *fon[] = {"bv", "sz", "cz", "cs", "ck", "qk", "cq", "yi", "gj", "ly"};   // faltas de ortografía habituales
    for (int a = 0; a < 26; a++) for (int b = 0; b < 26; b++) sust[a][b] = a == b ? 0 : key_cost(a + 'a', b + 'a') == 1 ? 2 : 4;
    for (size_t i = 0; i < sizeof fon / sizeof *fon; i++) { int a = fon[i][0] - 'a', b = fon[i][1] - 'a'; sust[a][b] = sust[b][a] = 2; }
    dic_mask = malloc(sizeof(unsigned) * (dic_n + 1));
    if (dic_mask) for (int i = 0; i < dic_n; i++) dic_mask[i] = letras(dic_fold + dic_off[i]);
    sust_ok = true;
}
// Distancia con pesos: tecla vecina, falta habitual, letras cambiadas o letra de más o de menos: 2. Otra tecla: 4. Una h, una letra doble o la u de qu: 1.
// Devuelve -1 si supera lim.
static int dist_pesos(const char *a, int la, const char *b, int lb, int lim) {
    static short d[3][66]; short *p2 = d[0], *p1 = d[1], *c = d[2];
    #define COSTE_LETRA(s, k) ((s)[k] == 'h' || ((k) > 0 && ((s)[k] == (s)[(k) - 1] || ((s)[k] == 'u' && (s)[(k) - 1] == 'q'))) ? 1 : 2)
    p1[0] = 0; for (int j = 1; j <= lb; j++) p1[j] = p1[j - 1] + COSTE_LETRA(b, j - 1);
    for (int i = 1; i <= la; i++) {
        c[0] = p1[0] + COSTE_LETRA(a, i - 1); int mn = c[0];
        for (int j = 1; j <= lb; j++) {
            int v = p1[j - 1] + sust[a[i - 1] - 'a'][b[j - 1] - 'a'], x = p1[j] + COSTE_LETRA(a, i - 1), y = c[j - 1] + COSTE_LETRA(b, j - 1);
            if (x < v) v = x;
            if (y < v) v = y;
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1] && p2[j - 2] + 2 < v) v = p2[j - 2] + 2;
            c[j] = v; if (v < mn) mn = v;
        }
        if (mn > lim) return -1;
        short *t = p2; p2 = p1; p1 = c; c = t;
    }
    return p1[lb] > lim ? -1 : p1[lb];
}
static void minus_utf8(const char *s, char *o, size_t cap) {
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p && n + 1 < cap; p++) {
        unsigned char c = *p;
        if (c >= 'A' && c <= 'Z') c += 32;
        else if (c >= 0x80 && c < 0xA0 && p > (const unsigned char *)s && p[-1] == 0xC3) c += 0x20;
        o[n++] = c;
    }
    o[n] = 0;
}
// Escribe en out la corrección de una palabra suelta. false si está bien escrita o no hay candidata.
static bool kb_corrige(const char *word, char *out, size_t cap) {
    char t[64], low[64]; int L = fold_utf8(word, t, sizeof t);
    if (L < 3 || L > 62 || !dic_n) return false;
    if (!sust_ok) sust_init();
    minus_utf8(word, low, sizeof low);
    for (int i = 0; i < kb_nmias; i++) if (!strcmp(kb_mias[i], low)) return false;
    bool sin_acento = !strchr(low, 0xC3);
    int lim = L <= 4 ? 2 : L <= 6 ? 3 : L <= 9 ? 4 : 6, igual = -1, mejor = -1; float best = 1e9f; unsigned mt = letras(t);
    for (int i = 0; i < dic_n; i++) {
        if (dic_mask && __builtin_popcount(mt ^ dic_mask[i]) > lim) continue;   // cada letra distinta cuesta al menos 1
        const char *w = dic_fold + dic_off[i]; int lw = strlen(w);
        if (lw > 62 || abs(lw - L) > lim) continue;
        int d = dist_pesos(t, L, w, lw, lim);
        if (d < 0) continue;
        if (d == 0) {
            if (!strcmp(dic_pool + dic_off[dic_n + i], low)) {   // existe: solo se cambia si es una falta de acento clarísima
                if (sin_acento && igual >= 0 && (long)igual * 8 < i) break;
                return false;
            }
            if (igual < 0) igual = i;
            continue;
        }
        float sc = d * 5.f + log2f(i + 2);
        if (sc < best) { best = sc; mejor = i; }
    }
    int r = igual >= 0 ? igual : mejor;
    if (r < 0) return false;
    snprintf(out, cap, "%s", dic_pool + dic_off[dic_n + r]);
    return true;
}
static bool es_mayus(const char *s) { return (*s >= 'A' && *s <= 'Z') || ((unsigned char)s[0] == 0xC3 && (unsigned char)s[1] < 0xA0); }
static void capitaliza(char *o) { if (o[0] >= 'a' && o[0] <= 'z') o[0] -= 32; else if ((unsigned char)o[0] == 0xC3) o[1] -= 0x20; }
// Mayúsculas a mitad de frase o dentro de la palabra: nombres y siglas, no se tocan.
static bool kb_palabra_libre(const char *pal, const char *inicio) {
    for (const char *p = pal + 1; *p; p++) if (es_mayus(p)) return false;
    if (!es_mayus(pal)) return true;
    return inicio == kb_target || (inicio - kb_target >= 2 && inicio[-1] == ' ' && strchr(".?!", inicio[-2]));
}
static void kb_mias_carga(void) {
    FILE *f = fopen(MIAS_PATH, "r"); if (!f) return;
    while (kb_nmias < MAX_MIAS && fgets(kb_mias[kb_nmias], 64, f)) { kb_mias[kb_nmias][strcspn(kb_mias[kb_nmias], "\n")] = 0; if (*kb_mias[kb_nmias]) kb_nmias++; }
    fclose(f);
}
static void kb_mias_add(const char *word) {
    char low[64]; minus_utf8(word, low, sizeof low);
    for (int i = 0; i < kb_nmias; i++) if (!strcmp(kb_mias[i], low)) return;
    if (kb_nmias < MAX_MIAS) snprintf(kb_mias[kb_nmias++], 64, "%s", low);
    FILE *f = fopen(MIAS_PATH, "a"); if (f) { fprintf(f, "%s\n", low); fclose(f); }
}
static void kb_suggest(const char *word, bool upper) {
    kb_nsug = 0; kb_hay_fix = false; char t[64]; size_t L = fold_utf8(word, t, sizeof t);
    if (!L || !dic_n) return;
    char fix[64];
    if (kb_palabra_libre(word, word) && kb_corrige(word, fix, sizeof fix)) { kb_hay_fix = true; snprintf(kb_sug[kb_nsug++], 64, "%s", fix); if (upper) capitaliza(kb_sug[0]); }
    static unsigned char costs[26][26]; static bool costs_ready;
    if (!costs_ready) {
        for (int a = 0; a < 26; a++) for (int b = 0; b < 26; b++)
            costs[a][b] = key_cost('a' + a, 'a' + b);
        costs_ready = true;
    }
    int maxc = L <= 2 ? 0 : L <= 4 ? 1 : L <= 7 ? 2 : 3;
    long best[3] = {1L << 40, 1L << 40, 1L << 40}; int bi[3] = {-1, -1, -1};
    for (int i = 0; i < dic_n; i++) {
        const char *w = dic_fold + dic_off[i]; int c = 0; size_t j = 0;
        for (; j < L && w[j]; j++) { c += costs[t[j] - 'a'][w[j] - 'a']; if (c > maxc) break; }
        if (c > maxc || j < L) continue;
        size_t lw = strlen(w);
        long sc = c * 15000L + i + (lw - L > 6 ? 4000 : 0) - (lw == L ? 8000 : 0);
        for (int b = 0; b < 3; b++) if (sc < best[b]) {
            for (int m = 2; m > b; m--) { best[m] = best[m - 1]; bi[m] = bi[m - 1]; }
            best[b] = sc; bi[b] = i; break;
        }
    }
    for (int b = 0; b < 3 && kb_nsug < 3; b++) if (bi[b] >= 0) {
        char *o = kb_sug[kb_nsug]; snprintf(o, 64, "%s", dic_pool + dic_off[dic_n + bi[b]]);
        if (upper) capitaliza(o);
        if (!kb_hay_fix || strcmp(o, kb_sug[0])) kb_nsug++;
    }
}
static const char *kb_word_start(void) { const char *sp = strrchr(kb_target, ' '); return sp ? sp + 1 : kb_target; }
// Corrige la última palabra al pulsar espacio o Enviar. La puntuación pegada se conserva.
static char kb_und_antes[64], kb_und_despues[80]; static int kb_und_off = -1;
static void kb_autocorrige(const char *sep) {
    kb_und_off = -1;
    if (!kb_sugerir) return;
    char *ini = (char *)kb_word_start(), *a = ini, *z = ini + strlen(ini);
    while (*a && strchr("(\"'", *a)) a++;
    while (z > a && strchr(".,?!;:)\"'", z[-1])) z--;
    size_t n = z - a, cola = strlen(z);
    if (!n || n >= 64 || z - ini + cola >= sizeof kb_und_antes) return;
    char pal[64], fix[64], res[80]; memcpy(pal, a, n); pal[n] = 0;
    if (!kb_palabra_libre(pal, ini) || !kb_corrige(pal, fix, sizeof fix)) return;
    if (es_mayus(pal)) capitaliza(fix);
    size_t f = strlen(fix), pre = a - ini;
    if ((size_t)(a - kb_target) + f + cola + strlen(sep) + 1 >= kb_cap || pre + f + cola + strlen(sep) >= sizeof res) return;
    snprintf(kb_und_antes, sizeof kb_und_antes, "%s", ini);
    memmove(a + f, z, cola + 1); memcpy(a, fix, f);
    snprintf(res, sizeof res, "%s%s", ini, sep);
    snprintf(kb_und_despues, sizeof kb_und_despues, "%s", res);
    kb_und_off = ini - kb_target;
}
// Borrar justo después de corregir recupera lo escrito y recuerda esa palabra.
static bool kb_deshaz(void) {
    if (kb_und_off < 0 || (size_t)kb_und_off > strlen(kb_target) || strcmp(kb_target + kb_und_off, kb_und_despues)) return false;
    strcpy(kb_target + kb_und_off, kb_und_antes);
    char pal[64]; const char *a = kb_und_antes; while (*a && strchr("(\"'", *a)) a++;
    snprintf(pal, sizeof pal, "%s", a);
    size_t n = strlen(pal); while (n && strchr(".,?!;:)\"'", pal[n - 1])) pal[--n] = 0;
    if (n) kb_mias_add(pal);
    kb_und_off = -1;
    return true;
}
// Mayúscula automática al empezar frase.
static void kb_auto(void) {
    if (!kb_sugerir) return;
    size_t n = strlen(kb_target);
    if (n == 0 || (n >= 2 && kb_target[n - 1] == ' ' && strchr(".?!", kb_target[n - 2]))) kb_shift = true;
}
static const char *kb_acento(const char *l) {
    static const struct { const char *a, *b; } m[] = {{"a", "á"}, {"e", "é"}, {"i", "í"}, {"o", "ó"}, {"u", "ú"}, {"A", "Á"}, {"E", "É"}, {"I", "Í"}, {"O", "Ó"}, {"U", "Ú"}};
    for (size_t i = 0; i < sizeof m / sizeof *m; i++) if (!strcmp(l, m[i].a)) return m[i].b;
    return NULL;
}
// Devuelve true si se pulsa Intro.
static bool keyboard(const char *enter_label) {
    double t = clampf((now_s() - kb_open_t) / .22f, 0, 1);
    float off = (1 - ease_out(t)) * KB_H;
    float y0 = H - KB_H + off;
    float ox = OY; OY = off;
    rr(0, H - KB_H - 6, W, KB_H + 40, 22, rgba(T.bg[0] * .55f, T.bg[1] * .55f, T.bg[2] * .55f, .96f));
    ring(-2, H - KB_H - 6, W + 4, KB_H + 40, 22, 1.2f, T.line);
    bool enter = false;
    if (ui.press) kb_touch = ui.y >= H - KB_H - 8 - (kb_sugerir ? KB_BAR : 0);
    double now_k = now_s();
    if (kb_sugerir) {   // barra de sugerencias
        if (!dic_ok) { dic_load(); kb_mias_carga(); }
        const char *ws = kb_word_start();
        static double suggest_after; static bool suggest_pending;
        if (strcmp(ws, kb_prev)) {
            snprintf(kb_prev, sizeof kb_prev, "%s", ws);
            kb_nsug = 0; kb_hay_fix = false;
            suggest_after = now_k + .10; suggest_pending = true;
        }
        // La entrada tiene prioridad: buscar cuando el dedo ya se ha levantado.
        if (suggest_pending && !ui.down && !ui.release && now_k >= suggest_after) {
            kb_suggest(ws, es_mayus(ws)); suggest_pending = false;
        }
        float by = H - KB_H - 6 - KB_BAR;
        rr(0, by, W, KB_BAR + 12, 0, rgba(T.bg[0] * .55f, T.bg[1] * .55f, T.bg[2] * .55f, .96f));
        float cw = W / 3.f;
        for (int i = 0; i < kb_nsug; i++) {
            unsigned id = 5900 + i;
            if (kb_btn(id, i * cw, by, cw, KB_BAR)) {
                kb_und_off = -1; *(char *)kb_word_start() = 0; kb_append(kb_sug[i]); kb_append(" "); kb_shift = false; kb_auto();
            }
            if (kb_on(id)) rr(i * cw + 4, by + 4, cw - 8, KB_BAR - 8, 12, rgba(1, 1, 1, .2f));
            char v[64]; snprintf(v, sizeof v, "%s", kb_sug[i]);
            text_fit(F_SANS, 22, v, i * cw + 14, by + 12, cw - 24, i == 0 && kb_hay_fix ? T.accent : T.text);
            if (i) rr(i * cw, by + 12, 1, KB_BAR - 24, 0, T.line);
        }
    }
    float ky = H - KB_H + 8, kh = 72, gap = 7;
    y0 = ky;
    for (int row = 0; row < 3; row++) {
        char tmp[96]; snprintf(tmp, sizeof tmp, "%s", KB[kb_layer][row]);
        char *keys[12]; int n = 0;
        char *sv, *tk = strtok_r(tmp, " ", &sv);
        while (tk && n < 12) { keys[n++] = tk; tk = strtok_r(NULL, " ", &sv); }
        float side = row == 2 ? 78 : 0;
        float area = W - 12 - side * 2;
        float kw = (area - gap * (n - 1)) / n;
        float x0 = 6 + side;
        if (row == 2) {
            if (kb_layer == 0) {
                unsigned id = ID();
                if (kb_btn(id, 0, y0 + row * (kh + gap) - gap / 2, 6 + side, kh + gap)) kb_shift = !kb_shift;
                C bg = kb_shift ? T.accent : rgba(1, 1, 1, .10f);
                rr(6, y0 + row * (kh + gap), side - gap, kh, 11, bg);
                text(F_SANS, 26, "⇧", 6 + (side - gap) / 2, y0 + row * (kh + gap) + 20, kb_shift ? ON_ACC : T.text, 1);
            } else {
                unsigned id = ID();
                if (kb_btn(id, 0, y0 + row * (kh + gap) - gap / 2, 6 + side, kh + gap)) kb_layer = kb_layer == 1 ? 2 : 1;
                rr(6, y0 + row * (kh + gap), side - gap, kh, 11, rgba(1, 1, 1, .10f));
                text(F_SANS, 20, kb_layer == 1 ? "#+=" : "123", 6 + (side - gap) / 2, y0 + row * (kh + gap) + 24, T.text, 1);
            }
            unsigned idb = ID();
            float bx = W - 6 - side + gap;
            static double bs_t = 0, bs_r = 0; static bool bs_rep = false;
            if (kb_btn(idb, bx - gap, y0 + row * (kh + gap) - gap / 2, W - bx + gap, kh + gap)) { if (!bs_rep && !kb_deshaz()) kb_back(); bs_rep = false; kb_auto(); }
            if (kb_on(idb)) {   // mantener pulsado: borrar seguido
                if (bs_t == 0) { bs_t = now_k; bs_r = now_k + .45; bs_rep = false; }
                if (now_k >= bs_r) { kb_back(); bs_rep = true; bs_r = now_k + .06; }
            } else bs_t = 0;
            rr(bx, y0 + row * (kh + gap), side - gap, kh, 11, rgba(1, 1, 1, kb_on(idb) ? .28f : .10f));
            text(F_SANS, 24, "⌫", bx + (side - gap) / 2, y0 + row * (kh + gap) + 21, T.text, 1);
        }
        for (int i = 0; i < n; i++) {
            float x = x0 + i * (kw + gap), y = y0 + row * (kh + gap);
            unsigned id = 5000 + row * 100 + i;
            char lab[16]; snprintf(lab, sizeof lab, "%s", keys[i]);
            if (kb_layer == 0 && kb_shift) {
                if (!strcmp(lab, "ñ")) strcpy(lab, "Ñ");
                else if (lab[0] >= 'a' && lab[0] <= 'z') lab[0] -= 32;
            }
            float hx = i == 0 && side == 0 ? 0 : x - gap / 2, hr = i == n - 1 && side == 0 ? W : x + kw + gap / 2;
            float hy = y - gap / 2 - (row == 0 ? 6 : 0), hh = kh + gap + (row == 0 ? 6 : 0);
            static unsigned hold_id = 0; static double hold_t = 0;
            const char *ac = kb_layer == 0 ? kb_acento(lab) : NULL;
            bool larga = ac && hold_id == id && now_k - hold_t > .4;   // mantener una vocal: acento
            if (kb_btn(id, hx, hy, hr - hx, hh)) { kb_append(larga ? ac : lab); kb_shift = false; kb_auto(); hold_id = 0; }
            bool p = kb_on(id);
            if (p && hold_id != id) { hold_id = id; hold_t = now_k; }
            rr(x, y, kw, kh, 11, rgba(1, 1, 1, p ? .30f : .16f));
            text(F_SANS, 27, lab, x + kw / 2, y + 18, T.text, 1);
            if (p) {  // burbuja de pulsación
                rr(x - 6, y - 84, kw + 12, 76, 14, T.accent);
                text(F_SANS, 38, larga ? ac : lab, x + kw / 2, y - 72, ON_ACC, 1);
            }
        }
    }
    float y = y0 + 3 * (kh + gap);
    float bw = 92;
    { unsigned id = ID();
      if (kb_btn(id, 0, y - gap / 2, 6 + bw + gap / 2, H - y + gap / 2)) kb_layer = kb_layer == 0 ? 1 : 0;
      rr(6, y, bw, kh, 11, rgba(1, 1, 1, .10f));
      text(F_SANS, 22, kb_layer == 0 ? "123" : "abc", 6 + bw / 2, y + 23, T.text, 1); }
    { unsigned id = ID(); float x = 6 + bw + gap, w = W - 12 - bw * 2 - 30 - gap * 2;
      if (kb_btn(id, x - gap / 2, y - gap / 2, w + gap, H - y + gap / 2)) { kb_autocorrige(" "); kb_append(" "); kb_auto(); }
      rr(x, y, w, kh, 11, rgba(1, 1, 1, kb_on(id) ? .30f : .16f)); }
    { unsigned id = ID(); float w = bw + 30, x = W - 6 - w;
      if (kb_btn(id, x - gap / 2, y - gap / 2, W - x + gap / 2, H - y + gap / 2)) { kb_autocorrige(""); kb_und_off = -1; enter = true; }
      rr(x, y, w, kh, 11, T.accent);
      text(F_SANS, 22, enter_label, x + w / 2, y + 23, ON_ACC, 1); }
    OY = ox;
    return enter;
}
static void kb_show(char *target, size_t cap) {
    kb_target = target; kb_cap = cap; kb_layer = 0; kb_shift = false; kb_open = true; kb_open_t = now_s(); kb_sugerir = false; kb_und_off = -1;
}
static void kb_show_texto(char *target, size_t cap) { kb_show(target, cap); kb_sugerir = true; kb_prev[0] = 1; kb_auto(); }

// ---------------------------------------------------------------- autenticación
static char pw_buf[128]; static volatile int auth_state = 0;  // 0 nada, 1 comprobando, 2 ok, 3 recién fallada, 4 fallada
static char auth_user[64] = "";  // el usuario de config_cargar(), salvo FOSIL_USUARIO
static int pam_conv_fn(int n, const struct pam_message **m, struct pam_response **r, void *d) {
    struct pam_response *resp = calloc(n, sizeof *resp);
    for (int i = 0; i < n; i++)
        if (m[i]->msg_style == PAM_PROMPT_ECHO_OFF || m[i]->msg_style == PAM_PROMPT_ECHO_ON)
            resp[i].resp = strdup((const char *)d);
    *r = resp;
    return PAM_SUCCESS;
}
static void *auth_thread(void *arg) {
    char *pw = arg;
    struct pam_conv conv = {pam_conv_fn, pw};
    pam_handle_t *h = NULL;
    int rc = pam_start("login", auth_user, &conv, &h);
    if (rc == PAM_SUCCESS) rc = pam_authenticate(h, 0);
    if (rc == PAM_SUCCESS) rc = pam_acct_mgmt(h, 0);
    if (rc != PAM_SUCCESS) fprintf(stderr, "PAM: %s\n", h ? pam_strerror(h, rc) : "sin sesión");
    if (h) pam_end(h, rc);
    explicit_bzero(pw, strlen(pw)); free(pw);
    auth_state = rc == PAM_SUCCESS ? 2 : 3;
    wake();
    return NULL;
}
static void auth_start(void) {
    auth_state = 1;
    char *copy = strdup(pw_buf);
    explicit_bzero(pw_buf, sizeof pw_buf);
    pthread_t th; pthread_create(&th, NULL, auth_thread, copy); pthread_detach(th);
}

// ---------------------------------------------------------------- red: conectar
static char net_pw[128]; static char net_target[64]; static bool net_prompt = false; static volatile int net_busy = 0;
typedef struct { char ssid[64]; char pw[128]; bool saved; } ConnJob;
static void shquote(char *out, size_t cap, const char *s) {
    size_t o = 0; out[o++] = '\'';
    for (; *s && o + 5 < cap; s++) {
        if (*s == '\'') { memcpy(out + o, "'\\''", 4); o += 4; } else out[o++] = *s;
    }
    out[o++] = '\''; out[o] = 0;
}
static void *conn_thread(void *arg) {
    ConnJob *j = arg;
    char qs[160], qp[300], cmd[600];
    shquote(qs, sizeof qs, j->ssid); shquote(qp, sizeof qp, j->pw);
    if (j->saved) snprintf(cmd, sizeof cmd, "nmcli con up id %s >/dev/null 2>&1", qs);
    else if (*j->pw) snprintf(cmd, sizeof cmd, "nmcli dev wifi connect %s password %s >/dev/null 2>&1", qs, qp);
    else snprintf(cmd, sizeof cmd, "nmcli dev wifi connect %s >/dev/null 2>&1", qs);
    int rc = system(cmd);
    pthread_mutex_lock(&st.m);
    pthread_mutex_unlock(&st.m);
    if (rc == 0) toast("Conectado a %s", j->ssid); else toast("No se pudo conectar a %s", j->ssid);
    explicit_bzero(j->pw, sizeof j->pw); free(j);
    net_busy = 0;
    start_scan();
    wake();
    return NULL;
}
static void connect_to(const char *ssid, const char *pw, bool saved) {
    ConnJob *j = calloc(1, sizeof *j);
    snprintf(j->ssid, sizeof j->ssid, "%s", ssid); snprintf(j->pw, sizeof j->pw, "%s", pw); j->saved = saved;
    net_busy = 1; toast("Conectando a %s…", ssid);
    pthread_t th; pthread_create(&th, NULL, conn_thread, j); pthread_detach(th);
}

// ---------------------------------------------------------------- iconos
enum { I_TERM, I_SYS, I_NET, I_AGENT, I_SET, I_POWER, I_BACK, I_APPS, I_FILES, I_TASK };
static void icon(int kind, float cx, float cy, float s, C c) {
    switch (kind) {
    case I_TERM:
        ring(cx - s, cy - s * .78f, s * 2, s * 1.56f, s * .28f, 3, c);
        text(F_MONO, (int)(s * .9f), ">_", cx, cy - s * .62f, c, 1);
        break;
    case I_SYS: {
        float h[3] = {.7f, 1.3f, 1.f};
        for (int i = 0; i < 3; i++) {
            float bh = s * h[i];
            rr(cx - s * .85f + i * s * .65f, cy + s * .7f - bh, s * .42f, bh, s * .12f, c);
        }
        break; }
    case I_NET:
        ring(cx - s, cy - s, s * 2, s * 2, s, 3, alpha(c, .45f));
        ring(cx - s * .62f, cy - s * .62f, s * 1.24f, s * 1.24f, s * .62f, 3, alpha(c, .75f));
        rr(cx - s * .22f, cy - s * .22f, s * .44f, s * .44f, s * .22f, c);
        break;
    case I_AGENT:
        ring(cx - s * .9f, cy - s * .7f, s * 1.8f, s * 1.5f, s * .42f, 3, c);
        rr(cx - s * .46f, cy - s * .22f, s * .26f, s * .26f, s * .13f, c);
        rr(cx + s * .20f, cy - s * .22f, s * .26f, s * .26f, s * .13f, c);
        rr(cx - 1.5f, cy - s * 1.05f, 3, s * .35f, 1.5f, c);
        rr(cx - s * .1f, cy - s * 1.16f, s * .2f, s * .2f, s * .1f, c);
        break;
    case I_SET:
        for (int i = 0; i < 3; i++) {
            float y = cy - s * .7f + i * s * .7f, k = cx + (i == 1 ? s * .35f : i == 0 ? -s * .35f : s * .05f);
            rr(cx - s, y - 1.5f, s * 2, 3, 1.5f, alpha(c, .55f));
            rr(k - s * .2f, y - s * .2f, s * .4f, s * .4f, s * .2f, c);
        }
        break;
    case I_POWER:
        ring(cx - s * .85f, cy - s * .7f, s * 1.7f, s * 1.7f, s * .85f, 3, c);
        rr(cx - 3, cy - s * 1.05f, 6, s * 1.05f, 3, c);
        break;
    case I_BACK:
        text(F_SANS, (int)(s * 2), "‹", cx, cy - s * 1.25f, c, 1);
        break;
    case I_APPS:
        for (int i = 0; i < 4; i++) rr(cx - s * .85f + (i % 2) * s * .95f, cy - s * .85f + (i / 2) * s * .95f, s * .75f, s * .75f, s * .2f, c);
        break;
    case I_FILES:
        ring(cx - s, cy - s * .55f, s * 2, s * 1.4f, s * .2f, 3, c);
        rr(cx - s, cy - s * .85f, s * .9f, s * .4f, s * .15f, c);
        break;
    case I_TASK:
        for (int i = 0; i < 3; i++) { rr(cx - s, cy - s * .7f + i * s * .65f, s * .3f, s * .3f, s * .15f, c); rr(cx - s * .45f, cy - s * .62f + i * s * .65f, s * 1.4f, s * .16f, s * .08f, c); }
        break;
    }
}

// ---------------------------------------------------------------- pantallas
static const char *DIAS[] = {"domingo", "lunes", "martes", "miércoles", "jueves", "viernes", "sábado"};
static const char *MESES[] = {"enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre"};
static void time_strs(char *hm, size_t hl, char *date, size_t dl) {
    time_t t = time(NULL); struct tm m; localtime_r(&t, &m);
    snprintf(hm, hl, "%02d:%02d", m.tm_hour, m.tm_min);
    snprintf(date, dl, "%s, %d de %s", DIAS[m.tm_wday], m.tm_mday, MESES[m.tm_mon]);
}

// ---------------------------------------------------------------- PIN
// Guarda solo un hash lento en un fichero de root. Tras 5 fallos exige la contraseña.
#define PIN_FILE STATE_DIR "/pin"
#define PIN_MAX_FALLOS 5
static char pin_hash[140]; static int pin_len = 0, pin_fails = 0;
static char pin_in[16], pin_first[16]; static volatile int pin_state = 0;  // 0 nada, 1 comprobando, 2 ok, 3 mal
static bool lock_pin = false; static int pin_stage = 0;  // S_PIN: 0 contraseña, 1 nuevo, 2 confirmar
static double pin_shake = -10;
static void pin_write(void) {
    if (!pin_len) { unlink(PIN_FILE); return; }
    int fd = open(PIN_FILE ".tmp", O_WRONLY | O_CREAT | O_TRUNC, 0600); if (fd < 0) return;
    FILE *f = fdopen(fd, "w"); if (!f) { close(fd); return; }
    fprintf(f, "%s %d %d\n", pin_hash, pin_len, pin_fails); fclose(f);
    rename(PIN_FILE ".tmp", PIN_FILE);
}
static void pin_load(void) {
    FILE *f = fopen(PIN_FILE, "r"); if (!f) return;
    if (fscanf(f, "%139s %d %d", pin_hash, &pin_len, &pin_fails) != 3) { pin_len = 0; pin_hash[0] = 0; }
    fclose(f);
}
static bool pin_usable(void) { return pin_len >= 4 && pin_fails < PIN_MAX_FALLOS; }
static bool pin_set(const char *pin) {
    static const char al[] = "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    unsigned char r[16]; int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0 || read(fd, r, sizeof r) != sizeof r) { if (fd >= 0) close(fd); return false; }
    close(fd);
    char salt[64] = "$6$rounds=60000$"; size_t n = strlen(salt);
    for (int i = 0; i < 16; i++) salt[n++] = al[r[i] % 64];
    salt[n++] = '$'; salt[n] = 0;
    char *h = crypt(pin, salt);
    if (!h || h[0] != '$') return false;
    snprintf(pin_hash, sizeof pin_hash, "%s", h); pin_len = strlen(pin); pin_fails = 0; pin_write();
    return true;
}
static void *pin_thread(void *arg) {
    char *pin = arg; char *h = crypt(pin, pin_hash);
    bool ok = h && !strcmp(h, pin_hash);
    explicit_bzero(pin, strlen(pin)); free(pin);
    pin_state = ok ? 2 : 3; wake();
    return NULL;
}
static void pin_check_start(void) {
    pin_state = 1; char *c = strdup(pin_in); explicit_bzero(pin_in, sizeof pin_in);
    pthread_t th; pthread_create(&th, NULL, pin_thread, c); pthread_detach(th);
}
// Teclado numérico a toda anchura. Devuelve la cifra, 10 borrar, 11 aceptar o -1.
static int pinpad(float y0, bool ok_key) {
    if (ui.press) kb_touch = ui.y >= y0 - 8;
    static const char *lab[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "⌫", "0", "OK"};
    float cw = (W - 40) / 3.f, ch = 104; int r = -1;
    for (int i = 0; i < 12; i++) {
        if (i == 11 && !ok_key) continue;
        int cx = i % 3, cy = i / 3;
        float x = 20 + cx * cw, y = y0 + cy * ch;
        unsigned id = 7000 + i;
        float hx = cx == 0 ? 0 : x, hr = cx == 2 ? W : x + cw;
        if (kb_btn(id, hx, y, hr - hx, cy == 3 ? H - y : ch)) r = i == 9 ? 10 : i == 10 ? 0 : i == 11 ? 11 : i + 1;
        bool digit = i != 9 && i != 11;
        rr(x + 8, y + 6, cw - 16, ch - 12, 28, kb_on(id) ? alpha(T.accent, .55f) : i == 11 ? alpha(T.accent, .9f) : rgba(1, 1, 1, digit ? .13f : .06f));
        text(F_SANS, digit ? 40 : 28, lab[i], x + cw / 2, y + (digit ? 26 : 34), i == 11 ? ON_ACC : T.text, 1);
    }
    return r;
}
static void pin_dots(float y, int n, int total) {
    float shake = 0; double ds = now_s() - pin_shake;
    if (ds < .4) shake = sinf(ds * 46) * 14 * (1 - ds / .4f);
    float w = total * 34, x = W / 2.f - w / 2 + 17 + shake;
    for (int i = 0; i < total; i++) {
        if (i < n) rr(x + i * 34 - 9, y, 18, 18, 9, T.accent);
        else ring(x + i * 34 - 9, y, 18, 18, 9, 1.6f, alpha(T.sub, .8f));
    }
}
static void pin_key(int k) {
    size_t n = strlen(pin_in);
    if (k == 10) { if (n) pin_in[n - 1] = 0; }
    else if (k >= 0 && k <= 9 && n < 8) { pin_in[n] = '0' + k; pin_in[n + 1] = 0; }
}
static void draw_lock_pin(void) {
    char hm[16], date[64]; time_strs(hm, sizeof hm, date, sizeof date);
    text(F_SANS, 72, hm, W / 2.f, 70, T.text, 1);
    text(F_SANS, 22, date, W / 2.f, 164, T.sub, 1);
    text(F_SANS, 24, pin_fails ? "PIN incorrecto" : "Escribe tu PIN", W / 2.f, 232, pin_fails ? rgba(1, .55f, .55f, 1) : T.sub, 1);
    pin_dots(282, (int)strlen(pin_in), pin_len);
    int k = pinpad(340, false);
    if (pin_state == 0 && k >= 0) { pin_key(k); if ((int)strlen(pin_in) == pin_len) pin_check_start(); }
    float x = 20 + (W - 40) * 2 / 3.f + 8, y = 340 + 3 * 104 + 6;
    // La casilla libre del teclado numérico lleva a la contraseña.
    unsigned idc = 7100;
    if (kb_btn(idc, x - 8, y - 6, W - x + 8, H - y + 6)) {
        explicit_bzero(pin_in, sizeof pin_in); lock_pin = false; pw_buf[0] = 0; auth_state = 0; kb_show(pw_buf, sizeof pw_buf);
    }
    rr(x, y, (W - 40) / 3.f - 16, 92, 28, rgba(1, 1, 1, kb_on(idc) ? .2f : .06f));
    text(F_SANS, 18, "Contraseña", x + ((W - 40) / 3.f - 16) / 2, y + 36, T.sub, 1);
}
// Gracia: si la pantalla se apagó hace menos de 5 min con la sesión abierta, se vuelve sin PIN.
// CLOCK_BOOTTIME sigue contando si el móvil se suspende. "Bloquear" en Energía no deja gracia.
#define PIN_GRACIA 300
static Screen gracia_screen = S_LOCK; static double gracia_t;
static double boot_s(void) {
    struct timespec ts; clock_gettime(CLOCK_BOOTTIME, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static void gracia_armar(void) {
    if (screen == S_LOCK) return;
    gracia_screen = screen == S_PIN ? S_SETTINGS : screen; gracia_t = boot_s();
}
static void gracia_volver(void) {
    Screen s = gracia_screen; gracia_screen = S_LOCK;
    if (s != S_LOCK && pin_usable() && boot_s() - gracia_t < PIN_GRACIA) { go(s, 1); trans_start = -10; }
}
static void pin_poll(void) {
    if (pin_state == 2) {
        pin_state = 0; pin_fails = 0; pin_write(); lock_pin = false; go(S_HOME, 1);
    } else if (pin_state == 3) {
        pin_state = 0; pin_fails++; pin_write(); pin_shake = now_s(); vibrate();
        if (pin_fails >= PIN_MAX_FALLOS) {
            lock_pin = false; pw_buf[0] = 0; auth_state = 0; kb_show(pw_buf, sizeof pw_buf);
            toast("Demasiados intentos: usa la contraseña");
        }
    }
}

static bool lock_trabajando(void);
static void draw_lock(void) {
    if (lock_pin) { draw_lock_pin(); return; }
    char hm[16], date[64]; time_strs(hm, sizeof hm, date, sizeof date);
    bool typing = kb_open;
    if (T.metro && !typing) {
        float y0 = H - 356;
        text(F_SANS, 128, hm, 26, y0, T.text, 0);
        text(F_SANS, 30, date, 34, y0 + 166, T.text, 0);
        pthread_mutex_lock(&st.m); int ba = st.batt, na = st.nfeed; pthread_mutex_unlock(&st.m);
        char li[80]; snprintf(li, sizeof li, na ? "%d %% · %d aviso%s" : "%d %%", ba, na, na == 1 ? "" : "s");
        text(F_SANS, 22, li, 34, y0 + 214, T.sub, 0);
        if (lock_trabajando()) text(F_SANS, 20, "Un agente está trabajando", 34, y0 + 250, T.accent, 0);
        text(F_SANS, 20, "Toca para desbloquear", 34, H - 58, T.sub, 0);
        if (btn(ID(), 0, 0, W, H)) {
            if (pin_usable()) { pin_in[0] = 0; lock_pin = true; kb_touch = false; }
            else { pw_buf[0] = 0; kb_show(pw_buf, sizeof pw_buf); auth_state = 0; }
        }
        return;
    }
    float e = typing ? ease_out(clampf((now_s() - kb_open_t) / .22f, 0, 1)) : 0;
    float cy = 210 - e * 110;
    text(F_SANS, 128, hm, W / 2.f, cy, T.text, 1);
    text(F_SANS, 26, date, W / 2.f, cy + 158, T.sub, 1);
    if (!typing) {
        text(F_SANS, 22, "Fósil", W / 2.f, 70, alpha(T.accent, .95f), 1);
        { pthread_mutex_lock(&st.m); int ba = st.batt, na = st.nfeed; pthread_mutex_unlock(&st.m);
          char li[80]; snprintf(li, sizeof li, na ? "%d %% · %d aviso%s" : "%d %%", ba, na, na == 1 ? "" : "s");
          text(F_SANS, 22, li, W / 2.f, cy + 204, T.text, 1);
          if (lock_trabajando()) text(F_SANS, 20, "Un agente está trabajando", W / 2.f, cy + 242, T.accent, 1); }
        float pulse = .55f + .35f * sinf(now_s() * 2.2f);
        unsigned id = ID();
        if (btn(id, 0, 0, W, H)) {
            if (pin_usable()) { pin_in[0] = 0; lock_pin = true; kb_touch = false; }
            else { pw_buf[0] = 0; kb_show(pw_buf, sizeof pw_buf); auth_state = 0; }
        }
        rr(W / 2.f - 34, H - 118, 68, 5, 2.5f, alpha(T.text, pulse));
        text(F_SANS, 21, "Toca para desbloquear", W / 2.f, H - 96, alpha(T.sub, .9f), 1);
    } else {
        float shake = 0; double ds = now_s() - shake_start;
        if (ds < .4) shake = sinf(ds * 46) * 14 * (1 - ds / .4f);
        float fy = cy + 215, fw = W - 110, fx = 55 + shake;
        rgrad(fx, fy, fw, 68, 20, alpha(T.card, 2.f), T.card);
        ring(fx, fy, fw, 68, 20, 1.4f, auth_state == 4 ? rgba(1, .4f, .4f, .9f) : T.line);
        int n = 0; for (char *p = pw_buf; *p; p++) if (((unsigned char)*p & 0xC0) != 0x80) n++;
        if (n == 0) text(F_SANS, 22, auth_state == 4 ? "Contraseña incorrecta" : "Contraseña", fx + fw / 2, fy + 21,
                         auth_state == 4 ? rgba(1, .55f, .55f, 1) : T.sub, 1);
        else {
            float total = fmin(n, 16) * 22, x = fx + fw / 2 - total / 2 + 8;
            for (int i = 0; i < n && i < 16; i++) rr(x + i * 22 - 7, fy + 27, 14, 14, 7, T.text);
        }
        if (auth_state == 1) text(F_SANS, 20, "Comprobando…", W / 2.f, fy + 80, T.accent, 1);
        if (keyboard("Entrar") && auth_state != 1 && *pw_buf) auth_start();
    }
}
static void chip(float x, float y, float w, const char *title, const char *value, C vc) {
    card(x, y, w, 88, 20);
    text(F_SANS, 16, title, x + 18, y + 14, T.sub, 0);
    text_fit(F_SANS, 26, value, x + 18, y + 40, w - 30, vc);
}
// Consumo de las IAs: cada línea es nombre|detalle|porcentaje más alto (lo escribe fosil-consumo).
static void draw_consumo(float y) {
    char ia[2][120];
    pthread_mutex_lock(&st.m); memcpy(ia, st.ia, sizeof ia); pthread_mutex_unlock(&st.m);
    card(34, y, W - 68, 88, 20);
    for (int i = 0; i < 2; i++) {
        float ry = y + 9 + i * 37;
        char *d = strchr(ia[i], '|'), *pc = d ? strchr(d + 1, '|') : NULL;
        if (!d) { text(F_SANS, 18, i ? "Codex" : "Claude", 54, ry, T.sub, 0); text(F_SANS, 17, "sin datos", W - 54, ry + 1, T.sub, 2); continue; }
        *d++ = 0; if (pc) *pc++ = 0;
        int p = pc ? atoi(pc) : -1;
        C col = p >= 80 ? rgba(1, .5f, .4f, 1) : T.accent;
        text(F_SANS, 18, ia[i], 54, ry, T.text, 0);
        text_fit(F_SANS, 17, d, 130, ry + 1, W - 184, p >= 80 ? col : T.sub);
        rr(54, ry + 26, W - 108, 4, 2, alpha(T.sub, .25f));
        if (p > 0) rr(54, ry + 26, (W - 108) * (p > 100 ? 100 : p) / 100.f, 4, 2, col);
    }
}
static void widget_value(int w, char *b, size_t n, const char **title, C *col) {
    pthread_mutex_lock(&st.m);
    int batt = st.batt, mu = st.mem_used, mt = st.mem_total, sig = st.signal; bool wifi = st.wifi, login = st.ts_login;
    char ssid[64], ts[48]; snprintf(ssid, sizeof ssid, "%s", st.ssid); snprintf(ts, sizeof ts, "%s", st.ts_ip);
    pthread_mutex_unlock(&st.m);
    if (demo) { strcpy(ssid, "Mi Wi-Fi"); strcpy(ts, "moto-e"); }
    *title = widget_names[w]; *col = T.text;
    switch (w) {
    case 0:
        snprintf(b, n, batt >= 0 ? "%d %%" : "—", batt);
        if (batt >= 0 && batt < 20) *col = rgba(1, .5f, .4f, 1);
        break;
    case 1: snprintf(b, n, "%d de %d MB", mu, mt); break;
    case 2:
        snprintf(b, n, "%s", wifi ? ssid : "Sin conexión");
        if (wifi) *title = sig >= 70 ? "Wi-Fi · fuerte" : sig >= 40 ? "Wi-Fi · media" : "Wi-Fi · débil";
        break;
    case 3: snprintf(b, n, "%s", *ts ? ts : login ? "Sin autorizar" : "—"); *col = *ts ? T.accent : T.sub; break;
    default:
        pthread_mutex_lock(&st.m);
        if (w == 4) snprintf(b, n, "%.0f %%", st.cpu * 100);
        if (w == 5) snprintf(b, n, "%d °C", st.temp);
        if (w == 6) snprintf(b, n, "%d / %d MB", st.disk_used, st.disk_total);
        if (w == 7) snprintf(b, n, "%d h %02d min", st.uptime / 3600, st.uptime / 60 % 60);
        pthread_mutex_unlock(&st.m);
    }
}
static void scroll_area(float top, float bottom, float content_h, Screen s);
// Inicio de Metro: rejilla de baldosas de 4 columnas que se desplaza en vertical.
static bool tile(unsigned id, float x, float y, float w, float h, C col, const char *label, float top, float bottom) {
    bool click = ui.y >= top && ui.y <= bottom && btn(id, x, y, w, h);
    float s = pressed(id) * 5;
    rr(x + s, y + s, w - 2 * s, h - 2 * s, 0, col);
    if (label) text(F_SANS, 17, label, x + 10, y + h - 30, rgba(1, 1, 1, 1), 0);
    return click;
}
static bool tile_long(unsigned id) {
    if (!ui.enabled || ui.active != id || !ui.down || ui.dragged || now_s() - ui.down_at < .65) return false;
    ui.active = 0; ui.dragged = true; vibrate();
    return true;
}
static void tile_consumo(float x, float y, float w) {
    char ia[2][120];
    pthread_mutex_lock(&st.m); memcpy(ia, st.ia, sizeof ia); pthread_mutex_unlock(&st.m);
    for (int i = 0; i < 2; i++) {
        float ry = y + 8 + i * 56;
        char *d = strchr(ia[i], '|'), *pc = d ? strchr(d + 1, '|') : NULL;
        if (!d) { text(F_SANS, 20, i ? "Codex" : "Claude", x + 12, ry, rgba(1, 1, 1, 1), 0); text(F_SANS, 17, "sin datos", x + w - 12, ry + 3, rgba(1, 1, 1, .85f), 2); continue; }
        *d++ = 0; if (pc) *pc++ = 0;
        int p = pc ? atoi(pc) : -1;
        C col = p >= 80 ? rgba(.94f, .64f, .04f, 1) : rgba(1, 1, 1, 1);
        text(F_SANS, 20, ia[i], x + 12, ry, rgba(1, 1, 1, 1), 0);
        text_fit(F_SANS, 17, d, x + 88, ry + 3, w - 100, rgba(1, 1, 1, .85f));
        rr(x + 12, ry + 35, w - 24, 5, 0, rgba(1, 1, 1, .28f));
        if (p > 0) rr(x + 12, ry + 35, (w - 24) * (p > 100 ? 100 : p) / 100.f, 5, 0, col);
    }
}
// Baldosas vivas: cada pocos segundos gira una al azar y enseña su otra cara.
// Solo se dibuja mientras gira; entre giros la interfaz sigue parada y no gasta.
#define TILES 14
#define FLIP_DUR .6
static bool tile_back[TILES]; static int flip_i = -1, flip_last = -1; static double flip_t, flip_next;
static volatile bool vivas_on = true;
static bool flip_tick(double t) {
    if (flip_i >= 0) {
        if (ui.down) flip_t = t - FLIP_DUR;  // el dedo manda: el giro en curso termina ya
        if (t - flip_t < FLIP_DUR + .05) return screen == S_HOME;
        tile_back[flip_i] = !tile_back[flip_i]; flip_last = flip_i; flip_i = -1;
        flip_next = t + 3 + (rand() % 30) / 10.;
    }
    if (!vivas_on || !T.metro || screen != S_HOME || widget_edit >= 0 || ui.down || blanked || t < flip_next || t - trans_start < TRANS || t - last_touch < 2) return false;
    pthread_mutex_lock(&st.m); float carga = st.cpu; pthread_mutex_unlock(&st.m);
    if (carga > .6f) return false;  // móvil ocupado: las baldosas esperan
    int backs = 0; for (int i = 0; i < TILES; i++) backs += tile_back[i];
    for (int tries = 0; tries < 40; tries++) {  // nunca más de 4 dadas la vuelta: los iconos siguen ahí
        int i = rand() % TILES;
        if (i != flip_last && (backs < 4 || tile_back[i])) { flip_i = i; break; }
    }
    if (flip_i < 0) return false;
    flip_t = t;
    return true;
}
static bool flip_begin(int i, float x, float y, float w, float h) {
    if (i != flip_i || TURN != 0) return tile_back[i];
    float k = clampf((now_s() - flip_t) / FLIP_DUR, 0, 1);
    FLX = x + w / 2; FLY = y + h / 2;
    FLA = k < .5f ? k * k * 4 * 1.5708f : -(1 - ease_out((k - .5f) * 2)) * 1.5708f;
    return k < .5f ? tile_back[i] : !tile_back[i];
}
static void flip_end(void) { FLA = FLX = FLY = 0; }
// Cara trasera de las baldosas pequeñas: rótulo y dato.
static void tile_dato(float x, float y, float w, const char *cap, const char *big) {
    text(F_SANS, 15, cap, x + 10, y + 10, rgba(1, 1, 1, .85f), 0);
    text_fit(F_SANS, 26, big, x + 10, y + 34, w - 20, rgba(1, 1, 1, 1));
}
static void tile_spark(float x, float y, float w, float h, const float *hist) {
    int n = 24; float bw = w / n;
    for (int i = 0; i < n; i++) {
        float v = clampf(hist[HIST - n + i], .03f, 1) * h;
        rr(x + i * bw, y + h - v, bw - 2, v, 0, rgba(1, 1, 1, .9f));
    }
}
static void draw_start(const char *hm, const char *date, const char *tiempo) {
    const float m = 14, g = 8, u = (W - 2 * m - 3 * g) / 4, wd = 2 * u + g, top = 40, bottom = H - 64;
    C white = rgba(1, 1, 1, 1), soft = rgba(1, 1, 1, .85f);
    char b[80]; const char *title; C col;
    pthread_mutex_lock(&st.m);
    int batt = st.batt, ma = st.batt_ma, mi = st.batt_min, up = st.uptime, mu = st.mem_used, mt = st.mem_total, du = st.disk_used, dt = st.disk_total, sig = st.signal, temp = st.temp, nfotos = st.nfotos, napps = st.napps;
    bool wifi = st.wifi; float cpu = st.cpu, hist[HIST]; memcpy(hist, st.hist, sizeof hist);
    char ssid[64], ip[48], ts[48], kernel[64], chat_t[64], chat_h[16];
    snprintf(ssid, sizeof ssid, "%s", st.ssid); snprintf(ip, sizeof ip, "%s", st.ip); snprintf(ts, sizeof ts, "%s", st.ts_ip); snprintf(kernel, sizeof kernel, "%s", st.kernel);
    snprintf(chat_t, sizeof chat_t, "%s", st.chat_titulo); snprintf(chat_h, sizeof chat_h, "%s", st.chat_hora);
    char upd_disp[24]; snprintf(upd_disp, sizeof upd_disp, "%s", st.upd_disp); int upd_linux = st.upd_linux;
    pthread_mutex_unlock(&st.m);
    if (demo) { strcpy(ssid, "Mi Wi-Fi"); ip[0] = 0; strcpy(ts, "moto-e"); }
    char quedan[32], enc[24], libres[24];
    if (ma > 15) snprintf(quedan, sizeof quedan, "+%d mA", ma);
    else if (mi >= 0) snprintf(quedan, sizeof quedan, "%d h %02d", mi / 60, mi % 60);
    else snprintf(quedan, sizeof quedan, "Estable");
    snprintf(enc, sizeof enc, "%d h %02d", up / 3600, up / 60 % 60);
    snprintf(libres, sizeof libres, "%.1f GB", (dt - du) / 1024.f);

    // 2 x 2, 2 filas de apps, 2 de widgets y 1 de consumo
    scroll_area(top, bottom, wd + g + (5 + (*upd_disp ? 1 : 0)) * (u + g) + 8, S_HOME);
    float y0 = top - scroll_y[S_HOME], y = y0;
    bool edit = false;
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, (int)(H - bottom), W, (int)(bottom - top));

    {
        bool back = flip_begin(0, m, y, wd, wd);
        tile(2000, m, y, wd, wd, T.accent, NULL, top, bottom);
        if (!back) {
            text(F_SANS, 84, hm, m + 12, y + 6, white, 0);
            if (*tiempo) text(F_SANS, 30, tiempo, m + 14, y + 118, soft, 0);
            const char *coma = strchr(date, ',');
            if (coma) {
                char dia[16]; snprintf(dia, sizeof dia, "%.*s", (int)(coma - date), date);
                text(F_SANS, 24, dia, m + 14, y + wd - 74, white, 0);
                text_fit(F_SANS, 20, coma + 2, m + 14, y + wd - 40, wd - 28, white);
            }
        } else {  // calendario
            time_t now = time(NULL); struct tm lt; localtime_r(&now, &lt);
            char n[8]; snprintf(n, sizeof n, "%d", lt.tm_mday);
            text(F_SANS, 120, n, m + 12, y + 4, white, 0);
            text(F_SANS, 30, DIAS[lt.tm_wday], m + 14, y + wd - 92, white, 0);
            text(F_SANS, 22, MESES[lt.tm_mon], m + 14, y + wd - 46, soft, 0);
            if (*tiempo) text(F_SANS, 30, tiempo, m + wd - 14, y + 16, white, 2);
        }
        flip_end();
    }
    float cx = m + wd + g;
    {
        bool trabajando = lock_trabajando();
        bool back = flip_begin(1, cx, y, wd, wd);
        if (tile(2001, cx, y, wd, wd, T.accent, trabajando ? "Chat · trabajando…" : "Chat", top, bottom)) go(S_CHAT, 1);
        if (!back) icon(I_AGENT, cx + wd / 2, y + wd / 2 - 6, 56, white);
        else if (*chat_t) {
            text(F_SANS, 17, chat_h, cx + 14, y + 16, soft, 0);
            text_wrap2(F_SANS, 26, chat_t, cx + 14, y + 52, 36, wd - 28, white);
        } else text(F_SANS, 26, "Sin charlas", cx + 14, y + 16, white, 0);
        flip_end();
    }
    y += wd + g;
    if (*upd_disp) {
        if (tile(2009, m, y, W - 2 * m, u, T.accent, "Actualizar", top, bottom)) go(S_UPDATE, 1);
        rr(m + 32, y + 22, 4, 36, 0, white); rr(m + 22, y + 62, 24, 4, 0, white);   // flecha de descarga
        snprintf(b, sizeof b, "v%s disponible", upd_disp); text(F_SANS, 30, b, m + 70, y + 14, white, 0);
        if (upd_linux > 0) snprintf(b, sizeof b, "Fósil y %d paquete%s de Linux", upd_linux, upd_linux == 1 ? "" : "s");
        else snprintf(b, sizeof b, "Fósil");
        text_fit(F_SANS, 17, b, m + 72, y + 56, W - 2 * m - 90, soft);
        y += u + g;
    }

    struct { const char *name; int ic; Screen s; } apps[] = {
        {"Terminal", I_TERM, S_NCOUNT}, {"Creaciones", I_APPS, S_APPS}, {"Ficheros", I_FILES, S_FILES}, {"Tareas", I_TASK, S_TAREAS},
        {"Sistema", I_SYS, S_SYSTEM}, {"Red", I_NET, S_NET}, {"Ajustes", I_SET, S_SETTINGS}, {"Energía", I_POWER, S_POWER},
    };
    for (int i = 0; i < 8; i++) {
        float x = m + (i % 4) * (u + g), ty = y + (i / 4) * (u + g);
        bool back = flip_begin(2 + i, x, ty, u, u);
        bool click = tile(2010 + i, x, ty, u, u, T.accent, apps[i].name, top, bottom);
        if (!back) icon(apps[i].ic, x + u / 2, ty + u / 2 - 12, 24, white);
        else switch (i) {
        case 0: tile_dato(x, ty, u, "encendido", enc); break;
        case 1: snprintf(b, sizeof b, "%d", napps); tile_dato(x, ty, u, napps == 1 ? "app" : "apps", b); break;
        case 2: tile_dato(x, ty, u, "libres", libres); break;
        case 3: snprintf(b, sizeof b, "%d", nfotos); tile_dato(x, ty, u, nfotos == 1 ? "punto" : "puntos", b); break;
        case 4: tile_spark(x + 10, ty + 14, u - 20, 58, hist); break;
        case 5: if (wifi) {
                    for (int s = 0; s < 4; s++) { float bh = 14 + s * 14; rr(x + 28 + s * 18, ty + 72 - bh, 12, bh, 0, sig > s * 25 ? white : rgba(1, 1, 1, .28f)); }
                } else tile_dato(x, ty, u, "Wi-Fi", "—");
                break;
        case 6: snprintf(b, sizeof b, "%d %%", (int)(brightness * 100)); tile_dato(x, ty, u, "brillo", b); break;
        default: tile_dato(x, ty, u, ma > 15 ? "cargando" : "quedan", quedan); break;
        }
        flip_end();
        if (click) {
            if (apps[i].s == S_NCOUNT) _exit(EXIT_TERMINAL);
            go(apps[i].s, 1);
        }
    }
    y += 2 * (u + g);

    for (int row = 0; row <= 2; row++) {
        if (row == (consumo_pos > 2 ? 2 : consumo_pos)) {
            tile(9320, m, y, W - 2 * m, u, T.accent, NULL, top, bottom);
            tile_consumo(m, y, W - 2 * m);
            if (tile_long(9320)) { widget_edit = 4; edit = true; }
            y += u + g;
        }
        if (row == 2) break;
        for (int c = 0; c < 2; c++) {
            int i = row * 2 + c; float x = m + c * (wd + g);
            widget_value(home_widgets[i], b, sizeof b, &title, &col);
            bool aviso = home_widgets[i] == 0 && batt >= 0 && batt < 20;
            bool back = flip_begin(10 + i, x, y, wd, u);
            tile(9100 + i, x, y, wd, u, aviso ? rgba(.9f, .08f, 0, 1) : T.accent, title, top, bottom);
            float nivel = home_widgets[i] == 0 ? batt / 100.f : home_widgets[i] == 1 && mt ? mu / (float)mt : home_widgets[i] == 4 ? cpu : home_widgets[i] == 6 && dt ? du / (float)dt : 0;
            if (!back) {  // el nivel se ve como un relleno más claro
                if (nivel > 0) rr(x, y, wd * clampf(nivel, 0, 1), u, 0, rgba(1, 1, 1, .12f));
                text_fit(F_SANS, 30, b, x + 10, y + 26, wd - 20, white);
            } else {
                char d[80]; d[0] = 0;
                switch (home_widgets[i]) {
                case 0: if (ma > 15) snprintf(d, sizeof d, "Cargando · %d mA", ma);
                        else if (ma < -15) snprintf(d, sizeof d, "%d mA · %s", -ma, quedan);
                        else snprintf(d, sizeof d, "Estable");
                        break;
                case 1: snprintf(d, sizeof d, "%d %% en uso", mt ? mu * 100 / mt : 0); break;
                case 2: snprintf(d, sizeof d, "%s", *ip ? ip : "Sin IP"); break;
                case 3: snprintf(d, sizeof d, "%s", *ts ? "Conectado" : "Sin conectar"); break;
                case 4: tile_spark(x + 10, y + 14, wd - 20, 64, hist); break;
                case 5: snprintf(d, sizeof d, "%s", temp >= 60 ? "Caliente" : temp >= 45 ? "Templado" : "Normal"); break;
                case 6: snprintf(d, sizeof d, "%s libres", libres); break;
                default: snprintf(d, sizeof d, "%s", kernel); break;
                }
                if (*d) text_fit(F_SANS, 24, d, x + 10, y + 30, wd - 20, white);
            }
            flip_end();
            if (tile_long(9100 + i)) { widget_edit = i; edit = true; }
        }
        y += u + g;
    }
    glDisable(GL_SCISSOR_TEST);
    if (edit) return;

    // Franja de estado arriba y barra de avisos fija abajo
    if (*ssid && wifi) text_fit(F_SANS, 18, ssid, 16, 9, 240, T.sub);
    snprintf(b, sizeof b, batt >= 0 ? "%d %%" : "—", batt);
    text(F_SANS, 18, b, W - 16, 9, T.sub, 2);
    { unsigned id = ID();
      pthread_mutex_lock(&st.m);
      char last[240]; snprintf(last, sizeof last, "%s", st.nfeed ? st.feed[0] : "");
      pthread_mutex_unlock(&st.m);
      char *msg = strchr(last, '|'); if (msg) msg = strchr(msg + 1, '|');
      if (btn(id, 0, bottom, W, H - bottom)) go(S_AGENTS, 1);
      rr(0, bottom, W, H - bottom, 0, rgba(.12f, .12f, .12f, 1));
      if (pressed(id)) rr(0, bottom, W, H - bottom, 0, alpha(T.accent, .4f));
      rr(20, bottom + 27, 10, 10, 5, *last ? T.accent : alpha(T.sub, .5f));
      text_fit(F_SANS, 20, msg ? msg + 1 : "Avisos: todo tranquilo", 42, bottom + 19, W - 62, *last ? T.text : T.sub);
    }
}
static void upd_vigilar(void);
static void draw_home(void) {
    if (widget_edit < 0) upd_vigilar();
    char hm[16], date[64], b[80]; time_strs(hm, sizeof hm, date, sizeof date);
    pthread_mutex_lock(&st.m);
    char tiempo[24]; snprintf(tiempo, sizeof tiempo, "%s", st.tiempo);
    pthread_mutex_unlock(&st.m);
    if (T.metro && widget_edit < 0) { draw_start(hm, date, tiempo); return; }
    text(F_SANS, 104, hm, 34, 52, T.text, 0);
    text(F_SANS, 24, date, 38, 182, T.sub, 0);
    if (*tiempo) text(F_SANS, 24, tiempo, W - 34, 182, T.text, 2);
    if (widget_edit == 4) {
        const char *positions[] = {"Encima de los widgets", "Entre los widgets", "Debajo de los widgets", "Al final de la home"};
        text(F_SANS, 30, "Mover consumo de IAs", 34, 232, T.text, 0);
        for (int i = 0; i < 4; i++) {
            float y = 296 + i * 80;
            bool click = btn(9300 + i, 34, y, W - 68, 66);
            card(34, y, W - 68, 66, 20);
            text(F_SANS, 23, positions[i], 54, y + 20, consumo_pos == i ? T.accent : T.text, 0);
            if (click) { consumo_pos = i; config_save(); widget_edit = -1; return; }
        }
        if (btn(9310, 34, 650, W - 68, 70)) widget_edit = -1;
        text(F_SANS, 23, "Cancelar", W / 2.f, 673, T.sub, 1);
        return;
    }
    if (widget_edit >= 0) {
        text(F_SANS, 30, "Cambiar widget", 34, 232, T.text, 0);
        for (int i = 0; i < 8; i++) {
            float y = 286 + i * 65;
            unsigned id = 9200 + i;
            bool click = btn(id, 34, y, W - 68, 56);
            card(34, y, W - 68, 56, 18);
            text(F_SANS, 23, widget_names[i], 54, y + 13,
                 home_widgets[widget_edit] == i ? T.accent : T.text, 0);
            if (click) {
                int slot = widget_edit, old = home_widgets[slot];
                home_widgets[slot] = i;
                for (int j = 0; j < 4; j++)
                    if (j != slot && home_widgets[j] == i) home_widgets[j] = old;
                config_save(); widget_edit = -1; return;
            }
        }
        if (btn(9210, 34, 824, W - 68, 64)) widget_edit = -1;
        text(F_SANS, 23, "Cancelar", W / 2.f, 844, T.sub, 1);
        return;
    }
    float cw = (W - 68 - 12) / 2.f;
    for (int i = 0; i < 4; i++) {
        float x = 34 + (i % 2) * (cw + 12), y = 232 + (i / 2) * 100 + (consumo_pos <= i / 2 ? 100 : 0);
        const char *title; C col;
        widget_value(home_widgets[i], b, sizeof b, &title, &col);
        chip(x, y, cw, title, b, col);
        unsigned id = 9100 + i;
        btn(id, x, y, cw, 88);
        if (ui.enabled && ui.active == id && ui.down && !ui.dragged && now_s() - ui.down_at >= .65) {
            widget_edit = i; ui.active = 0; ui.dragged = true; vibrate(); return;
        }
        if (pressed(id)) ring(x, y, cw, 88, 20, 2, T.accent);
    }

    struct { const char *name; int ic; Screen s; } apps[] = {
        {"Chat", I_AGENT, S_CHAT}, {"Terminal", I_TERM, S_NCOUNT}, {"Creaciones", I_APPS, S_APPS},
        {"Ficheros", I_FILES, S_FILES}, {"Tareas", I_TASK, S_TAREAS}, {"Sistema", I_SYS, S_SYSTEM},
        {"Red", I_NET, S_NET}, {"Ajustes", I_SET, S_SETTINGS}, {"Energía", I_POWER, S_POWER},
    };
    float gw = (W - 68 - 24) / 3.f, gh = 104, gy = 436 + (consumo_pos < 3 ? 100 : 0);
    for (int i = 0; i < 9; i++) {
        float x = 34 + (i % 3) * (gw + 12), y = gy + (i / 3) * (gh + 10);
        unsigned id = 2000 + i;
        bool click = btn(id, x, y, gw, gh);
        float p = pressed(id);
        float sh = p * 4;
        rgrad(x + sh, y + sh, gw - 2 * sh, gh - 2 * sh, 26, alpha(T.card, p ? 3.f : 1.8f), T.card);
        ring(x + sh, y + sh, gw - 2 * sh, gh - 2 * sh, 26, 1.2f, T.line);
        icon(apps[i].ic, x + gw / 2, y + gh * .38f, 20, T.accent);
        text(F_SANS, 18, apps[i].name, x + gw / 2, y + gh - 34, T.text, 1);
        if (click) {
            if (apps[i].s == S_NCOUNT) _exit(EXIT_TERMINAL);
            go(apps[i].s, 1);
        }
    }
    { unsigned id = ID(); float y = gy + 3 * (gh + 10) + 6;
      pthread_mutex_lock(&st.m);
      char last[240]; snprintf(last, sizeof last, "%s", st.nfeed ? st.feed[0] : "");
      pthread_mutex_unlock(&st.m);
      char *m = strchr(last, '|'); if (m) m = strchr(m + 1, '|');
      char disp[24]; pthread_mutex_lock(&st.m); snprintf(disp, sizeof disp, "%s", st.upd_disp); pthread_mutex_unlock(&st.m);
      float ax = 34, aw = W - 68;
      if (*disp) {  // aviso fijo de versión nueva: no se descarta mientras siga pendiente
          unsigned uid = ID(); float uw = (W - 68 - 10) * .62f; char t[48];
          if (btn(uid, 34, y, uw, 60)) go(S_UPDATE, 1);
          rgrad(34, y, uw, 60, 30, alpha(T.accent, pressed(uid) ? .36f : .24f), alpha(T.accent, .10f));
          ring(34, y, uw, 60, 30, 1.5f, T.accent);
          rr(56, y + 18, 3, 16, 1, T.accent); rr(50, y + 38, 15, 3, 1, T.accent);   // flecha de descarga
          snprintf(t, sizeof t, "v%s disponible", disp);
          text_fit(F_SANS, 19, t, 78, y + 8, uw - 60, T.text);
          text(F_SANS, 16, "Actualizar", 78, y + 32, T.accent, 0);
          ax = 34 + uw + 10; aw = W - 34 - ax;
      }
      if (btn(id, ax, y, aw, 60)) go(S_AGENTS, 1);
      rr(ax, y, aw, 60, 30, alpha(T.card, pressed(id) ? 3.f : 1.4f));
      ring(ax, y, aw, 60, 30, 1.2f, T.line);
      rr(ax + 22, y + 25, 10, 10, 5, *last ? T.accent : alpha(T.sub, .5f));
      text_fit(F_SANS, 20, m ? m + 1 : *disp ? "Avisos" : "Avisos: todo tranquilo", ax + 44, y + 18, aw - 66, *last ? T.text : T.sub);
    }
    float consumo_y = consumo_pos < 3 ? 232 + consumo_pos * 100 : 856;
    draw_consumo(consumo_y);
    btn(9320, 34, consumo_y, W - 68, 88);
    if (pressed(9320)) ring(34, consumo_y, W - 68, 88, 20, 2, T.accent);
    if (ui.enabled && ui.active == 9320 && ui.down && !ui.dragged && now_s() - ui.down_at >= .65) {
        widget_edit = 4; ui.active = 0; ui.dragged = true; vibrate();
    }
}
static Screen header_to = S_HOME;
static float header(const char *title) {
    unsigned id = ID();
    Screen dest = header_to; header_to = S_HOME;
    if (btn(id, 0, 28, 170, 90)) go(dest, -1);
    float p = pressed(id);
    if (T.metro) {  // título en minúsculas y ligero, con el nombre de la app encima
        char low[64]; snprintf(low, sizeof low, "%s", title);
        for (char *c = low; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;
        if (p) rr(26, 48, 54, 54, 27, T.text);
        ring(26, 48, 54, 54, 27, 2.4f, T.text);
        text(F_SANS, 38, "‹", 52, 50, p ? rgba(0, 0, 0, 1) : T.text, 1);
        text(F_SANS, 16, "FÓSIL", 98, 28, T.text, 0);
        text_fit(F_SANS, 52, low, 96, 46, W - 112, T.text);
        return 132;
    }
    rr(26, 48, 54, 54, 27, alpha(T.card, p ? 3.f : 1.6f));
    ring(26, 48, 54, 54, 27, 1.2f, T.line);
    text(F_SANS, 38, "‹", 52, 50, T.text, 1);
    text(F_SANS, 38, title, 98, 52, T.text, 0);
    return 132;
}
static void stat_row(float y, const char *k, const char *v) {
    text(F_SANS, 22, k, 56, y, T.sub, 0);
    text(F_SANS, 22, v, W - 56, y, T.text, 2);
}
static void draw_system(void) {
    pthread_mutex_lock(&st.m);
    float hist[HIST]; memcpy(hist, st.hist, sizeof hist);
    int ma = st.batt_ma, mi = st.batt_min;
    float cpu = st.cpu; int mu = st.mem_used, mt = st.mem_total, temp = st.temp, up = st.uptime, batt = st.batt, du = st.disk_used, dt = st.disk_total;
    char kernel[64]; snprintf(kernel, sizeof kernel, "%s", st.kernel);
    pthread_mutex_unlock(&st.m);
    float y = header("Sistema");
    card(34, y, W - 68, 218, 26);
    char b[80]; snprintf(b, sizeof b, "%.0f %%", cpu * 100);
    text(F_SANS, 20, "Procesador · 4 núcleos", 56, y + 20, T.sub, 0);
    text(F_SANS, 44, b, 56, y + 50, T.text, 0);
    float gx = 56, gw = W - 112, gy = y + 200, gh = 90, bw = gw / HIST;
    for (int i = 0; i < HIST; i++) {
        float v = clampf(hist[i], .015f, 1) * gh;
        rgrad(gx + i * bw, gy - v, bw - 2, v, 2, T.accent, alpha(T.accent, .25f));
    }
    y += 232;
    card(34, y, W - 68, 96, 26);
    snprintf(b, sizeof b, "%d de %d MB", mu, mt);
    text(F_SANS, 20, "Memoria", 56, y + 18, T.sub, 0);
    text(F_SANS, 20, b, W - 56, y + 18, T.text, 2);
    rr(56, y + 58, W - 112, 14, 7, rgba(1, 1, 1, .10f));
    if (mt) rr(56, y + 58, (W - 112) * mu / (float)mt, 14, 7, T.accent);
    y += 110;
    card(34, y, W - 68, 306, 26);
    float ry = y + 22;
    snprintf(b, sizeof b, "%d %%", batt); stat_row(ry, "Batería", b); ry += 46;
    if (ma > 15) snprintf(b, sizeof b, "Cargando · %d mA", ma);
    else if (ma < -15) snprintf(b, sizeof b, "%d mA · quedan %d h %02d", -ma, mi / 60, mi % 60);
    else snprintf(b, sizeof b, "Estable");
    stat_row(ry, "Estado", b); ry += 46;
    snprintf(b, sizeof b, "%d °C", temp); stat_row(ry, "Temperatura", b); ry += 46;
    snprintf(b, sizeof b, "%d h %02d min", up / 3600, up / 60 % 60); stat_row(ry, "Encendido", b); ry += 46;
    snprintf(b, sizeof b, "%.1f de %.1f GB", du / 1024.f, dt / 1024.f); stat_row(ry, "Disco", b); ry += 46;
    stat_row(ry, "Kernel", kernel);
}
static void scroll_area(float top, float bottom, float content_h, Screen s) {
    scroll_max = content_h > bottom - top ? content_h - (bottom - top) : 0;
    if (ui.enabled && ui.press && ui.y >= top && ui.y <= bottom) { scrolling = true; scroll_v = 0; }
    (void)s;
}
static void draw_net(void) {
    pthread_mutex_lock(&st.m);
    Net n[MAX_NETS]; int cnt = st.nnets; memcpy(n, st.nets, sizeof(Net) * cnt); bool scanning = st.scanning;
    char ip[48]; snprintf(ip, sizeof ip, "%s", st.ip);
    pthread_mutex_unlock(&st.m);
    if (demo) {
        static const Net dn[] = {{"Mi Wi-Fi", 71, true, true, true}, {"Zona del móvil", 58, true, false, true}, {"Invitados", 44, true, false, false}};
        cnt = 3; memcpy(n, dn, sizeof dn); ip[0] = 0;
    }
    float top = header("Red");
    if (net_prompt) {
        text(F_SANS, 22, "Contraseña de", 56, top + 6, T.sub, 0);
        text_fit(F_SANS, 30, net_target, 56, top + 36, W - 112, T.text);
        card(34, top + 90, W - 68, 68, 20);
        char shown[140]; snprintf(shown, sizeof shown, "%s", *net_pw ? net_pw : "Escribe aquí");
        text_fit(F_MONO, 22, shown, 54, top + 110, W - 108, *net_pw ? T.text : T.sub);
        unsigned id = ID();
        if (btn(id, 34, top + 176, 150, 54)) { net_prompt = false; kb_open = false; explicit_bzero(net_pw, sizeof net_pw); }
        rr(34, top + 176, 150, 54, 16, alpha(T.card, 1.6f));
        text(F_SANS, 21, "Cancelar", 109, top + 190, T.text, 1);
        if (keyboard("Conectar")) {
            connect_to(net_target, net_pw, false);
            explicit_bzero(net_pw, sizeof net_pw); net_prompt = false; kb_open = false;
        }
        return;
    }
    { unsigned id = ID();
      float bx = W - 34 - 54;
      if (btn(id, bx - 8, 40, 70, 70)) start_scan();
      rr(bx, 48, 54, 54, 27, alpha(T.card, pressed(id) ? 3.f : 1.6f));
      ring(bx, 48, 54, 54, 27, 1.2f, T.line);
      if (scanning) {
          float a = now_s() * 5;
          for (int i = 0; i < 3; i++) rr(bx + 27 + cosf(a + i * 2.1f) * 12 - 3, 75 + sinf(a + i * 2.1f) * 12 - 3, 6, 6, 3, T.accent);
      } else text(F_SANS, 28, "↻", bx + 27, 58, T.text, 1); }
    if (*ip) { char b[64]; snprintf(b, sizeof b, "IP %s", ip); text(F_MONO, 19, b, 40, top - 8, T.sub, 0); }
    float bottom = H - 30;
    float rowh = 88, content = cnt * rowh + 40;
    float start = top + 30;
    scroll_area(start, bottom, content, S_NET);
    float sy = scroll_y[S_NET];
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, (int)(H - bottom), W, (int)(bottom - start));
    if (cnt == 0) text(F_SANS, 22, scanning ? "Buscando redes…" : "No hay redes a la vista", W / 2.f, start + 40, T.sub, 1);
    for (int i = 0; i < cnt; i++) {
        float y = start + i * rowh - sy;
        if (y < start - rowh || y > bottom) continue;
        unsigned id = 3000 + i;
        bool click = ui.y >= start && ui.y <= bottom && btn(id, 34, y, W - 68, rowh - 10);
        float p = pressed(id);
        rgrad(34, y, W - 68, rowh - 10, 20, alpha(T.card, n[i].active ? 3.f : p ? 3.f : 1.5f), T.card);
        ring(34, y, W - 68, rowh - 10, 20, n[i].active ? 1.8f : 1.2f, n[i].active ? T.accent : T.line);
        text_fit(F_SANS, 24, n[i].ssid, 56, y + 12, W - 210, T.text);
        const char *sub = n[i].active ? "Conectada" : n[i].saved ? "Guardada" : n[i].secure ? "Protegida" : "Abierta";
        text(F_SANS, 17, sub, 56, y + 44, n[i].active ? T.accent : T.sub, 0);
        for (int b = 0; b < 4; b++) {
            float h = 8 + b * 7; bool on = n[i].signal > b * 25;
            rr(W - 104 + b * 11, y + 54 - h, 7, h, 2, on ? T.accent : rgba(1, 1, 1, .13f));
        }
        if (click && !net_busy && !n[i].active) {
            if (n[i].saved || !n[i].secure) connect_to(n[i].ssid, "", n[i].saved);
            else { snprintf(net_target, sizeof net_target, "%s", n[i].ssid); net_pw[0] = 0; net_prompt = true; kb_show(net_pw, sizeof net_pw); }
        }
    }
    glDisable(GL_SCISSOR_TEST);
}
static bool chat_send_fw(const char *m);
// Quita un aviso reescribiendo el fichero (mismo dueño y permisos).
static void feed_drop(const char *line) {
    FILE *f = fopen(STATE_DIR "/feed.log", "r"); if (!f) return;
    char *buf = malloc(64 * 1024); size_t n = 0; char l[400]; bool done = false; char (*all)[400] = malloc(400 * 220); int c = 0;
    while (fgets(l, sizeof l, f) && c < 220) { l[strcspn(l, "\n")] = 0; snprintf(all[c++], 400, "%s", l); }
    fclose(f);
    for (int i = c - 1; i >= 0 && !done; i--) if (!strncmp(all[i], line, 239)) { all[i][0] = 0; done = true; }
    for (int i = 0; i < c; i++) if (all[i][0]) n += snprintf(buf + n, 64 * 1024 - n, "%s\n", all[i]);
    if ((f = fopen(STATE_DIR "/feed.log", "w"))) { fwrite(buf, 1, n, f); fclose(f); }
    free(all); free(buf); read_feed();
}
static void draw_agents(void) {
    pthread_mutex_lock(&st.m);
    int cnt = st.nfeed; static char f[MAX_FEED][240]; memcpy(f, st.feed, sizeof f);
    char ts[48]; snprintf(ts, sizeof ts, "%s", st.ts_ip);
    pthread_mutex_unlock(&st.m);
    if (demo) strcpy(ts, "moto-e");
    float top = header("Avisos");
    if (cnt) { unsigned id = ID(); float bx = W - 34 - 110;
      if (btn(id, bx, 44, 110, 60)) { FILE *ff = fopen(STATE_DIR "/feed.log", "w"); if (ff) fclose(ff); read_feed(); }
      rr(bx, 48, 110, 52, 26, alpha(T.card, pressed(id) ? 3.f : 1.6f)); ring(bx, 48, 110, 52, 26, 1.2f, T.line);
      text(F_SANS, 18, "Borrar todo", bx + 55, 63, T.sub, 1); }
    text(F_MONO, 18, *ts ? ts : "Tailscale sin autorizar", 40, top - 8, *ts ? T.accent : T.sub, 0);
    float start = top + 30, bottom = H - 30, rowh = 112;
    scroll_area(start, bottom, cnt * rowh + 30, S_AGENTS);
    if (cnt == 0) {
        card(34, start, W - 68, 240, 26);
        icon(I_AGENT, W / 2.f, start + 66, 34, T.accent);
        text(F_SANS, 24, "Sin novedades", W / 2.f, start + 114, T.text, 1);
        text(F_SANS, 18, "Los agentes pueden avisar aquí con:", W / 2.f, start + 154, T.sub, 1);
        text(F_MONO, 19, "fosil-avisa \"mensaje\"", W / 2.f, start + 186, T.accent, 1);
        return;
    }
    float sy = scroll_y[S_AGENTS];
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, (int)(H - bottom), W, (int)(bottom - start));
    for (int i = 0; i < cnt; i++) {
        float y = start + i * rowh - sy;
        if (y < start - rowh || y > bottom) continue;
        char l[240]; snprintf(l, sizeof l, "%s", f[i]);
        char *a = l, *b = strchr(a, '|'), *c = NULL;
        if (b) { *b++ = 0; c = strchr(b, '|'); if (c) *c++ = 0; }
        char *conv = b ? strchr(b, '#') : NULL; if (conv) *conv++ = 0;
        unsigned id = 7400 + i;
        bool on_row = ui.enabled && inside(34, y, W - 68, rowh - 10);
        if (ui.press && on_row) ui.active = id;
        float dx = ui.active == id && ui.down && fabsf(ui.x - ui.sx) > fabsf(ui.y - ui.sy) ? ui.x - ui.sx : 0;
        if (dx > 0) dx = 0;
        float ox = OX; OX += dx;
        card(34, y, W - 68, rowh - 10, 20);
        text(F_MONO, 16, a, W - 100, y + 16, T.sub, 2);
        text_fit(F_SANS, 18, b ? b : "", 56, y + 14, W - 280, T.accent);
        text_wrap2(F_SANS, 20, c ? c : "", 56, y + 40, 26, W - 112, T.text);
        text(F_SANS, 26, "×", W - 66, y + 8, T.sub, 1);
        OX = ox;
        if (ui.release && ui.active == id) {
            bool cruz = ui.sx > W - 100 && ui.sy < y + 50 && !ui.dragged;
            if (cruz || ui.x - ui.sx < -140) { feed_drop(f[i]); ui.active = 0; }
            else if (!ui.dragged && conv && on_row) { char m[48]; snprintf(m, sizeof m, "!abrir %s", conv); chat_send_fw(m); go(S_CHAT, 1); }
        }
    }
    glDisable(GL_SCISSOR_TEST);
}
static pthread_mutex_t ch_m_ajustes = PTHREAD_MUTEX_INITIALIZER; static bool ajustes_total = true;
static void chat_toggle_permisos(void);
static void draw_settings(void) {
    float y = header("Ajustes");
    text(F_SANS, 21, "Aspecto", 42, y, T.sub, 0); y += 36;
    float cw = (W - 68 - 12 * (NTHEMES - 1)) / (float)NTHEMES;
    for (int i = 0; i < NTHEMES; i++) {
        float x = 34 + i * (cw + 12);
        unsigned id = 4000 + i;
        if (btn(id, x, y, cw, 176)) { theme_idx = i; config_save(); }
        const Theme *t = &THEMES[i];
        rgrad(x, y, cw, 176, 24, rgba(t->glow1[0] + t->bg[0], t->glow1[1] + t->bg[1], t->glow1[2] + t->bg[2], 1),
              rgba(t->glow2[0] + t->bg[0], t->glow2[1] + t->bg[1], t->glow2[2] + t->bg[2], 1));
        rr(x + cw / 2 - 19, y + 44, 38, 38, 19, t->accent);
        text(F_SANS, 20, t->name, x + cw / 2, y + 112, t->text, 1);
        ring(x, y, cw, 176, 24, i == theme_idx ? 3 : 1.2f, i == theme_idx ? t->accent : T.line);
    }
    y += 206;
    text(F_SANS, 21, *bl_path ? "Brillo" : "Atenuación", 42, y, T.sub, 0); y += 36;
    card(34, y, W - 68, 92, 24);
    float sx = 62, sw = W - 124, sy = y + 46;
    unsigned id = ID();
    if (ui.enabled && ui.press && inside(34, y, W - 68, 92)) ui.active = id;
    if (ui.active == id && ui.down) {
        brightness = clampf((ui.x - sx) / sw, .15f, 1); if (*bl_path) bl_set(brightness);
    }
    if (ui.active == id && ui.release) config_save();
    rr(sx, sy - 5, sw, 10, 5, rgba(1, 1, 1, .12f));
    rr(sx, sy - 5, sw * brightness, 10, 5, T.accent);
    rr(sx + sw * brightness - 15, sy - 15, 30, 30, 15, T.text);
    y += 124;
    text(F_SANS, 21, "Seguridad", 42, y, T.sub, 0); y += 36;
    { unsigned id = ID();
      if (btn(id, 34, y, W - 68, 84)) { pin_stage = 0; pw_buf[0] = 0; pin_in[0] = 0; auth_state = 0; go(S_PIN, 1); kb_show(pw_buf, sizeof pw_buf); }
      rgrad(34, y, W - 68, 84, 24, alpha(T.card, pressed(id) ? 3.f : 1.7f), T.card);
      ring(34, y, W - 68, 84, 24, 1.2f, T.line);
      text(F_SANS, 24, "PIN de desbloqueo", 56, y + 28, T.text, 0);
      text(F_SANS, 19, pin_len ? "Activado" : "Crear", W - 56, y + 31, pin_len ? T.accent : T.sub, 2); }
    y += 92;
    { unsigned id = ID();
      pthread_mutex_lock(&ch_m_ajustes); bool tot = ajustes_total; pthread_mutex_unlock(&ch_m_ajustes);
      if (btn(id, 34, y, W - 68, 84)) chat_toggle_permisos();
      rgrad(34, y, W - 68, 84, 24, alpha(T.card, pressed(id) ? 3.f : 1.7f), T.card);
      ring(34, y, W - 68, 84, 24, 1.2f, T.line);
      text(F_SANS, 24, "Agentes", 56, y + 28, T.text, 0);
      text(F_SANS, 19, tot ? "Acceso total" : "Limitado", W - 56, y + 31, tot ? rgba(1, .65f, .5f, 1) : T.accent, 2); }
    y += 100;
    text(F_SANS, 21, "Acerca de", 42, y, T.sub, 0); y += 34;
    { unsigned id = ID(); char v[48], d[48];
      pthread_mutex_lock(&st.m);
      snprintf(v, sizeof v, "Fósil v%s", st.upd_inst); snprintf(d, sizeof d, *st.upd_disp ? "v%s disponible" : "Al día", st.upd_disp);
      bool hay = *st.upd_disp; pthread_mutex_unlock(&st.m);
      if (btn(id, 34, y, W - 68, 92)) { go(S_UPDATE, 1); }
      float p = pressed(id);
      if (T.metro) { rr(34, y, W - 68, 92, 0, p ? alpha(T.accent, .4f) : T.card); rr(56, y + 20, 14, 52, 0, T.accent); }
      else { rgrad(34, y, W - 68, 92, 24, alpha(T.card, p ? 3.f : 1.7f), T.card); ring(34, y, W - 68, 92, 24, hay ? 1.5f : 1.2f, hay ? T.accent : T.line); }
      float tx = T.metro ? 88 : 56;
      text(F_SANS, 24, v, tx, y + 14, T.text, 0);
      text_fit(F_SANS, 17, p_desc, tx, y + 52, W - tx - 230, T.sub);
      text(F_SANS, 18, d, W - 56, y + (T.metro ? 32 : 20), hay ? T.accent : T.sub, 2);
      if (!T.metro) text(F_SANS, 16, "Ver ›", W - 56, y + 52, T.sub, 2); }
}
// ---------------------------------------------------------------- actualizaciones
typedef struct {
    char inst[24], disp[24], paso[16], texto[120], err[120], notas[600];
    int linux_n, up; long comprobado, estado_t; bool lock, reiniciar;
} Upd;
static void upd_copiar(Upd *u) {
    pthread_mutex_lock(&st.m);
    snprintf(u->inst, sizeof u->inst, "%s", st.upd_inst); snprintf(u->disp, sizeof u->disp, "%s", st.upd_disp);
    snprintf(u->paso, sizeof u->paso, "%s", st.upd_paso); snprintf(u->texto, sizeof u->texto, "%s", st.upd_texto);
    snprintf(u->err, sizeof u->err, "%s", st.upd_err); memcpy(u->notas, st.upd_notas, sizeof u->notas);
    u->linux_n = st.upd_linux; u->up = st.uptime; u->comprobado = st.upd_comprobado; u->estado_t = st.upd_estado_t;
    u->lock = st.upd_lock; u->reiniciar = st.upd_reiniciar;
    pthread_mutex_unlock(&st.m);
}
static long upd_visto;            // el «Terminado» ya enseñado (fecha del estado)
static double upd_pulsado = -100; // evita la doble pulsación hasta que aparece el bloqueo
static double upd_buscando = -100; static long upd_comp_antes; static bool upd_solo_linux;
// En marcha: hay bloqueo y el paso no es final. Si el proceso murió sin dejar estado, el bloqueo ya no está.
static bool upd_en_marcha(const Upd *u) {
    bool final = !strcmp(u->paso, "hecho") || !strcmp(u->paso, "error") || !*u->paso;
    return (u->lock && !final) || now_s() - upd_pulsado < 6;
}
// Un «hecho» nuevo de esta sesión del sistema (no de antes de reiniciar el móvil) que aún no se ha visto.
static bool upd_terminado_nuevo(const Upd *u) {
    long arranque = (long)time(NULL) - u->up;
    return !strcmp(u->paso, "hecho") && u->estado_t > upd_visto && u->estado_t > arranque && !u->lock;
}
static void *upd_hilo(void *a) {
    char c[160]; snprintf(c, sizeof c, "systemctl start --no-block %s", (const char *)a);
    int r = system(c); (void)r; return NULL;
}
static void upd_lanzar(const char *unit) {
    pthread_t t; if (!pthread_create(&t, NULL, upd_hilo, (void *)unit)) pthread_detach(t);
}
static bool upd_boton(unsigned id, float x, float y, float w, float h, const char *label, bool primario, bool activo) {
    bool click = btn(id, x, y, w, h) && activo;
    float p = activo ? pressed(id) : 0, a = activo ? 1 : .4f;
    if (T.metro) {
        if (primario) rr(x, y, w, h, 0, alpha(p ? T.text : T.accent, a));
        else { if (p) rr(x, y, w, h, 0, T.text); ring(x, y, w, h, 0, 2.4f, alpha(T.text, a)); }
        char low[64]; snprintf(low, sizeof low, "%s", label);
        for (char *c = low; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;
        text(F_SANS, primario ? 22 : 19, low, x + w / 2, y + h / 2 - (primario ? 15 : 13), p ? rgba(0, 0, 0, 1) : alpha(rgba(1, 1, 1, 1), a), 1);
    } else {
        if (primario) rgrad(x, y, w, h, h / 2, alpha(T.accent, a * (p ? .8f : 1)), alpha(T.accent, a * (p ? .65f : .85f)));
        else { rgrad(x, y, w, h, h / 2, alpha(T.card, p ? 3.f : 1.7f), T.card); ring(x, y, w, h, h / 2, 1.2f, T.line); }
        text(F_SANS, primario ? 23 : 20, label, x + w / 2, y + h / 2 - (primario ? 15 : 13), primario ? alpha(ON_ACC, a) : alpha(T.text, a), 1);
    }
    return click;
}
static void upd_fecha(long t, char *out, size_t len) {
    if (t <= 0) { snprintf(out, len, "nunca"); return; }
    time_t tt = t, ahora = time(NULL); struct tm a, h; localtime_r(&tt, &a); localtime_r(&ahora, &h);
    if (a.tm_year == h.tm_year && a.tm_yday == h.tm_yday) snprintf(out, len, "hoy, %02d:%02d", a.tm_hour, a.tm_min);
    else snprintf(out, len, "%d/%d, %02d:%02d", a.tm_mday, a.tm_mon + 1, a.tm_hour, a.tm_min);
}
static const char *UPD_PASOS[][2] = {
    {"comprobando", "Comprobando batería, disco y red"}, {"punto", "Punto para volver atrás"},
    {"cambios", "Cambios del móvil"}, {"compilando", "Compilando Fósil"}, {"linux", "Actualizando Linux"},
    {"instalando", "Instalando Fósil"}, {"reiniciando", "Reiniciando Fósil"},
};
static void draw_update_pasos(const Upd *u) {
    float y = header("Actualizando");
    text_wrap2(F_SANS, 20, !strcmp(u->paso, "esperando") ? u->texto : "No apagues el móvil. Al final Fósil se reinicia y te pide el PIN.",
               42, y, 28, W - 84, T.sub);
    y += 84;
    int actual = -1;
    for (int i = 0; i < 7; i++) if (!strcmp(u->paso, UPD_PASOS[i][0])) actual = i;
    for (int i = 0; i < 7; i++) {
        if (upd_solo_linux && i != 0 && i != 4) continue;
        bool hecho = actual > i, ahora = actual == i;
        float r = T.metro ? 0 : 7, cy = y + 18;
        if (hecho) rr(42, cy, 22, 22, r, T.accent);
        else if (ahora) ring(42, cy, 22, 22, r, 3, T.accent);
        else ring(42, cy, 22, 22, r, 2, alpha(T.sub, .6f));
        text(F_SANS, 22, UPD_PASOS[i][1], 84, y + 12, hecho ? T.sub : ahora ? T.text : alpha(T.sub, .75f), 0);
        y += 60;
    }
    text_wrap2(F_SANS, 19, u->texto, 42, H - 100, 26, W - 84, T.sub);
}
static void draw_update_terminado(const Upd *u) {
    bool rein = strstr(u->texto, "Reinicia") != NULL;
    const char *v = u->inst;
    C fg = T.text, sub = T.sub;
    if (T.metro) { rr(0, 0, W, H, 0, T.accent); fg = rgba(1, 1, 1, 1); sub = fg; text(F_SANS, 16, "FÓSIL", 40, 120, fg, 0); }
    else { ring(W / 2.f - 48, 150, 96, 96, 48, 3, T.accent); text(F_SANS, 44, "✓", W / 2.f, 170, T.accent, 1); }
    float y = T.metro ? 150 : 290;
    char b[160];
    if (rein) snprintf(b, sizeof b, "%s", T.metro ? "reinicia el móvil para terminar" : "Reinicia el móvil para terminar");
    else snprintf(b, sizeof b, "%s", u->texto);
    if (T.metro) for (char *c = b; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;
    text_wrap2(F_SANS, T.metro ? 46 : 36, b, T.metro ? 40 : 42, y, T.metro ? 54 : 44, W - 84, fg);
    y += T.metro ? 130 : 110;
    if (rein) snprintf(b, sizeof b, "Fósil v%s ya está instalado. Linux ha cambiado de kernel y lo usará al reiniciar.", v);
    else if (strstr(u->texto, "Actualizado")) snprintf(b, sizeof b, "Si algo no va bien, vuelve atrás desde Tareas con el punto «Antes de actualizar a v%s».", v);
    else b[0] = 0;
    if (*b) text_wrap2(F_SANS, 20, b, T.metro ? 40 : 42, y, 30, W - 84, sub);
    float by = H - 48 - 64;
    if (rein) {
        float bw = (W - 80 - 12) / 2.f;
        if (T.metro) {
            unsigned a = ID(), c = ID();
            if (btn(a, 40, by, bw, 64)) { upd_visto = u->estado_t; int r = system("systemctl reboot"); (void)r; }
            rr(40, by, bw, 64, 0, rgba(1, 1, 1, 1)); text(F_SANS, 19, "reiniciar", 40 + bw / 2, by + 19, rgba(0, 0, 0, 1), 1);
            if (btn(c, 52 + bw, by, bw, 64)) { upd_visto = u->estado_t; go(S_HOME, -1); }
            if (pressed(c)) rr(52 + bw, by, bw, 64, 0, alpha(rgba(1, 1, 1, 1), .25f));
            ring(52 + bw, by, bw, 64, 0, 2.4f, rgba(1, 1, 1, 1)); text(F_SANS, 19, "más tarde", 52 + bw + bw / 2, by + 19, fg, 1);
        } else {
            if (upd_boton(ID(), 40, by, bw, 64, "Reiniciar ahora", true, true)) { upd_visto = u->estado_t; int r = system("systemctl reboot"); (void)r; }
            if (upd_boton(ID(), 52 + bw, by, bw, 64, "Más tarde", false, true)) { upd_visto = u->estado_t; go(S_HOME, -1); }
        }
    } else {
        unsigned id = ID();
        if (btn(id, 40, by, W - 80, 64)) { upd_visto = u->estado_t; go(S_HOME, -1); }
        if (T.metro) {
            if (pressed(id)) rr(40, by, W - 80, 64, 0, alpha(rgba(1, 1, 1, 1), .25f));
            ring(40, by, W - 80, 64, 0, 2.4f, fg); text(F_SANS, 19, "listo", W / 2.f, by + 19, fg, 1);
        } else upd_boton(id, 40, by, W - 80, 64, "Volver al inicio", false, true);
    }
}
// Desde el inicio: avisa una vez por versión nueva y, tras reiniciarse Fósil al instalar, enseña cómo ha ido.
static void upd_vigilar(void) {
    Upd u; upd_copiar(&u);
    static char avisada[24];
    if (*u.disp && strcmp(u.disp, avisada)) { snprintf(avisada, sizeof avisada, "%s", u.disp); toast("Fósil v%s disponible", u.disp); }
    if (!upd_en_marcha(&u) && upd_terminado_nuevo(&u) && (strstr(u.texto, "Actualizado") || strstr(u.texto, "Reinicia"))
        && time(NULL) - u.estado_t < 3600) go(S_UPDATE, 1);
}
static void draw_update(void) {
    Upd u; upd_copiar(&u);
    bool marcha = upd_en_marcha(&u);
    if (marcha && now_s() - upd_pulsado >= 6) { draw_update_pasos(&u); return; }
    if (!marcha && upd_terminado_nuevo(&u)) { draw_update_terminado(&u); return; }
    header_to = S_SETTINGS;
    float y = header("Actualizaciones");
    char b[200], f[32];
    bool hay = *u.disp != 0;
    if (T.metro) {
        snprintf(b, sizeof b, hay ? "v%s" : "al día", hay ? u.disp : "");
        text(F_SANS, 40, b, 34, y - 6, hay ? T.accent : T.text, 0);
        snprintf(b, sizeof b, hay ? "disponible · tienes la v%s" : "tienes la v%s", u.inst);
        text(F_SANS, 18, b, 36, y + 48, T.sub, 0);
        y += 92;
    } else {
        card(34, y, W - 68, 124, 24);
        text(F_SANS, 22, "Instalada", 58, y + 22, T.sub, 0);
        snprintf(b, sizeof b, "v%s", u.inst); text(F_SANS, 22, b, W - 58, y + 22, T.text, 2);
        rr(58, y + 62, W - 116, 1, 0, T.line);
        text(F_SANS, 22, "Disponible", 58, y + 78, T.sub, 0);
        snprintf(b, sizeof b, hay ? "v%s" : "Al día", u.disp); text(F_SANS, 22, b, W - 58, y + 78, hay ? T.accent : T.text, 2);
        y += 148;
    }
    if (hay && *u.notas) {
        snprintf(b, sizeof b, T.metro ? "Novedades" : "Novedades de v%s", u.disp);
        text(F_SANS, 20, b, 42, y, T.sub, 0); y += 34;
        char *s = u.notas; int n = 0;   // asunto y cuerpo de la etiqueta, una línea por fila
        while (s && *s && n < 6 && y < 600) {
            char *e = strchr(s, '\n'); if (e) *e = 0;
            if (*s) { text_fit(F_SANS, 20, s, 42, y, W - 84, T.text); y += 30; n++; }
            s = e ? e + 1 : NULL;
        }
        y += 14;
    }
    text(F_SANS, 20, "Linux", 42, y, T.sub, 0);
    if (u.linux_n > 0) snprintf(b, sizeof b, u.linux_n == 1 ? "1 paquete pendiente" : "%d paquetes pendientes", u.linux_n);
    else snprintf(b, sizeof b, u.linux_n == 0 ? "Al día" : "Sin comprobar");
    text(F_SANS, 20, b, T.metro ? 42 : W - 42, T.metro ? y + 30 : y, T.text, T.metro ? 0 : 2);
    y += T.metro ? 70 : 40;
    upd_fecha(u.comprobado, f, sizeof f);
    snprintf(b, sizeof b, "Última comprobación: %s", f); text(F_SANS, 17, b, 42, y, T.sub, 0); y += 34;
    const char *fallo = *u.err ? u.err : !strcmp(u.paso, "error") ? u.texto : "";
    if (*fallo) text_wrap2(F_SANS, 19, fallo, 42, y, 26, W - 84, rgba(1, .62f, .5f, 1));
    bool buscando = now_s() - upd_buscando < 60 && u.comprobado == upd_comp_antes;
    float bw = (W - 68 - 12) / 2.f, by = H - 40 - 64;
    if (upd_boton(ID(), 34, by - 12 - 72, W - 68, 72, "Actualizar", true, hay && !marcha)) {
        upd_pulsado = now_s(); upd_solo_linux = false; upd_lanzar("fosil-actualizar.service");
    }
    if (upd_boton(ID(), 34, by, bw, 64, buscando ? "Buscando…" : "Buscar ahora", false, !marcha && !buscando)) {
        upd_buscando = now_s(); upd_comp_antes = u.comprobado; upd_lanzar("fosil-actualizar-comprobar.service");
    }
    if (upd_boton(ID(), 46 + bw, by, bw, 64, "Solo Linux", false, !marcha && u.linux_n != 0)) {
        upd_pulsado = now_s(); upd_solo_linux = true; upd_lanzar("fosil-actualizar-linux.service");
    }
}
static void draw_power(void) {
    float y = header("Energía");
    struct { const char *t, *d; } o[] = {
        {"Bloquear", "Vuelve a pedir la contraseña"},
        {"Reiniciar", "Arranca de nuevo"},
        {"Apagar", "Apaga el móvil"},
    };
    for (int i = 0; i < 3; i++) {
        unsigned id = 4100 + i;
        float yy = y + 20 + i * 122;
        bool c = btn(id, 34, yy, W - 68, 108);
        float p = pressed(id);
        rgrad(34, yy, W - 68, 108, 26, alpha(T.card, p ? 3.2f : 1.7f), T.card);
        ring(34, yy, W - 68, 108, 26, 1.2f, T.line);
        text(F_SANS, 28, o[i].t, 60, yy + 22, i == 2 ? rgba(1, .6f, .5f, 1) : T.text, 0);
        text(F_SANS, 19, o[i].d, 60, yy + 64, T.sub, 0);
        if (c) {
            if (i == 0) { explicit_bzero(pw_buf, sizeof pw_buf); kb_open = false; auth_state = 0; go(S_LOCK, -1); }
            if (i == 1) { int r = system("systemctl reboot"); (void)r; }
            if (i == 2) { int r = system("systemctl poweroff"); (void)r; }
        }
    }
}
static void draw_pin(void) {
    header("PIN");
    if (pin_stage == 0) {
        text(F_SANS, 24, "Primero, tu contraseña", W / 2.f, 170, T.text, 1);
        text(F_SANS, 19, auth_state == 4 ? "Contraseña incorrecta" : "Para que nadie cambie el PIN sin ti", W / 2.f, 208, auth_state == 4 ? rgba(1, .55f, .55f, 1) : T.sub, 1);
        card(34, 256, W - 68, 68, 20);
        int n = 0; for (char *p = pw_buf; *p; p++) if (((unsigned char)*p & 0xC0) != 0x80) n++;
        float total = fmin(n, 16) * 22, x = W / 2.f - total / 2 + 8;
        for (int i = 0; i < n && i < 16; i++) rr(x + i * 22 - 7, 283, 14, 14, 7, T.text);
        if (auth_state == 1) text(F_SANS, 20, "Comprobando…", W / 2.f, 340, T.accent, 1);
        if (keyboard("Seguir") && auth_state != 1 && *pw_buf) auth_start();
        return;
    }
    int len = strlen(pin_in);
    text(F_SANS, 26, pin_stage == 1 ? "Elige un PIN" : "Repítelo", W / 2.f, 150, T.text, 1);
    text(F_SANS, 19, pin_stage == 1 ? "De 4 a 8 cifras" : "Para confirmar", W / 2.f, 190, T.sub, 1);
    pin_dots(240, len, pin_stage == 1 ? (len < 4 ? 4 : len) : (int)strlen(pin_first));
    if (pin_stage == 1 && pin_len) {
        unsigned id = ID();
        if (btn(id, W / 2.f - 90, 272, 180, 50)) {
            pin_len = 0; pin_hash[0] = 0; pin_write(); explicit_bzero(pin_in, sizeof pin_in);
            toast("PIN quitado"); go(S_SETTINGS, -1); return;
        }
        text(F_SANS, 20, "Quitar PIN", W / 2.f, 284, rgba(1, .6f, .5f, 1), 1);
    }
    int k = pinpad(340, pin_stage == 1 && len >= 4);
    if (k < 0) return;
    if (k == 11) { snprintf(pin_first, sizeof pin_first, "%s", pin_in); explicit_bzero(pin_in, sizeof pin_in); pin_stage = 2; return; }
    pin_key(k);
    if (pin_stage == 2 && strlen(pin_in) == strlen(pin_first)) {
        bool same = !strcmp(pin_in, pin_first);
        if (same && pin_set(pin_in)) { toast("PIN guardado"); go(S_SETTINGS, -1); }
        else { pin_shake = now_s(); vibrate(); toast(same ? "No se pudo guardar" : "No coincide: vuelve a empezar"); pin_stage = 1; }
        explicit_bzero(pin_in, sizeof pin_in); explicit_bzero(pin_first, sizeof pin_first);
    }
}

// ---------------------------------------------------------------- chat con el agente
// El trabajo lo hace el servicio fosil-agente (el usuario del móvil). Aquí solo se pinta su registro.
#define CHAT_DIR STATE_DIR "/chat"
#define CHAT_TAIL (48 * 1024)
#define CHAT_MSGS 60
#define CHAT_LINES 900
static struct { pthread_mutex_t m; char *raw; bool fresh, busy, sesion, conocido, total; char motor[16]; } ch = {.m = PTHREAD_MUTEX_INITIALIZER, .motor = "claude", .total = true};
typedef struct { char k; float y, h, w; int first, n; } ChatMsg;
static ChatMsg cmsg[CHAT_MSGS]; static int cmsg_n; static char *cline[CHAT_LINES]; static int cline_n; static float chat_h;
static char chat_buf[1000];
static bool chat_send(const char *m) {
    int fd = open(CHAT_DIR "/entrada", O_WRONLY | O_NONBLOCK);
    if (fd < 0) { toast("El agente no está en marcha"); return false; }
    char b[1100]; int n = snprintf(b, sizeof b, "%s\n", m);
    ssize_t w = write(fd, b, n); close(fd);
    return w == n;
}
static bool chat_send_fw(const char *m) { return chat_send(m); }
static bool lock_trabajando(void) { pthread_mutex_lock(&ch.m); bool b = ch.busy; pthread_mutex_unlock(&ch.m); return b; }
static void chat_entered(void) { chat_send("!estado"); }
static void chat_toggle_permisos(void) {
    pthread_mutex_lock(&ch_m_ajustes); bool t = ajustes_total; pthread_mutex_unlock(&ch_m_ajustes);
    if (chat_send(t ? "!permisos limitado" : "!permisos total")) {
        pthread_mutex_lock(&ch_m_ajustes); ajustes_total = !t; pthread_mutex_unlock(&ch_m_ajustes);
        toast(t ? "Agentes en modo limitado" : "Agentes con acceso total");
    }
}
static volatile bool notify_req = false; static char notify_who[16];
static void tareas_medir(void);
static void *chat_thread(void *arg) {
    (void)arg; long long sl = -1, se = -1;
    for (;;) {
        usleep(blanked || screen != S_CHAT ? 2000000 : 250000);
        struct stat a; long long v = sl;
        if (screen == S_TAREAS && !blanked) tareas_medir();
        if (!blanked && screen == S_CHAT && !stat(CHAT_DIR "/log", &a)) v = (long long)a.st_mtim.tv_nsec + a.st_size * 1000003LL + (long long)a.st_mtime * 7;
        bool cambio = false;
        if (v != sl) {
            sl = v; char *raw = NULL; FILE *f = fopen(CHAT_DIR "/log", "r");
            if (f) {
                fseek(f, 0, SEEK_END); long sz = ftell(f); long from = sz > CHAT_TAIL ? sz - CHAT_TAIL : 0;
                fseek(f, from, SEEK_SET); raw = malloc(sz - from + 1);
                size_t r = fread(raw, 1, sz - from, f); raw[r] = 0; fclose(f);
                if (from) { char *nl = strchr(raw, '\n'); if (nl) memmove(raw, nl + 1, strlen(nl)); }
            }
            pthread_mutex_lock(&ch.m); free(ch.raw); ch.raw = raw; ch.fresh = true; pthread_mutex_unlock(&ch.m);
            cambio = true;
        }
        v = 0; if (!stat(CHAT_DIR "/estado", &a)) v = (long long)a.st_mtim.tv_nsec + (long long)a.st_mtime * 7;
        if (v != se) {
            se = v; char e[32] = "", mo[16] = "", se2[8] = "", pe[16] = ""; FILE *f = fopen(CHAT_DIR "/estado", "r");
            if (f) { if (fscanf(f, "%31s %15s %7s %15s", e, mo, se2, pe) < 1) e[0] = 0; fclose(f); }
            pthread_mutex_lock(&ch.m);
            ch.busy = !strcmp(e, "pensando");
            { static bool antes = false; bool ahora = ch.busy;
              if (antes && !ahora) { notify_req = true; snprintf(notify_who, sizeof notify_who, "%s", *mo && !strcmp(mo, "codex") ? "Codex" : "Claude"); }
              antes = ahora; }
            if (*mo) snprintf(ch.motor, sizeof ch.motor, "%s", mo);
            ch.conocido = *se2 != 0 && strcmp(se2, "?"); ch.sesion = !strcmp(se2, "si"); if (*pe) { ch.total = !strcmp(pe, "total"); pthread_mutex_lock(&ch_m_ajustes); ajustes_total = ch.total; pthread_mutex_unlock(&ch_m_ajustes); }
            pthread_mutex_unlock(&ch.m);
            cambio = true;
        }
        if (cambio) { dirty_req = true; wake(); }
    }
    return NULL;
}
static void chat_add_line(const char *s, size_t n, int kind, int size, float *mw) {
    if (cline_n >= CHAT_LINES) return;
    char *l = malloc(n + 1); memcpy(l, s, n); l[n] = 0; cline[cline_n++] = l;
    int w = 0; if (n) TTF_SizeUTF8(font(kind, size), l, &w, NULL);
    if (w > *mw) *mw = w;
}
static bool chat_fits(const char *s, size_t n, int kind, int size, float maxw) {
    char b[800]; if (n >= sizeof b) return false;
    memcpy(b, s, n); b[n] = 0; int w = 0; TTF_SizeUTF8(font(kind, size), b, &w, NULL);
    return w <= maxw;
}
static void chat_wrap(const char *s, int kind, int size, float maxw, float *mw) {
    const char *p = s;
    for (;;) {
        const char *e = strchr(p, '\n'); const char *end = e ? e : p + strlen(p);
        if (p == end) chat_add_line(p, 0, kind, size, mw);
        const char *q = p;
        while (q < end) {
            const char *ok = q, *r = q;
            while (r < end) {
                const char *nx = r; while (nx < end && *nx != ' ') nx++;
                if (!chat_fits(q, nx - q, kind, size, maxw)) break;
                ok = nx; r = nx < end ? nx + 1 : nx;
            }
            if (ok == q) {  // palabra más larga que la línea: se corta por caracteres
                const char *c = q + 1;
                for (;;) {
                    const char *d = c; while (d < end && ((unsigned char)*d & 0xC0) == 0x80) d++;
                    if (d >= end || !chat_fits(q, d - q, kind, size, maxw)) { ok = c; break; }
                    c = d + 1;
                }
                while (ok < end && ((unsigned char)*ok & 0xC0) == 0x80) ok++;
                chat_add_line(q, ok - q, kind, size, mw); q = ok;
            } else {
                chat_add_line(q, ok - q, kind, size, mw); q = ok < end ? ok + 1 : ok;
            }
        }
        if (!e) break;
        p = e + 1;
    }
}
static void chat_layout(char *raw) {
    for (int i = 0; i < cline_n; i++) free(cline[i]);
    cline_n = 0; cmsg_n = 0; chat_h = 0;
    if (!raw) return;
    char *ls[400]; int nl = 0;
    for (char *sv, *t = strtok_r(raw, "\n", &sv); t; t = strtok_r(NULL, "\n", &sv)) {
        if (nl == 400) { memmove(ls, ls + 1, sizeof(char *) * 399); nl = 399; }
        ls[nl++] = t;
    }
    int from = nl > CHAT_MSGS ? nl - CHAT_MSGS : 0; float y = 0;
    for (int i = from; i < nl && cline_n < CHAT_LINES; i++) {
        char *t = ls[i]; if (strlen(t) < 3 || t[1] != '|') continue;
        char k = t[0]; char *b = t + 2; ChatMsg *m = &cmsg[cmsg_n++];
        // \n literal a salto real; sin marcas de Markdown
        char *w = b; for (char *r = b; *r; r++) {
            if (r[0] == '\\' && r[1] == 'n') { *w++ = '\n'; r++; }
            else if (r[0] == '*' && r[1] == '*') r++;
            else if (*r == '`') {}
            else if (*r == '\t') *w++ = ' ';
            else *w++ = *r;
        }
        *w = 0; if (strlen(b) > 3000) strcpy(b + 2990, "…");
        m->k = k; m->first = cline_n; m->w = 0;
        if (k == 't') { chat_add_line(b, strlen(b) > 300 ? 300 : strlen(b), F_MONO, 15, &m->w); m->n = 1; m->h = 28; }
        else {
            chat_wrap(b, F_SANS, 20, k == 'y' ? W - 170 : W - 104, &m->w);
            m->n = cline_n - m->first; m->h = m->n * 27 + 24;
        }
        m->y = y; y += m->h + (k == 't' ? 4 : 12);
    }
    chat_h = y;
}
static void text_tail(int kind, int size, const char *s, float x, float y, float maxw, C c) {
    size_t n = strlen(s), offsets[n + 1], count = 0;
    for (size_t i = 0; i < n; i++)
        if (((unsigned char)s[i] & 0xC0) != 0x80) offsets[count++] = i;
    offsets[count] = n;
    size_t lo = 0, hi = count; int width; TTF_Font *f = font(kind, size);
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        TTF_SizeUTF8(f, s + offsets[mid], &width, NULL);
        if (width > maxw) lo = mid + 1; else hi = mid;
    }
    text(kind, size, s + offsets[lo], x, y, c, 0);
}
static void draw_chat(void) {
    static float last_bottom = 0;
    pthread_mutex_lock(&ch.m);
    bool busy = ch.busy, sesion = ch.sesion, conocido = ch.conocido, fresh = ch.fresh; char motor[16]; snprintf(motor, sizeof motor, "%s", ch.motor);
    bool at_end = scroll_y[S_CHAT] >= scroll_max - 40;
    if (fresh) { chat_layout(ch.raw); free(ch.raw); ch.raw = NULL; ch.fresh = false; }
    pthread_mutex_unlock(&ch.m);
    bool codex = !strcmp(motor, "codex");
    const char *nombre = codex ? "Codex" : "Claude";
    header(nombre);
    { static const char *lab[3] = {"Chats", "Nueva", NULL}; float bw = 78, bx = W - 34 - 3 * bw - 12;
      for (int i = 0; i < 3; i++) {
          unsigned id = 7500 + i; float x = bx + i * (bw + 6);
          bool c = btn(id, x, 44, bw, 60);
          rr(x, 48, bw, 52, 26, alpha(T.card, pressed(id) ? 3.f : 1.6f)); ring(x, 48, bw, 52, 26, 1.2f, T.line);
          text(F_SANS, 17, i == 2 ? (codex ? "Claude" : "Codex") : lab[i], x + bw / 2, 64, T.sub, 1);
          if (!c) continue;
          if (i == 0) go(S_CONVS, 1);
          else if (busy) toast("Espera a que termine o pulsa Parar");
          else if (i == 1) chat_send("!nuevo");
          else chat_send(codex ? "!motor claude" : "!motor codex");
      } }
    float start = 122, bar = 62, plant = (!kb_open && !busy && cmsg_n) ? 52 : 0;
    float bottom = (kb_open ? H - KB_H - 6 - KB_BAR : H - 22) - bar - 10 - plant;
    scroll_area(start, bottom, chat_h + 8, S_CHAT);
    if ((fresh || bottom != last_bottom) && at_end) scroll_y[S_CHAT] = scroll_max;
    if (scroll_y[S_CHAT] > scroll_max) scroll_y[S_CHAT] = scroll_max;
    last_bottom = bottom;
    if (kb_open && ui.release && ui.y < bottom && !ui.dragged) kb_open = false;
    if (cmsg_n == 0) {
        float y = start + 16;
        card(34, y, W - 68, conocido && !sesion ? 262 : 196, 26);
        icon(I_AGENT, W / 2.f, y + 56, 30, T.accent);
        if (conocido && !sesion) {
            text(F_SANS, 24, "Falta iniciar sesión", W / 2.f, y + 100, T.text, 1);
            text(F_SANS, 18, "Solo una vez. En Terminal, ejecuta:", W / 2.f, y + 140, T.sub, 1);
            text(F_MONO, 19, codex ? "codex login --device-auth" : "claude", W / 2.f, y + 172, T.accent, 1);
            if (!codex) text(F_SANS, 18, "y dentro escribe /login", W / 2.f, y + 208, T.sub, 1);
        } else {
            char b[64]; snprintf(b, sizeof b, "Pídeselo a %s", nombre);
            text(F_SANS, 24, b, W / 2.f, y + 100, T.text, 1);
            text(F_SANS, 18, ch.total ? "Acceso total al móvil, con sudo" : "Lee el sistema y escribe en ~/taller", W / 2.f, y + 140, T.sub, 1);
            static const char *ideas[] = {"¿Cómo está el móvil ahora mismo?", "¿Qué programas hay instalados?", "Escríbeme un script de ejemplo"};
            float yy = y + 214;
            for (int i = 0; i < 3 && !kb_open; i++) {
                unsigned id = 7200 + i;
                if (btn(id, 34, yy, W - 68, 64) && chat_send(ideas[i])) { pthread_mutex_lock(&ch.m); ch.busy = true; pthread_mutex_unlock(&ch.m); }
                rr(34, yy, W - 68, 64, 22, alpha(T.card, pressed(id) ? 3.f : 1.3f)); ring(34, yy, W - 68, 64, 22, 1.2f, T.line);
                text_fit(F_SANS, 20, ideas[i], 56, yy + 19, W - 112, T.text);
                yy += 76;
            }
        }
    }
    float sy = scroll_y[S_CHAT];
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, (int)(H - bottom), W, (int)(bottom - start));
    for (int i = 0; i < cmsg_n; i++) {
        ChatMsg *m = &cmsg[i]; float y = start + m->y - sy;
        if (y + m->h < start || y > bottom) continue;
        if (m->k == 't') {
            text(F_MONO, 15, "›", 44, y + 5, T.accent, 0);
            text_fit(F_MONO, 15, cline[m->first], 62, y + 5, W - 100, T.sub);
            continue;
        }
        bool me = m->k == 'y', err = m->k == 'e';
        float bw = me ? m->w + 36 : W - 68, bx = me ? W - 34 - bw : 34;
        if (me) rr(bx, y, bw, m->h, 20, alpha(T.accent, .92f));
        else { rr(bx, y, bw, m->h, 20, err ? rgba(.5f, .12f, .1f, .55f) : alpha(T.card, 1.5f)); ring(bx, y, bw, m->h, 20, 1.2f, T.line); }
        for (int l = 0; l < m->n; l++)
            text(F_SANS, 20, cline[m->first + l], bx + 18, y + 12 + l * 27, me ? ON_ACC : err ? rgba(1, .8f, .78f, 1) : T.text, 0);
    }
    glDisable(GL_SCISSOR_TEST);
    float by = bottom + 8 + plant;
    if (plant) {   // plantillas
        static const char *pl[3] = {"Sigue", "Arregla el error", "Resume"};
        static const char *ms[3] = {"Sigue con lo que estabas haciendo.", "Arregla el último error.", "Resume en pocas frases lo que has hecho."};
        static const float pw[3] = {100, 206, 150}; float px = 34;
        for (int i = 0; i < 3; i++) {
            unsigned id = 7600 + i;
            if (btn(id, px, bottom + 6, pw[i], 44) && chat_send(ms[i])) { pthread_mutex_lock(&ch.m); ch.busy = true; pthread_mutex_unlock(&ch.m); }
            rr(px, bottom + 6, pw[i], 44, 22, alpha(T.card, pressed(id) ? 3.f : 1.3f)); ring(px, bottom + 6, pw[i], 44, 22, 1.1f, T.line);
            text(F_SANS, 18, pl[i], px + pw[i] / 2, bottom + 17, T.text, 1);
            px += pw[i] + 6;
        }
    }
    if (busy) {
        { unsigned idq = ID(); if (btn(idq, 34, by, W - 68 - 110, bar) && !kb_open) kb_show_texto(chat_buf, sizeof chat_buf); }
        rr(34, by, W - 68 - 110, bar, 24, alpha(T.card, 1.6f)); ring(34, by, W - 68 - 110, bar, 24, 1.3f, kb_open ? T.accent : T.line);
        int ph = (int)(now_s() * 3) % 3;
        for (int i = 0; i < 3; i++) rr(58 + i * 18, by + 26, 10, 10, 5, alpha(T.accent, i == ph ? 1 : .35f));
        if (*chat_buf) text_tail(F_SANS, 20, chat_buf, 124, by + 19, W - 68 - 110 - 100, T.text);
        else text(F_SANS, 20, kb_open ? "Para la cola" : "Trabajando", 124, by + 19, T.sub, 0);
        unsigned id = ID();
        if (btn(id, W - 34 - 100, by, 100, bar)) chat_send("!parar");
        rr(W - 34 - 100, by, 100, bar, 24, rgba(.75f, .25f, .2f, pressed(id) ? 1 : .8f));
        text(F_SANS, 20, "Parar", W - 34 - 50, by + 19, T.text, 1);
    } else {
        unsigned id = ID();
        if (btn(id, 34, by, W - 68, bar) && !kb_open) kb_show_texto(chat_buf, sizeof chat_buf);
        rr(34, by, W - 68, bar, 24, alpha(T.card, 1.8f)); ring(34, by, W - 68, bar, 24, 1.3f, kb_open ? T.accent : T.line);
        if (*chat_buf) text_tail(F_SANS, 20, chat_buf, 56, by + 19, W - 112, T.text);
        else text(F_SANS, 20, "Escribe aquí", 56, by + 19, T.sub, 0);
    }
    if (kb_open && keyboard("Enviar") && *chat_buf) {
        if (chat_send(chat_buf)) { chat_buf[0] = 0; kb_open = false; pthread_mutex_lock(&ch.m); ch.busy = true; pthread_mutex_unlock(&ch.m); }
    }
}

// ---------------------------------------------------------------- conversaciones
#define MAX_CONVS 40
static struct { char id[20], hora[16], titulo[64]; } convs[MAX_CONVS]; static int nconvs;
static void convs_load(void) {
    nconvs = 0; FILE *f = fopen(CHAT_DIR "/lista", "r"); if (!f) return;
    char l[200];
    while (nconvs < MAX_CONVS && fgets(l, sizeof l, f)) {
        l[strcspn(l, "\n")] = 0; char *a = strchr(l, '|'); if (!a) continue; *a++ = 0;
        char *b = strchr(a, '|'); if (!b) continue; *b++ = 0;
        snprintf(convs[nconvs].id, 20, "%s", l); snprintf(convs[nconvs].hora, 16, "%s", a); snprintf(convs[nconvs].titulo, 64, "%s", *b ? b : "Sin título"); nconvs++;
    }
    fclose(f);
}
static void draw_convs(void) {
    header_to = S_CHAT; header("Chats");
    float start = 122, bottom = H - 24, rowh = 92;
    scroll_area(start, bottom, nconvs * rowh + 10, S_CONVS);
    if (!nconvs) { text(F_SANS, 22, "Aún no hay conversaciones", W / 2.f, 220, T.sub, 1); return; }
    float sy = scroll_y[S_CONVS];
    glEnable(GL_SCISSOR_TEST); glScissor(0, (int)(H - bottom), W, (int)(bottom - start));
    for (int i = 0; i < nconvs; i++) {
        float y = start + i * rowh - sy; if (y < start - rowh || y > bottom) continue;
        unsigned id = 7700 + i, idx = 7800 + i; bool bx = btn(idx, W - 34 - 70, y, 70, rowh - 10), c = !bx && btn(id, 34, y, W - 68 - 70, rowh - 10);
        rgrad(34, y, W - 68, rowh - 10, 20, alpha(T.card, pressed(id) ? 3.f : 1.7f), T.card); ring(34, y, W - 68, rowh - 10, 20, 1.2f, T.line);
        text_fit(F_SANS, 21, convs[i].titulo, 56, y + 14, W - 180, T.text);
        text(F_MONO, 15, convs[i].hora, 56, y + 50, T.sub, 0);
        text(F_SANS, 26, "×", W - 34 - 35, y + 26, T.sub, 1);
        char m[48];
        if (c) { snprintf(m, sizeof m, "!abrir %s", convs[i].id); chat_send(m); go(S_CHAT, -1); }
        if (bx) { snprintf(m, sizeof m, "!borrar %s", convs[i].id); chat_send(m); usleep(150000); convs_load(); }
    }
    glDisable(GL_SCISSOR_TEST);
}

// ---------------------------------------------------------------- tareas y puntos para volver atrás
static struct { pthread_mutex_t m; int rss, cola; float cpu; char fotos[8][120]; int nf; bool volviendo; } tar = {.m = PTHREAD_MUTEX_INITIALIZER};
static int foto_conf = -1;
static void tareas_medir(void) {
    static unsigned long long pt = 0, pj = 0; unsigned long long j = 0, tot = 0; int rss = 0;
    DIR *d = opendir("/proc"); struct dirent *e;
    if (d) { while ((e = readdir(d))) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        char p[64], c[32] = ""; snprintf(p, sizeof p, "/proc/%s/stat", e->d_name);
        FILE *f = fopen(p, "r"); if (!f) continue;
        unsigned long long ut = 0, stm = 0; long r = 0; int pid;
        if (fscanf(f, "%d (%31[^)]) %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu %*d %*d %*d %*d %*d %*d %*u %*u %ld", &pid, c, &ut, &stm, &r) == 5 && (!strncmp(c, "claude", 6) || !strncmp(c, "codex", 5))) { j += ut + stm; rss += r * 4 / 1024; }
        fclose(f);
    } closedir(d); }
    FILE *f = fopen("/proc/stat", "r"); if (f) { unsigned long long a[8] = {0}; if (fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", a, a+1, a+2, a+3, a+4, a+5, a+6, a+7) == 8) for (int i = 0; i < 8; i++) tot += a[i]; fclose(f); }
    int cola = 0; if ((f = fopen(CHAT_DIR "/cola", "r"))) { int c; while ((c = fgetc(f)) != EOF) if (c == '\n') cola++; fclose(f); }
    pthread_mutex_lock(&tar.m); tar.rss = rss; tar.cola = cola;
    tar.cpu = tot > pt && j >= pj ? (float)(j - pj) * 100 / (tot - pt) : 0; pt = tot; pj = j; pthread_mutex_unlock(&tar.m);
    wake(); dirty_req = true;
}
static void *fotos_thread(void *a) {
    (void)a; FILE *p = popen("/usr/local/bin/fosil-foto listar 6 2>/dev/null", "r"); char l[120]; int n = 0; char t[8][120];
    if (p) { while (n < 8 && fgets(l, sizeof l, p)) { l[strcspn(l, "\n")] = 0; snprintf(t[n++], 120, "%s", l); } pclose(p); }
    pthread_mutex_lock(&tar.m); memcpy(tar.fotos, t, sizeof t); tar.nf = n; pthread_mutex_unlock(&tar.m); dirty_req = true; wake();
    return NULL;
}
static void tareas_entered(void) { foto_conf = -1; pthread_t th; pthread_create(&th, NULL, fotos_thread, NULL); pthread_detach(th); }
static void *volver_thread(void *a) {
    char *h = a, c[80]; snprintf(c, sizeof c, "/usr/local/bin/fosil-foto volver %s >/dev/null 2>&1", h); free(h);
    int r = system(c);
    pthread_mutex_lock(&tar.m); tar.volviendo = false; pthread_mutex_unlock(&tar.m);
    toast(r == 0 ? "Sistema devuelto a ese punto" : "No se pudo volver a ese punto"); fotos_thread(NULL);
    return NULL;
}
static void draw_tareas(void) {
    float y = header("Tareas");
    pthread_mutex_lock(&ch.m); bool busy = ch.busy; char mo[16]; snprintf(mo, sizeof mo, "%s", ch.motor); pthread_mutex_unlock(&ch.m);
    pthread_mutex_lock(&tar.m); int rss = tar.rss, cola = tar.cola, nf = tar.nf; float cpu = tar.cpu; bool volv = tar.volviendo; char f[8][120]; memcpy(f, tar.fotos, sizeof f); pthread_mutex_unlock(&tar.m);
    card(34, y - 12, W - 68, 170, 24); char b[120];
    text(F_SANS, 24, busy ? (!strcmp(mo, "codex") ? "Codex trabajando" : "Claude trabajando") : "Sin tareas en marcha", 56, y + 8, busy ? T.accent : T.text, 0);
    snprintf(b, sizeof b, "%d MB · %.0f %% de CPU · %d en cola", rss, cpu, cola);
    text(F_SANS, 19, b, 56, y + 48, T.sub, 0);
    { unsigned id = ID(); bool c = btn(id, 56, y + 84, 150, 56) && busy;
      rr(56, y + 84, 150, 56, 22, busy ? rgba(.75f, .25f, .2f, pressed(id) ? 1 : .8f) : rgba(1, 1, 1, .06f));
      text(F_SANS, 20, "Parar", 131, y + 100, busy ? T.text : T.sub, 1); if (c) chat_send("!parar"); }
    y += 180;
    text(F_SANS, 21, "Volver a un punto anterior", 42, y, T.sub, 0); y += 36;
    if (volv) { text(F_SANS, 21, "Volviendo…", W / 2.f, y + 20, T.accent, 1); return; }
    if (!nf) { text(F_SANS, 19, "Se crea un punto antes de cada tarea del chat", W / 2.f, y + 20, T.sub, 1); return; }
    for (int i = 0; i < nf && i < 6; i++) {
        char l[120]; snprintf(l, sizeof l, "%s", f[i]); char *h = l, *t = strchr(l, '|'); if (!t) continue; *t++ = 0; char *m = strchr(t, '|'); if (!m) continue; *m++ = 0;
        float ry = y + i * 80; unsigned id = 7900 + i;
        bool c = btn(id, 34, ry, W - 68, 72);
        bool conf = foto_conf == i;
        rgrad(34, ry, W - 68, 72, 18, conf ? alpha(T.accent, .35f) : alpha(T.card, pressed(id) ? 3.f : 1.7f), T.card); ring(34, ry, W - 68, 72, 18, 1.2f, conf ? T.accent : T.line);
        text_fit(F_SANS, 19, conf ? "Toca otra vez para volver aquí" : m, 54, ry + 10, W - 108, T.text);
        text(F_MONO, 14, t, 54, ry + 42, T.sub, 0);
        if (c) {
            if (conf) { foto_conf = -1; pthread_mutex_lock(&tar.m); tar.volviendo = true; pthread_mutex_unlock(&tar.m); pthread_t th; pthread_create(&th, NULL, volver_thread, strdup(h)); pthread_detach(th); }
            else foto_conf = i;
        }
    }
}

// ---------------------------------------------------------------- visor de texto
static char view_title[64]; static char *view_lines[400]; static int view_n; static Screen view_back = S_FILES;
static void view_set(const char *title, const char *txt, size_t n, Screen back) {
    for (int i = 0; i < view_n; i++) free(view_lines[i]);
    view_n = 0; snprintf(view_title, sizeof view_title, "%s", title); view_back = back;
    const char *p = txt, *end = txt + n;
    while (p < end && view_n < 400) { const char *q = memchr(p, '\n', end - p); size_t l = q ? (size_t)(q - p) : (size_t)(end - p); if (l > 200) l = 200; view_lines[view_n] = strndup(p, l); for (char *c = view_lines[view_n]; *c; c++) if (*c == '\t' || *c == '\r' || *c == 27) *c = ' '; view_n++; if (!q) break; p = q + 1; }
}
static void draw_view(void) {
    header_to = view_back; char t[24]; snprintf(t, sizeof t, "%.20s", view_title); header(t);
    float start = 122, bottom = H - 24, lh = 24;
    scroll_area(start, bottom, view_n * lh + 20, S_VIEW);
    float sy = scroll_y[S_VIEW];
    glEnable(GL_SCISSOR_TEST); glScissor(0, (int)(H - bottom), W, (int)(bottom - start));
    for (int i = 0; i < view_n; i++) { float y = start + i * lh - sy; if (y < start - lh || y > bottom) continue; if (view_lines[i][0]) text_fit(F_MONO, 16, view_lines[i], 26, y, W - 52, T.text); }
    glDisable(GL_SCISSOR_TEST);
}

// ---------------------------------------------------------------- ficheros
#define HOME_L home_u
typedef struct { char name[80]; bool dir; long size; } FE;
static FE fes[200]; static int nfe; static char fcwd[400];  // empieza en ~/taller (main)
static int fe_cmp(const void *a, const void *b) { const FE *x = a, *y = b; if (x->dir != y->dir) return x->dir ? -1 : 1; return strcasecmp(x->name, y->name); }
static void files_load(void) {
    nfe = 0; DIR *d = opendir(fcwd); if (!d) { strcpy(fcwd, HOME_L); d = opendir(fcwd); if (!d) return; }
    struct dirent *e;
    while ((e = readdir(d)) && nfe < 200) {
        if (e->d_name[0] == '.') continue;
        char p[500]; snprintf(p, sizeof p, "%s/%s", fcwd, e->d_name); struct stat s2; if (lstat(p, &s2)) continue;
        snprintf(fes[nfe].name, 80, "%s", e->d_name); fes[nfe].dir = S_ISDIR(s2.st_mode); fes[nfe].size = s2.st_size; nfe++;
    }
    closedir(d); qsort(fes, nfe, sizeof(FE), fe_cmp); scroll_y[S_FILES] = 0;
}
static void file_open(const char *path, const char *name) {
    FILE *f = fopen(path, "r"); if (!f) { toast("No se puede abrir"); return; }
    static char buf[64 * 1024]; size_t n = fread(buf, 1, sizeof buf, f); fclose(f);
    if (memchr(buf, 0, n)) { char m[80]; snprintf(m, sizeof m, "Fichero binario de %zu KB o más.", n / 1024); view_set(name, m, strlen(m), S_FILES); }
    else view_set(name, buf, n, S_FILES);
    scroll_y[S_VIEW] = 0; go(S_VIEW, 1);
}
static void draw_files(void) {
    header("Ficheros");
    char rel[60]; snprintf(rel, sizeof rel, "~%s", fcwd + strlen(HOME_L));
    text_fit(F_MONO, 16, rel, 40, 112, W - 80, T.sub);
    float start = 142, bottom = H - 24, rowh = 66; bool sub = strcmp(fcwd, HOME_L) != 0; int cnt = nfe + (sub ? 1 : 0);
    scroll_area(start, bottom, cnt * rowh + 10, S_FILES);
    float sy = scroll_y[S_FILES]; int abrir = -2;
    glEnable(GL_SCISSOR_TEST); glScissor(0, (int)(H - bottom), W, (int)(bottom - start));
    for (int i = 0; i < cnt; i++) {
        float y = start + i * rowh - sy; if (y < start - rowh || y > bottom) continue;
        int k = sub ? i - 1 : i; unsigned id = 8000 + i;
        if (btn(id, 34, y, W - 68, rowh - 8)) abrir = k;
        rgrad(34, y, W - 68, rowh - 8, 16, alpha(T.card, pressed(id) ? 3.f : 1.6f), T.card); ring(34, y, W - 68, rowh - 8, 16, 1.1f, T.line);
        if (k < 0) { text(F_SANS, 20, "‹ Subir", 56, y + 17, T.accent, 0); continue; }
        icon(fes[k].dir ? I_FILES : I_TERM, 66, y + 29, 12, fes[k].dir ? T.accent : T.sub);
        text_fit(F_SANS, 20, fes[k].name, 96, y + 17, W - 240, T.text);
        if (!fes[k].dir) { char b[24]; if (fes[k].size < 1024) snprintf(b, sizeof b, "%ld B", fes[k].size); else snprintf(b, sizeof b, "%ld KB", fes[k].size / 1024); text(F_MONO, 15, b, W - 54, y + 21, T.sub, 2); }
    }
    glDisable(GL_SCISSOR_TEST);
    if (abrir == -1) { char *sl = strrchr(fcwd, '/'); if (sl && sl > fcwd) *sl = 0; if (strncmp(fcwd, HOME_L, strlen(HOME_L))) strcpy(fcwd, HOME_L); files_load(); }
    else if (abrir >= 0) {
        char p[500]; snprintf(p, sizeof p, "%s/%s", fcwd, fes[abrir].name);
        if (fes[abrir].dir) { if (strlen(p) < sizeof fcwd) { strcpy(fcwd, p); files_load(); } }
        else file_open(p, fes[abrir].name);
    }
}

// ---------------------------------------------------------------- creaciones
typedef struct { char dir[210], nombre[40], comando[300]; bool pantalla; } AppE;
static AppE aps[24]; static int naps; static volatile bool app_running = false;
static void apps_load(void) {
    naps = 0; char ad[120]; snprintf(ad, sizeof ad, "%s/apps", home_u); DIR *d = opendir(ad); if (!d) return; struct dirent *e;
    while ((e = readdir(d)) && naps < 24) {
        if (e->d_name[0] == '.') continue;
        AppE a; memset(&a, 0, sizeof a); snprintf(a.dir, sizeof a.dir, "%s/%.80s", ad, e->d_name);
        char p[230], l[400]; snprintf(p, sizeof p, "%s/app.conf", a.dir); FILE *f = fopen(p, "r"); if (!f) continue;
        snprintf(a.nombre, sizeof a.nombre, "%s", e->d_name);
        while (fgets(l, sizeof l, f)) {
            l[strcspn(l, "\n")] = 0;
            if (!strncmp(l, "nombre=", 7)) snprintf(a.nombre, sizeof a.nombre, "%s", l + 7);
            else if (!strncmp(l, "comando=", 8)) snprintf(a.comando, sizeof a.comando, "%s", l + 8);
            else if (!strncmp(l, "tipo=", 5)) a.pantalla = !strcmp(l + 5, "pantalla");
        }
        fclose(f);
        if (*a.comando) aps[naps++] = a;
    }
    closedir(d);
}
static char app_out[32 * 1024]; static size_t app_n; static char app_name[40]; static volatile bool app_done = false;
static void *app_thread(void *arg) {
    AppE *a = arg; char q1[460], q2[700], qu[140], c[1400];
    shquote(q1, sizeof q1, a->dir); shquote(q2, sizeof q2, a->comando); shquote(qu, sizeof qu, usuario);
    snprintf(c, sizeof c, "cd %s && exec timeout 30 su -s /bin/sh %s -c %s 2>&1", q1, qu, q2);
    FILE *p = popen(c, "r"); app_n = 0;
    if (p) { app_n = fread(app_out, 1, sizeof app_out - 1, p); pclose(p); }
    if (!app_n) { strcpy(app_out, "(sin salida)"); app_n = strlen(app_out); }
    snprintf(app_name, sizeof app_name, "%s", a->nombre); free(a); app_done = true; wake();
    return NULL;
}
static void draw_apps(void) {
    header("Creaciones");
    if (!naps) {
        card(34, 140, W - 68, 196, 24); icon(I_APPS, W / 2.f, 192, 24, T.accent);
        text(F_SANS, 23, "Aún no hay creaciones", W / 2.f, 236, T.text, 1);
        text(F_SANS, 18, "Pídele al chat que te haga una app:", W / 2.f, 276, T.sub, 1);
        text(F_SANS, 18, "aparecerá aquí sola.", W / 2.f, 302, T.sub, 1);
        return;
    }
    float gw = (W - 68 - 24) / 3.f, gh = 120;
    for (int i = 0; i < naps; i++) {
        float x = 34 + (i % 3) * (gw + 12), y = 130 + (i / 3) * (gh + 12); unsigned id = 8300 + i;
        bool c = btn(id, x, y, gw, gh);
        rgrad(x, y, gw, gh, 24, alpha(T.card, pressed(id) ? 3.f : 1.8f), T.card); ring(x, y, gw, gh, 24, 1.2f, T.line);
        char ini[8] = ""; { const char *n = aps[i].nombre; size_t l = 1; if (((unsigned char)n[0] & 0xE0) == 0xC0) l = 2; snprintf(ini, sizeof ini, "%.*s", (int)l, n); }
        text(F_SANS, 38, ini, x + gw / 2, y + 16, T.accent, 1);
        text_fit(F_SANS, 17, aps[i].nombre, x + 10, y + 82, gw - 20, T.text);
        if (c && !app_running) {
            if (aps[i].pantalla) {
                FILE *f = fopen("/run/fosil/lanzar", "w");
                if (f) { char q[260]; shquote(q, sizeof q, aps[i].dir); fprintf(f, "cd %s && SDL_VIDEODRIVER=kmsdrm XDG_RUNTIME_DIR=/tmp %s\n", q, aps[i].comando); fclose(f); _exit(43); }
            } else { toast("Ejecutando %s…", aps[i].nombre); AppE *a = malloc(sizeof(AppE)); *a = aps[i]; app_running = true; pthread_t th; pthread_create(&th, NULL, app_thread, a); pthread_detach(th); }
        }
    }
}
static void draw_screen(Screen s) {
    switch (s) {
    case S_LOCK: draw_lock(); break;
    case S_HOME: draw_home(); break;
    case S_SYSTEM: draw_system(); break;
    case S_NET: draw_net(); break;
    case S_AGENTS: draw_agents(); break;
    case S_SETTINGS: draw_settings(); break;
    case S_POWER: draw_power(); break;
    case S_CHAT: draw_chat(); break;
    case S_PIN: draw_pin(); break;
    case S_CONVS: draw_convs(); break;
    case S_TAREAS: draw_tareas(); break;
    case S_FILES: draw_files(); break;
    case S_VIEW: draw_view(); break;
    case S_APPS: draw_apps(); break;
    case S_UPDATE: draw_update(); break;
    default: break;
    }
}

// ---------------------------------------------------------------- control de pruebas (solo root)
typedef struct { char type; float x, y; } Fake;
static Fake fq[64]; static int fq_n; static pthread_mutex_t fq_m = PTHREAD_MUTEX_INITIALIZER;
static char shot_path[256]; static Screen goto_req = S_NCOUNT; static int fps_req = 0;
static void *ctl_thread(void *arg) {
    (void)arg;
    mkdir(CTL_DIR, 0755); chmod(CTL_DIR, 0755);
    mkfifo(CTL_FIFO, 0600); chmod(CTL_FIFO, 0600);
    FILE *f = fopen(CTL_FIFO, "r+");
    if (!f) return NULL;
    char l[300];
    while (fgets(l, sizeof l, f)) {
        float x, y; char p[256]; int s;
        pthread_mutex_lock(&fq_m);
        if (sscanf(l, "tap %f %f", &x, &y) == 2 && fq_n + 2 < 64) {
            fq[fq_n++] = (Fake){'d', x, y}; fq[fq_n++] = (Fake){'u', x, y};
        } else if (sscanf(l, "down %f %f", &x, &y) == 2 && fq_n < 64) fq[fq_n++] = (Fake){'d', x, y};
        else if (sscanf(l, "move %f %f", &x, &y) == 2 && fq_n < 64) fq[fq_n++] = (Fake){'m', x, y};
        else if (sscanf(l, "up %f %f", &x, &y) == 2 && fq_n < 64) fq[fq_n++] = (Fake){'u', x, y};
        else if (sscanf(l, "shot %255s", p) == 1) snprintf(shot_path, sizeof shot_path, "%s", p);
        else if (sscanf(l, "go %d", &s) == 1 && s >= 0 && s < S_NCOUNT) goto_req = s;
        else if (!strncmp(l, "fps", 3)) fps_req = 1;
        else if (sscanf(l, "vivas %d", &s) == 1) vivas_on = s;
        else if (sscanf(l, "demo %d", &s) == 1) demo = s;
        else if (sscanf(l, "theme %d", &s) == 1 && s >= 0 && s < NTHEMES) theme_idx = s;
        pthread_mutex_unlock(&fq_m);
        wake();
    }
    return NULL;
}
static void save_shot(void) {
    unsigned char *px = malloc(W * H * 4);
    glFinish();
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
    FILE *f = fopen(shot_path, "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", W, H);
        for (int y = H - 1; y >= 0; y--)
            for (int x = 0; x < W; x++) fwrite(px + (y * W + x) * 4, 1, 3, f);
        fclose(f);
    }
    free(px); shot_path[0] = 0;
}

// ---------------------------------------------------------------- modo espera de la terminal
// Espera a que se mantenga pulsado Bajar volumen o a que alguien escriba en la FIFO.
static int wait_return(void) {
    mkdir(CTL_DIR, 0755);
    unlink(BACK_FIFO); mkfifo(BACK_FIFO, 0666); chmod(BACK_FIFO, 0666);
    int ff = open(BACK_FIFO, O_RDWR | O_NONBLOCK);
    int vol = find_input(p_vol);
    struct pollfd p[2] = {{ff, POLLIN, 0}, {vol, POLLIN, 0}};
    double down_at = 0;
    for (;;) {
        int to = down_at > 0 ? 100 : -1;
        poll(p, vol >= 0 ? 2 : 1, to);
        if (p[0].revents & POLLIN) return 0;
        if (vol >= 0 && (p[1].revents & POLLIN)) {
            struct input_event ev;
            while (read(vol, &ev, sizeof ev) == sizeof ev)
                if (ev.type == EV_KEY) down_at = ev.value ? now_s() : 0;
        }
        if (down_at > 0 && now_s() - down_at > 1.2) return 0;
    }
}

// ---------------------------------------------------------------- espera sin gastar
static int touch_fd = -1;
// Con la pantalla apagada: doble toque para encenderla.
static void blank_wait(void) {
    static double last = 0; struct pollfd p[2] = {{wake_pipe[0], POLLIN, 0}, {touch_fd, POLLIN, 0}};
    poll(p, touch_fd >= 0 ? 2 : 1, 5000); char b[256];
    if (p[0].revents & POLLIN) while (read(wake_pipe[0], b, sizeof b) > 0) {}
    if (touch_fd < 0 || !(p[1].revents & POLLIN)) return;
    struct input_event ev[32]; ssize_t r;
    while ((r = read(touch_fd, ev, sizeof ev)) > 0)
        for (size_t i = 0; i < r / sizeof(ev[0]); i++)
            if (ev[i].type == EV_KEY && ev[i].code == BTN_TOUCH && ev[i].value == 1) {
                double t = now_s();
                if (t - last > .05 && t - last < .45) { SDL_Event e; SDL_zero(e); e.type = EV_POWER; SDL_PushEvent(&e); last = 0; }
                else last = t;
            }
}
static void wait_input(int ms, bool with_touch) {
    struct pollfd p[2] = {{wake_pipe[0], POLLIN, 0}, {touch_fd, POLLIN, 0}};
    poll(p, with_touch && touch_fd >= 0 ? 2 : 1, ms);
    char b[256];
    if (p[0].revents & POLLIN) while (read(wake_pipe[0], b, sizeof b) > 0) {}
    if (with_touch && touch_fd >= 0 && (p[1].revents & POLLIN)) while (read(touch_fd, b, sizeof b) > 0) {}
}

// ---------------------------------------------------------------- principal
int main(int argc, char **argv) {
    config_cargar();   // antes que nada: --esperar necesita la tecla de bajar volumen del perfil
    if (argc > 1 && !strcmp(argv[1], "--esperar")) return wait_return();
    if (argc > 3 && !strcmp(argv[1], "--pulsar")) {  // --pulsar <dispositivo> <tecla> [segundos]
        int fd = find_input(argv[2]); if (fd < 0) return 1;
        struct input_event ev; memset(&ev, 0, sizeof ev);
        for (int v = 1; v >= 0; v--) {
            ev.type = EV_KEY; ev.code = atoi(argv[3]); ev.value = v;
            if (write(fd, &ev, sizeof ev) < 0) return 2;
            ev.type = EV_SYN; ev.code = 0; ev.value = 0;
            if (write(fd, &ev, sizeof ev) < 0) return 2;
            if (v && argc > 4) usleep(atof(argv[4]) * 1e6);
        }
        return 0;
    }
    if (argc > 3 && !strcmp(argv[1], "--tocar")) {  // --tocar x y (por el dispositivo táctil real)
        int fd = find_input("Synaptics s2316b"); if (fd < 0) return 1;
        struct input_absinfo ax, ay;
        ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ax); ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ay);
        int x = atof(argv[2]) / 540 * ax.maximum, y = atof(argv[3]) / 960 * ay.maximum;
        struct input_event seq[] = {
            {.type = EV_ABS, .code = ABS_MT_SLOT, .value = 0},
            {.type = EV_ABS, .code = ABS_MT_TRACKING_ID, .value = 77},
            {.type = EV_ABS, .code = ABS_MT_POSITION_X, .value = x},
            {.type = EV_ABS, .code = ABS_MT_POSITION_Y, .value = y},
            {.type = EV_KEY, .code = BTN_TOUCH, .value = 1},
            {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
        };
        struct input_event up[] = {
            {.type = EV_ABS, .code = ABS_MT_TRACKING_ID, .value = -1},
            {.type = EV_KEY, .code = BTN_TOUCH, .value = 0},
            {.type = EV_SYN, .code = SYN_REPORT, .value = 0},
        };
        if (write(fd, seq, sizeof seq) < 0) return 2;
        usleep(120000);
        if (write(fd, up, sizeof up) < 0) return 2;
        return 0;
    }
    if (argc > 4 && !strcmp(argv[1], "--deslizar")) {  // --deslizar x y0 y1
        int fd = find_input("Synaptics s2316b"); if (fd < 0) return 1;
        struct input_absinfo ax, ay;
        ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ax); ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ay);
        float x = atof(argv[2]) / 540 * ax.maximum, y0 = atof(argv[3]) / 960 * ay.maximum, y1 = atof(argv[4]) / 960 * ay.maximum;
        struct input_event d[] = {
            {.type = EV_ABS, .code = ABS_MT_SLOT, .value = 0}, {.type = EV_ABS, .code = ABS_MT_TRACKING_ID, .value = 78},
            {.type = EV_ABS, .code = ABS_MT_POSITION_X, .value = (int)x}, {.type = EV_ABS, .code = ABS_MT_POSITION_Y, .value = (int)y0},
            {.type = EV_KEY, .code = BTN_TOUCH, .value = 1}, {.type = EV_SYN, .code = SYN_REPORT, .value = 0}};
        if (write(fd, d, sizeof d) < 0) return 2;
        for (int i = 1; i <= 20; i++) {
            usleep(16000);
            struct input_event m[] = {{.type = EV_ABS, .code = ABS_MT_POSITION_Y, .value = (int)(y0 + (y1 - y0) * i / 20)}, {.type = EV_SYN, .code = SYN_REPORT, .value = 0}};
            if (write(fd, m, sizeof m) < 0) return 2;
        }
        struct input_event up[] = {{.type = EV_ABS, .code = ABS_MT_TRACKING_ID, .value = -1}, {.type = EV_KEY, .code = BTN_TOUCH, .value = 0}, {.type = EV_SYN, .code = SYN_REPORT, .value = 0}};
        if (write(fd, up, sizeof up) < 0) return 2;
        return 0;
    }
    snprintf(auth_user, sizeof auth_user, "%s", usuario);
    snprintf(fcwd, sizeof fcwd, "%s/taller", home_u);
    const char *envu = getenv("FOSIL_USUARIO");
    if (envu) snprintf(auth_user, sizeof auth_user, "%s", envu);
    bool start_unlocked = getenv("FOSIL_SIN_BLOQUEO") != NULL;

    mkdir(STATE_DIR, 0775);
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    if (TTF_Init() != 0) { fprintf(stderr, "TTF\n"); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_DisplayMode dm; SDL_GetDesktopDisplayMode(0, &dm);
    SDL_Window *win = SDL_CreateWindow("Fósil", 0, 0, dm.w, dm.h, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
    if (!win) { fprintf(stderr, "ventana: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext gl = SDL_GL_CreateContext(win);
    if (!gl) { fprintf(stderr, "GL: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetSwapInterval(1);
    SDL_ShowCursor(SDL_DISABLE);
    SDL_GL_GetDrawableSize(win, &W, &H);
    glViewport(0, 0, W, H);
    fprintf(stderr, "Fósil: %dx%d %s, táctiles: %d\n", W, H, glGetString(GL_RENDERER), SDL_GetNumTouchDevices());
    gpu_init();
    EV_WAKE = SDL_RegisterEvents(2); EV_POWER = EV_WAKE + 1;
    config_load(); pin_load(); bl_init(); panel_init(win); if (*bl_path) bl_set(brightness); vib_init();
    if (pipe(wake_pipe) == 0) { fcntl(wake_pipe[0], F_SETFL, O_NONBLOCK); fcntl(wake_pipe[1], F_SETFL, O_NONBLOCK); }
    { int f = find_input("Synaptics s2316b");
      if (f >= 0) { char p[64]; snprintf(p, sizeof p, "/proc/self/fd/%d", f); touch_fd = open(p, O_RDONLY | O_NONBLOCK); close(f); } }
    pthread_t th;
    pthread_create(&th, NULL, stats_thread, NULL); pthread_detach(th);
    pthread_create(&th, NULL, chat_thread, NULL); pthread_detach(th);
    pthread_create(&th, NULL, power_thread, NULL); pthread_detach(th);
    pthread_create(&th, NULL, ctl_thread, NULL); pthread_detach(th);
    if (start_unlocked) screen = S_HOME;

    double t0 = now_s(); last_touch = t0;
    bool dirty = true; int last_min = -1, last_ver = -1;
    double fps_t = now_s(); int fps_n = 0;
    const double AMBIENT = 25, BLANK_AFTER = 75;
    float bg_time = 0; double last_frame = t0;

    for (;;) {
        double t = now_s();
        bool anim = (t - trans_start < TRANS) || (t - toast_t < 2.6) || scrolling || fabsf(scroll_v) > 2
                    || (t - last_touch < AMBIENT && !blanked && !kb_open && !T.metro) || (t - pin_shake < .5) || flip_tick(t) || (kb_open && t - last_touch < .35) || (kb_open && t - kb_open_t < .3) || ui.down
                    || (t - shake_start < .5);
        pthread_mutex_lock(&st.m); if (st.scanning && screen == S_NET) anim = true; pthread_mutex_unlock(&st.m);
        // Procesa como máximo un cambio de dedo por fotograma.
        ui.press = ui.release = false;
        SDL_Event e; bool got = false;
        if (blanked) blank_wait();
        else if (!anim && !dirty) { pthread_mutex_lock(&ch.m); bool b = ch.busy && screen == S_CHAT; pthread_mutex_unlock(&ch.m); wait_input(b ? 300 : 1000, true); if (b) dirty = true; }
        got = SDL_PollEvent(&e);
        bool fake_edge = false;
        pthread_mutex_lock(&fq_m);
        if (fq_n > 0 && !ui.press && !ui.release) {
            Fake f = fq[0]; memmove(fq, fq + 1, sizeof(Fake) * --fq_n);
            ui.x = f.x; ui.y = f.y; dirty = true; last_touch = now_s(); fake_edge = true;
            if (f.type == 'd') { ui.down_at = now_s(); ui.down = ui.press = true; ui.sx = f.x; ui.sy = f.y; ui.dragged = false; }
            if (f.type == 'u') { ui.down = false; ui.release = true; }
            if (f.type == 'm') { if (fabsf(ui.y - ui.sy) > 14 || fabsf(ui.x - ui.sx) > 14) ui.dragged = true; }
        }
        pthread_mutex_unlock(&fq_m);
        float last_y = ui.y;
        while (got && !fake_edge) {
            dirty = true;
            if (e.type == SDL_QUIT) _exit(0);
            if (e.type == EV_POWER) {
                blanked = !blanked;
                if (blanked) {
                    gracia_armar(); panel_set(false);
                    explicit_bzero(pw_buf, sizeof pw_buf); kb_open = false; auth_state = 0; net_prompt = false; lock_pin = false; explicit_bzero(pin_in, sizeof pin_in);
                    screen = S_LOCK; trans_start = -10;
                } else { panel_set(true); last_touch = now_s(); dirty = true; gracia_volver(); }
            }
            if (!blanked) {
                if (e.type == SDL_FINGERDOWN) {
                    ui.x = e.tfinger.x * W; ui.y = e.tfinger.y * H; ui.sx = ui.x; ui.sy = ui.y; last_y = ui.y;
                    ui.down_at = now_s(); ui.down = ui.press = true; ui.dragged = false; last_touch = now_s(); break;
                }
                if (e.type == SDL_FINGERUP) {
                    ui.x = e.tfinger.x * W; ui.y = e.tfinger.y * H; ui.down = false; ui.release = true; last_touch = now_s(); break;
                }
                if (e.type == SDL_FINGERMOTION) {
                    float nx = e.tfinger.x * W, ny = e.tfinger.y * H;
                    if (fabsf(ny - ui.sy) > 14 || fabsf(nx - ui.sx) > 14) ui.dragged = true;
                    if (scrolling && ui.dragged) {
                        float dy = ny - ui.y;
                        scroll_y[screen] = clampf(scroll_y[screen] - dy, 0, scroll_max);
                        scroll_v = -dy * 60;
                    }
                    ui.x = nx; ui.y = ny; last_touch = now_s();
                }
            }
            got = SDL_PollEvent(&e);
        }
        (void)last_y;
        t = now_s();
        if (blanked && notify_req) { blanked = false; panel_set(true); last_touch = t; dirty = true; }
        if (blanked) continue;
        if (ui.release) scrolling = false;
        if (!scrolling && fabsf(scroll_v) > 2 && screen < S_NCOUNT) {
            float dt = t - last_frame;
            scroll_y[screen] = clampf(scroll_y[screen] + scroll_v * dt, 0, scroll_max);
            scroll_v *= powf(.04f, dt);
        }
        if (auth_state == 2) {
            auth_state = 0; kb_open = false; explicit_bzero(pw_buf, sizeof pw_buf);
            if (screen == S_PIN) { pin_stage = 1; pin_in[0] = 0; }
            else { if (pin_fails) { pin_fails = 0; pin_write(); } go(S_HOME, 1); }
            dirty = true;
        } else if (auth_state == 3) { shake_start = t; pw_buf[0] = 0; vibrate(); auth_state = 4; dirty = true; }
        { int ps = pin_state; pin_poll(); if (ps >= 2) dirty = true; }
        if (notify_req) {   // un agente ha terminado: vibra y enciende la pantalla si estaba apagada
            notify_req = false; vibrate(); last_touch = now_s(); dirty = true;
            if (screen != S_CHAT) toast("%s ha terminado", notify_who);
        }
        if (app_done) { app_done = false; app_running = false; view_set(app_name, app_out, app_n, S_APPS); scroll_y[S_VIEW] = 0; go(S_VIEW, 1); dirty = true; }
        if (goto_req != S_NCOUNT) { screen = goto_req; trans_start = -10; kb_open = false; goto_req = S_NCOUNT; dirty = true; if (screen == S_NET) start_scan(); }

        // Apagar pantalla por inactividad
        pthread_mutex_lock(&ch.m); bool ch_busy = ch.busy; pthread_mutex_unlock(&ch.m);
        if (dirty_req) { dirty_req = false; dirty = true; }
        if (t - last_touch > (ch_busy ? 300 : BLANK_AFTER) && !shot_path[0] && !ui.down) {
            gracia_armar(); blanked = true; panel_set(false);
            explicit_bzero(pw_buf, sizeof pw_buf); kb_open = false; auth_state = 0; net_prompt = false; lock_pin = false; explicit_bzero(pin_in, sizeof pin_in);
            screen = S_LOCK; trans_start = -10;
            continue;
        }
        time_t now = time(NULL); struct tm m; localtime_r(&now, &m);
        if (m.tm_min != last_min) { last_min = m.tm_min; dirty = true; }
        pthread_mutex_lock(&st.m); int ver = st.version; pthread_mutex_unlock(&st.m);
        if (ver != last_ver) { last_ver = ver; if (screen != S_LOCK || !kb_open) dirty = true; }
        if (shot_path[0]) dirty = true;
        if (!dirty && !anim) continue;
        dirty = false;
        double fr_ini = now_s();

        // Solo avanza el fondo mientras hay actividad; luego se queda quieto y no gasta.
        float dtf = t - last_frame; last_frame = t;
        if (t - last_touch < AMBIENT) bg_time += dtf;
        background(bg_time + 40);

        double ts = t - trans_start;
        ui.enabled = true;
        if (ts < TRANS) {
            float k = ease_out(clampf(ts / TRANS, 0, 1));
            ui.enabled = false;
            if (T.metro) {  // torniquete: la página sale girando sobre el borde izquierdo y entra la nueva
                float q = clampf(ts / TRANS, 0, 1);
                OX = OY = 0;
                if (q < .5f) { float e = q * 2; TURN = trans_dir * e * e * 1.35f; GA = 1 - e * e; draw_screen(prev_screen); }
                else { float e = ease_out((q - .5f) * 2); TURN = -trans_dir * (1 - e) * 1.35f; GA = e; draw_screen(screen); }
                TURN = 0; GA = 1;
            } else {
            OX = -trans_dir * k * W * .28f; OY = 0; GA = 1 - k;
            draw_screen(prev_screen);
            OX = trans_dir * (1 - k) * W; GA = .3f + .7f * k;
            draw_screen(screen);
            OX = OY = 0; GA = 1;
            }
        } else {
            OX = OY = 0; GA = 1;
            draw_screen(screen);
        }
        // Aviso flotante
        if (t - toast_t < 2.6) {
            float a = clampf(fminf((t - toast_t) * 6, (2.6f - (t - toast_t)) * 4), 0, 1);
            GA = a;
            if (T.metro) {  // franja de aviso a todo lo ancho, como las notificaciones del móvil
                GA = 1; float ty = (a - 1) * 76;
                rr(0, ty, W, 76, 0, T.accent);
                text_fit(F_SANS, 21, toast_msg, 24, ty + 24, W - 48, rgba(1, 1, 1, 1));
            } else {
            int tw = 0; TTF_SizeUTF8(font(F_SANS, 21), toast_msg, &tw, NULL);
            float w = tw + 50 > W - 40 ? W - 40 : tw + 50;
            rr(W / 2 - w / 2, 28 + (1 - a) * -20, w, 58, 29, rgba(T.bg[0] * .6f, T.bg[1] * .6f, T.bg[2] * .6f, .95f));
            ring(W / 2 - w / 2, 28 + (1 - a) * -20, w, 58, 29, 1.3f, T.accent);
            text_fit(F_SANS, 21, toast_msg, W / 2 - w / 2 + 25, 44 + (1 - a) * -20, w - 50, T.text);
            }
            GA = 1;
        }
        if (!*bl_path && brightness < .99f) rr(-2, -2, W + 4, H + 4, 0, rgba(0, 0, 0, (1 - brightness) * .9f));
        if (ui.release) { ui.active = 0; dirty = true; }  // un fotograma más para reflejar lo que cambió el toque
        if (shot_path[0]) save_shot();
        double fr_dib = now_s();
        SDL_GL_SwapWindow(win);
        fps_n++;
        if (fps_req) {  // tiempos por fotograma: dibujo (CPU), total y peor salto entre fotogramas seguidos
            static double ult, sd, st_, peor, gpeor; static int nf, ng; double fin = now_s();
            bool giro = flip_i >= 0;
            sd += fr_dib - fr_ini; st_ += fin - fr_ini; nf++;
            if (fin - fr_ini > peor) peor = fin - fr_ini;
            if (giro && ult > 0 && fin - ult < .5) { ng++; if (fin - ult > gpeor) gpeor = fin - ult; }
            ult = fin;
            if (t - fps_t > 3) {
                fprintf(stderr, "fps %.1f dibujo %.1f ms total %.1f ms peor %.1f ms | giro %d fot peor salto %.1f ms\n", fps_n / (t - fps_t), sd / nf * 1e3, st_ / nf * 1e3, peor * 1e3, ng, gpeor * 1e3);
                fps_t = t; fps_n = 0; sd = st_ = peor = gpeor = 0; nf = ng = 0;
            }
        } else if (t - fps_t > 3) { fps_t = t; fps_n = 0; }
    }
    return 0;
}
