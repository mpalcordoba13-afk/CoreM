/*
 * browser.c - Navegador de texto HTTP/HTTPS minimo para MyOS.
 *
 * Caracteristicas:
 *   - Barra de URL editable con cursor (Enter = navegar).
 *   - Descarga via net_http_get()  (HTTP/1.0,  puerto 80).
 *   - Descarga via net_https_get() (HTTPS/TLS, puerto 443).
 *   - Strip de tags HTML y expansion de entidades basicas
 *     (&amp; &lt; &gt; &nbsp; &quot;).
 *   - Area de texto scrollable (flechas UP/DOWN/PGUP/PGDN).
 *   - Historial hacia atras (tecla Backspace cuando la URL esta vacia).
 *   - Sin malloc: buffers estaticos.
 */
#include "browser.h"
#include "net.h"
#include "tls.h"
#include "framebuffer.h"
#include "keyboard.h"
#include "gui.h"
#include <stdint.h>

/* ---- Imagen inline ------------------------------------------------ */
#define IMG_BUF_SIZE (256*1024)
static uint8_t  img_buf[IMG_BUF_SIZE];
static uint32_t img_len   = 0;
static int      img_valid = 0;   /* 1 si img_buf contiene una imagen BMP válida */
static int      view_mode = 0;   /* 0=texto, 1=imagen */

#define MAX_URL     256
#define MAX_CONTENT 32768
#define MAX_LINES   512
#define CHARS_PER_LINE 80
#define HIST_SIZE   8

/* Fuente 9px de ancho x 16px de alto (la de fb_draw_char) */
#define CHAR_W 9
#define CHAR_H 12
#define URL_BAR_H 24
#define STATUS_H  14
#define PAD 4

static char url_buf[MAX_URL];
static int  url_len = 0;
static int  url_cursor = 0;
static int  url_focused = 1; /* 1=editando URL, 0=leyendo contenido */

static char content[MAX_CONTENT];
static int  content_len = 0;

/* Lineas del contenido renderizado (sin wrap todavia, truncado a CHARS_PER_LINE) */
static char lines[MAX_LINES][CHARS_PER_LINE+1];
static int  line_count = 0;

static int scroll = 0;      /* primera linea visible */
static int visible_lines;   /* se calcula en draw */

static char status[128];
static int loading = 0;
static int load_cancelled = 0; /* flag: el usuario pidio cancelar */

/* Historial */
static char history[HIST_SIZE][MAX_URL];
static int hist_top = 0;

/* ---- helpers de string -------------------------------------------- */
/* Constantes extra de teclado no en keyboard.h */
#define KEY_HOME 132
#define KEY_END  133
#define KEY_PGUP 134
#define KEY_PGDN 135

static int slen(const char *s){ int n=0; while(s[n]) n++; return n; }
static void scpy(char *d, const char *s, int max){
    int i=0; while(s[i]&&i<max-1){d[i]=s[i];i++;} d[i]='\0';
}

/* ---- strip HTML basico -------------------------------------------- */
/* Expande entidades HTML basicas en su lugar. */
static void expand_entities(char *s, int *lenp){
    static const struct { const char *ent; char ch; } ents[] = {
        {"&amp;", '&'},{"&lt;", '<'},{"&gt;", '>'},
        {"&nbsp;",' '},{"&quot;",'"'},{"&#39;", '\''},
        {0, 0}
    };
    int len = *lenp;
    for (int i=0;i<len;){
        if (s[i]=='&'){
            int matched=0;
            for (int e=0;ents[e].ent;e++){
                int elen=slen(ents[e].ent);
                if (i+elen<=len){
                    int ok=1;
                    for(int k=0;k<elen;k++) if(s[i+k]!=ents[e].ent[k]){ok=0;break;}
                    if(ok){
                        s[i]=ents[e].ch;
                        for(int k=i+1;k<len-elen+1;k++) s[k]=s[k+elen-1];
                        len-=elen-1;
                        matched=1; break;
                    }
                }
            }
            if(!matched) i++;
        } else i++;
    }
    s[len]='\0';
    *lenp=len;
}

/* Quita tags HTML y deja texto plano.
 * Convierte <br>, <p>, <div>, <h1>-<h6>, <li> en saltos de linea.
 * Devuelve la longitud del resultado en out. */
static int strip_html(const char *in, int ilen, char *out, int maxout){
    int o=0; int in_tag=0; int in_script=0;
    for (int i=0;i<ilen&&o<maxout-1;i++){
        if (in_script){
            /* Saltar hasta </script> o </style> */
            if (in[i]=='<' && i+8<ilen){
                int ok1=1,ok2=1;
                const char *sc="</script"; const char *st="</style";
                for(int k=0;k<8&&ok1;k++) if(in[i+k]!=sc[k]) ok1=0;
                for(int k=0;k<7&&ok2;k++) if(in[i+k]!=st[k]) ok2=0;
                if(ok1||ok2) in_script=0;
            }
            continue;
        }
        if (in_tag){
            if (in[i]=='>') in_tag=0;
            continue;
        }
        if (in[i]=='<'){
            /* Detectar tags de bloque que se convierten en newline */
            const char *p=in+i+1;
            int is_br   =(p[0]=='b'||p[0]=='B')&&(p[1]=='r'||p[1]=='R');
            int is_p    =(p[0]=='p'||p[0]=='P')&&(p[1]=='>'||p[1]==' '||p[1]=='/');
            int is_div  =(p[0]=='d'||p[0]=='D');
            int is_hdr  =(p[0]=='h'||p[0]=='H')&&p[1]>='1'&&p[1]<='6';
            int is_li   =(p[0]=='l'||p[0]=='L')&&(p[1]=='i'||p[1]=='I');
            int is_tr   =(p[0]=='t'||p[0]=='T')&&(p[1]=='r'||p[1]=='R');
            int is_scr  =(p[0]=='s'||p[0]=='S')&&(p[1]=='c'||p[1]=='C');
            int is_styl =(p[0]=='s'||p[0]=='S')&&(p[1]=='t'||p[1]=='T');
            if (is_scr||is_styl) in_script=1;
            if ((is_br||is_p||is_div||is_hdr||is_li||is_tr) && o>0 && out[o-1]!='\n')
                out[o++]='\n';
            if (is_hdr && o<maxout-1) out[o++]='\n'; /* doble espacio para h1-h6 */
            in_tag=1;
            continue;
        }
        /* Normalizar whitespace: tabs y CR a espacio, LF lo dejamos */
        char c=in[i];
        if (c=='\t'||c=='\r') c=' ';
        /* Colapsar espacios multiples */
        if (c==' ' && o>0 && out[o-1]==' ') continue;
        out[o++]=c;
    }
    out[o]='\0';
    expand_entities(out, &o);
    return o;
}

/* ---- wrapping de texto a lineas ----------------------------------- */
static void wrap_content(void){
    line_count = 0;
    int i = 0;
    while (i <= content_len && line_count < MAX_LINES){
        if (content[i]=='\n'||content[i]=='\0'){
            lines[line_count][0]='\0'; /* linea vacia por el \n */
            line_count++;
            i++;
            continue;
        }
        int col=0;
        while (content[i]&&content[i]!='\n'&&col<CHARS_PER_LINE){
            lines[line_count][col++]=content[i++];
        }
        lines[line_count][col]='\0';
        line_count++;
        /* si la linea continua, seguimos en la siguiente sin avanzar i por \n */
    }
}

/* ---- navegacion --------------------------------------------------- */
static void push_history(const char *u){
    scpy(history[hist_top % HIST_SIZE], u, MAX_URL);
    hist_top++;
}

static void navigate(const char *url){
    const char *p = url;
    while(*p==' ') p++;

    int use_https = 0;
    int is_search = 0;

    if (p[0]=='h'&&p[1]=='t'&&p[2]=='t'&&p[3]=='p'&&p[4]=='s'&&p[5]==':'&&p[6]=='/'&&p[7]=='/'){
        use_https=1; p+=8;
    } else if (p[0]=='h'&&p[1]=='t'&&p[2]=='t'&&p[3]=='p'&&p[4]==':'&&p[5]=='/'&&p[6]=='/'){
        p+=7;
    } else {
        int has_dot=0,has_space=0,has_slash=0;
        for(int i=0;p[i];i++){
            if(p[i]=='.') has_dot=1;
            if(p[i]==' ') has_space=1;
            if(p[i]=='/') has_slash=1;
        }
        if(!has_space&&(has_dot||has_slash)) use_https=1;
        else is_search=1;
    }

    if(is_search){
        /* Codificar query para DuckDuckGo HTML lite */
        static char encoded[256];
        int ei=0;
        const char *q=url; while(*q==' ') q++;
        while(*q&&ei<250){
            if(*q==' '){encoded[ei++]='+';}
            else if((*q>='A'&&*q<='Z')||(*q>='a'&&*q<='z')||(*q>='0'&&*q<='9')||
                    *q=='-'||*q=='_'||*q=='.'||*q=='~') encoded[ei++]=*q;
            else{
                static const char hx[]="0123456789ABCDEF";
                encoded[ei++]='%';
                encoded[ei++]=hx[((unsigned char)*q)>>4];
                encoded[ei++]=hx[((unsigned char)*q)&0xF];
            }
            q++;
        }
        encoded[ei]='\0';

        static char spath[280];
        int si=0;
        const char *pre="/html/?q=";
        while(pre[si]){spath[si]=pre[si];si++;}
        for(int k=0;encoded[k];k++) spath[si++]=encoded[k];
        spath[si]='\0';

        /* Actualizar URL visible */
        static char full_url[MAX_URL];
        int fi=0;
        const char *pfx="https://html.duckduckgo.com/html/?q=";
        while(pfx[fi]&&fi<MAX_URL-2) full_url[fi]=pfx[fi],fi++;
        for(int k=0;encoded[k]&&fi<MAX_URL-2;k++) full_url[fi++]=encoded[k];
        full_url[fi]='\0';
        scpy(url_buf,full_url,sizeof(url_buf));
        url_len=slen(url_buf);

        scpy(status,"Buscando...",sizeof(status));
        loading=1; load_cancelled=0; url_focused=0;
        static char raw[MAX_CONTENT];
        int n=net_https_get("html.duckduckgo.com",spath,raw,MAX_CONTENT-1);
        loading=0;
        if(n<0){
            scpy(content,
                "No se pudo conectar con el buscador.\n\n"
                "Verifica la conexion a Internet\n"
                "(icono WiFi en la barra inferior).\n",MAX_CONTENT);
            content_len=slen(content);
            scpy(status,"Error de conexion",sizeof(status));
        } else {
            img_valid=0; view_mode=0;
            content_len=strip_html(raw,n,content,MAX_CONTENT-1);
            scpy(status,"DuckDuckGo",sizeof(status));
        }
        push_history(url_buf);
        wrap_content(); scroll=0;
        return;
    }

    char host[128]; int hi=0;
    while (*p && *p!='/' && hi<127) host[hi++]=*p++;
    host[hi]='\0';
    const char *path = *p ? p : "/";

    if (use_https)
        scpy(status,"Conectando (HTTPS)...", sizeof(status));
    else
        scpy(status,"Conectando...", sizeof(status));
    loading=1;
    load_cancelled=0;
    /* Forzar refresco mostrando estado */
    url_focused=0;

    static char raw[MAX_CONTENT];
    int n = use_https
        ? net_https_get(host, path, raw, MAX_CONTENT-1)
        : net_http_get (host, path, raw, MAX_CONTENT-1);
    loading=0;
    if (n<0){
        scpy(content,
            "No se pudo cargar la pagina.\n"
            "================================\n\n"
            "Posibles causas:\n"
            "  - La red no esta configurada (abre el menu WiFi)\n"
            "  - El servidor no responde o la URL es incorrecta\n"
            "  - Sin conexion a Internet en este entorno\n\n"
            "Puedes:\n"
            "  - Cambiar la URL arriba y pulsar ENTER\n"
            "  - Pulsar R para reintentar\n"
            "  - Configurar la red con el icono WiFi en la barra inferior\n",
            MAX_CONTENT);
        content_len=slen(content);
        scpy(status, "Error: no se pudo conectar", sizeof(status));
        url_focused=1; /* devolver foco a la URL para facilitar edicion */
    } else {
        /* Detectar BMP por cabecera mágica */
        int is_bmp = (n >= 2 && (unsigned char)raw[0]=='B' && (unsigned char)raw[1]=='M');

        if (is_bmp){
            uint32_t isize = (uint32_t)n < IMG_BUF_SIZE ? (uint32_t)n : IMG_BUF_SIZE;
            for(uint32_t k=0;k<isize;k++) img_buf[k]=(uint8_t)raw[k];
            img_len   = isize;
            img_valid = 1;
            view_mode = 1;
            scpy(content,"[Imagen BMP]\n",MAX_CONTENT);
            content_len=slen(content);
            scpy(status,"Imagen BMP cargada",sizeof(status));
            push_history(url);
            wrap_content();
            scroll=0;
            loading=0;
            return;
        }

        img_valid = 0;
        view_mode = 0;

        /* Si parece HTML, lo procesamos; si no, texto crudo */
        int is_html=0;
        for(int i=0;i<n-4;i++) if(raw[i]=='<'&&(raw[i+1]=='h'||raw[i+1]=='H')) { is_html=1; break; }
        if(is_html)
            content_len = strip_html(raw, n, content, MAX_CONTENT-1);
        else {
            int cn=n<MAX_CONTENT-1?n:MAX_CONTENT-1;
            for(int i=0;i<cn;i++) content[i]=raw[i];
            content[cn]='\0';
            content_len=cn;
        }
        char nb[16]; int nn=n,np=0;
        char tmp[32]; tmp[0]='[';
        while(nn>0){nb[np++]='0'+nn%10;nn/=10;}
        /* invertir */
        for(int k=0;k<np;k++) tmp[1+k]=nb[np-1-k];
        tmp[1+np]=']'; tmp[2+np]='\0';
        scpy(status, tmp, sizeof(status));
    }
    push_history(url);
    wrap_content();
    scroll=0;
}

/* ---- API publica -------------------------------------------------- */
void browser_init(void){
    scpy(url_buf, "http://info.cern.ch/", MAX_URL);
    url_len = slen(url_buf);
    url_cursor = url_len;
    url_focused = 1;
    scpy(content,
        "Bienvenido al Navegador de MyOS\n"
        "================================\n\n"
        "Escribe una URL y presiona ENTER o click en IR.\n\n"
        "Sitios HTTP que funcionan bien:\n\n"
        "  http://info.cern.ch/       <- primera pag web del mundo\n"
        "  http://neverssl.com/       <- siempre HTTP puro\n"
        "  http://httpforever.com/\n\n"
        "Nota: la mayoria de sitios modernos usan HTTPS\n"
        "y van a redirigir. El navegador te avisa cuando pasa.\n\n"
        "Teclas:\n"
        "  ENTER / IR    navegar\n"
        "  UP / DOWN     scroll\n"
        "  TAB           enfocar barra URL\n"
        "  R             recargar\n",
        MAX_CONTENT);
    content_len = slen(content);
    wrap_content();
    scroll = 0;
    scpy(status, "Listo", sizeof(status));
}

void browser_draw(int wx, int wy, int ww, int wh){
    int bx = wx + BORDER;
    int by = wy + TITLEBAR_H;
    int bw = ww - BORDER*2;
    int bh = wh - TITLEBAR_H - BORDER;
    if (bw<80||bh<60) return;

    uint32_t BG     = fb_color(0x1a,0x1a,0x2e);
    uint32_t TXT    = fb_color(0xe0,0xe0,0xf0);
    uint32_t BAR_BG = fb_color(0x0f,0x0f,0x22);
    uint32_t BAR_FG = url_focused ? fb_color(0x88,0xcc,0xff) : fb_color(0x55,0x88,0xaa);
    uint32_t BAR_TXT= fb_color(0xf0,0xf0,0xff);
    uint32_t ST_BG  = fb_color(0x10,0x10,0x20);
    uint32_t ST_TXT = fb_color(0x66,0xcc,0x66);
    uint32_t LINK   = fb_color(0x44,0xaa,0xff);
    uint32_t CURSOR = fb_color(0xff,0xff,0x00);

    /* Fondo */
    fb_fill_rect(bx, by, bw, bh, BG);

    /* Barra URL */
    int url_y = by + PAD;
    fb_fill_rect(bx, url_y, bw, URL_BAR_H, BAR_BG);
    fb_draw_rect(bx, url_y, bw, URL_BAR_H, url_focused ? fb_color(0x44,0x88,0xff) : BAR_FG);

    /* Icono lupa */
    fb_draw_str(bx+PAD, url_y+6, "\x14", fb_color(0x88,0x88,0xaa), BAR_BG); /* placeholder */
    fb_fill_rect(bx+PAD+1, url_y+8, 8, 8, BAR_BG);
    fb_draw_rect(bx+PAD+1, url_y+8, 7, 7, fb_color(0x88,0x88,0xaa));
    fb_fill_rect(bx+PAD+7, url_y+14, 4, 2, fb_color(0x88,0x88,0xaa));

    int url_x    = bx + 22;
    int btn_w    = loading ? 56 : 32;
    int btn_x    = bx + bw - btn_w - 4;
    int url_area = btn_x - url_x - 4;
    int url_max_chars = url_area / CHAR_W;
    if(url_max_chars < 1) url_max_chars = 1;

    int show_from = url_cursor - url_max_chars + 4;
    if (show_from < 0) show_from = 0;
    static char url_disp[MAX_URL];
    int di=0;
    for(int i=show_from; i<url_len && di<url_max_chars; i++) url_disp[di++]=url_buf[i];
    url_disp[di]='\0';

    /* Placeholder si la barra está vacía y sin foco */
    if(url_len==0 && !url_focused){
        fb_draw_str(url_x, url_y+6, "Buscar o escribir URL...",
                    fb_color(0x66,0x66,0x88), BAR_BG);
    } else {
        fb_draw_str(url_x, url_y+6, url_disp, BAR_TXT, BAR_BG);
    }

    /* Cursor */
    if (url_focused){
        int cx = url_x + (url_cursor-show_from)*CHAR_W;
        fb_fill_rect(cx, url_y+4, 2, URL_BAR_H-8, CURSOR);
    }

    /* Botón IR / PARAR */
    if (loading){
        fb_fill_rect(btn_x, url_y+2, btn_w, URL_BAR_H-4, fb_color(0xaa,0x22,0x22));
        fb_draw_rect(btn_x, url_y+2, btn_w, URL_BAR_H-4, fb_color(0xff,0x44,0x44));
        fb_draw_str(btn_x+4, url_y+6, "PARAR", fb_color(0xff,0xff,0xff), fb_color(0xaa,0x22,0x22));
    } else {
        fb_fill_rect(btn_x, url_y+2, btn_w, URL_BAR_H-4, fb_color(0x22,0x55,0x99));
        fb_draw_rect(btn_x, url_y+2, btn_w, URL_BAR_H-4, fb_color(0x44,0x88,0xcc));
        fb_draw_str(btn_x+6, url_y+6, "IR", fb_color(0xff,0xff,0xff), fb_color(0x22,0x55,0x99));
    }

    /* Area de contenido */
    int ca_y = by + URL_BAR_H + PAD*2;
    int ca_h = bh - URL_BAR_H - STATUS_H - PAD*3;
    visible_lines = ca_h / CHAR_H;
    if (visible_lines < 1) visible_lines = 1;
    fb_fill_rect(bx, ca_y, bw, ca_h, BG);

    if (loading){
        fb_draw_str(bx+PAD, ca_y+PAD,    "Cargando pagina...", fb_color(0xff,0xcc,0x00), BG);
        fb_draw_str(bx+PAD, ca_y+PAD+20, "Pulsa el boton PARAR o Esc para cancelar.", fb_color(0xaa,0x88,0x66), BG);
        fb_draw_str(bx+PAD, ca_y+PAD+40, "También puedes editar la URL arriba.", fb_color(0x88,0x88,0xaa), BG);
    } else if (view_mode == 1 && img_valid){
        /* Mostrar imagen BMP escalada */
        fb_draw_bmp(bx+PAD, ca_y+PAD, bw-PAD*2, ca_h-PAD*2, img_buf, img_len);
    } else {
        int max_scroll = line_count - visible_lines;
        if (max_scroll < 0) max_scroll = 0;
        if (scroll > max_scroll) scroll = max_scroll;
        if (scroll < 0) scroll = 0;

        for (int i=0; i<visible_lines && (scroll+i)<line_count; i++){
            const char *ln = lines[scroll+i];
            /* Coloreado simple: lineas que empiezan con '=' o '-' son separadores */
            uint32_t lc = TXT;
            if (ln[0]=='='||ln[0]=='-'||ln[0]=='#') lc = fb_color(0x88,0xcc,0xff);
            else if (ln[0]=='h'&&ln[1]=='t'&&ln[2]=='t'&&ln[3]=='p') lc = LINK;
            fb_draw_str(bx+PAD, ca_y + i*CHAR_H, ln, lc, BG);
        }

        /* Scrollbar simple */
        if (line_count > visible_lines){
            int sb_x = bx+bw-8;
            fb_fill_rect(sb_x, ca_y, 6, ca_h, fb_color(0x22,0x22,0x33));
            int thumb_h = ca_h * visible_lines / line_count;
            if (thumb_h < 8) thumb_h = 8;
            int thumb_y = ca_y + (ca_h-thumb_h) * scroll / (line_count-visible_lines);
            fb_fill_rect(sb_x, thumb_y, 6, thumb_h, fb_color(0x44,0x66,0x99));
        }
    }

    /* Barra de estado */
    int st_y = by + bh - STATUS_H;
    fb_fill_rect(bx, st_y, bw, STATUS_H, ST_BG);
    fb_draw_str(bx+PAD, st_y+2, status, ST_TXT, ST_BG);

    /* Indicador IP */
    const char *ip = net_get_ip_str();
    int ip_len = slen(ip)*CHAR_W;
    fb_draw_str(bx+bw-ip_len-PAD, st_y+2, ip, fb_color(0x44,0x88,0x44), ST_BG);
}

void browser_key(int ch){
    /* Esc: cancelar carga o deseleccionar URL */
    if (ch == 27){
        if (loading){
            load_cancelled = 1;
            loading = 0;
            scpy(status, "Cancelado. Edita la URL y pulsa ENTER.", sizeof(status));
            url_focused = 1;
        } else {
            url_focused = 0;
        }
        return;
    }

    if (ch == '\t'){ /* TAB alterna foco */
        url_focused = !url_focused;
        return;
    }

    /* La barra URL SIEMPRE acepta escritura (incluso durante carga) */
    if (url_focused || loading){
        if (ch == '\n' || ch == '\r'){ /* navegar */
            url_buf[url_len]='\0';
            navigate(url_buf);
            url_focused=0;
        } else if (ch == 8 || ch == 127){ /* backspace */
            if (url_cursor>0){
                for(int i=url_cursor-1;i<url_len-1;i++) url_buf[i]=url_buf[i+1];
                url_len--; url_cursor--;
            }
        } else if (ch == KEY_LEFT){ if(url_cursor>0) url_cursor--;
        } else if (ch == KEY_RIGHT){ if(url_cursor<url_len) url_cursor++;
        } else if (ch == KEY_HOME){ url_cursor=0;
        } else if (ch == KEY_END){ url_cursor=url_len;
        } else if (ch >= 32 && ch < 127 && url_len<MAX_URL-1){
            /* Click en la URL activa edicion incluso durante carga */
            if (!url_focused) url_focused=1;
            for(int i=url_len;i>url_cursor;i--) url_buf[i]=url_buf[i-1];
            url_buf[url_cursor++]=ch;
            url_len++;
        }
        return;
    }

    /* Scroll del contenido */
    if      (ch==KEY_UP)   scroll--;
    else if (ch==KEY_DOWN) scroll++;
    else if (ch==KEY_PGUP) scroll-=visible_lines;
    else if (ch==KEY_PGDN) scroll+=visible_lines;
    else if (ch==KEY_HOME) scroll=0;
    else if (ch==KEY_END)  scroll=line_count-visible_lines;
    else if (ch=='\t')     url_focused=1;
    else if (ch=='r'||ch=='R'){ /* recargar */
        url_buf[url_len]='\0';
        navigate(url_buf);
    }
}

void browser_mouse(int bx_win, int by_win, int bw_win, int bh_win, int px, int py, int click){
    (void)bh_win;
    if (!click) return;
    /* Click en la barra URL */
    int url_y = by_win + PAD;
    int btn_x = bx_win + bw_win - BORDER*2 - 52;
    if (py >= url_y && py < url_y + URL_BAR_H){
        /* Boton IR / PARAR */
        if (px >= btn_x && px < btn_x + 48){
            if (loading){
                load_cancelled = 1;
                loading = 0;
                scpy(status, "Cancelado. Edita la URL y pulsa ENTER.", sizeof(status));
                url_focused = 1;
            } else {
                url_buf[url_len]='\0';
                navigate(url_buf);
                url_focused=0;
            }
        } else {
            url_focused = 1;
            url_cursor = url_len;
        }
        return;
    }
    /* Click en contenido: desenfocar URL */
    url_focused = 0;
}

void browser_load_bmp(const uint8_t *data, uint32_t len){
    uint32_t sz = len < IMG_BUF_SIZE ? len : IMG_BUF_SIZE;
    for(uint32_t k=0;k<sz;k++) img_buf[k]=data[k];
    img_len   = sz;
    img_valid = 1;
    view_mode = 1;
    scpy(content,"[Imagen BMP desde USB]\n",MAX_CONTENT);
    content_len=slen(content);
    scpy(url_buf,"usb://imagen.bmp",sizeof(url_buf));
    url_len=slen(url_buf);
}
