/*
 * code.c  –  Editor de código para MyOS
 *
 * Características:
 *   - Syntax highlighting para C (keywords, strings, comentarios, números)
 *   - Números de línea
 *   - Múltiples archivos (tabs)
 *   - Compilador simulado con mensajes de error/éxito
 *   - Atajos: Ctrl+S guardar, Ctrl+R compilar&ejecutar, Ctrl+N nuevo
 */

#include "code.h"
#include "framebuffer.h"
#include "gui.h"
#include "fs.h"
#include "keyboard.h"
#include "timer.h"
#include <stdint.h>

/* ---- Configuración ------------------------------------------- */
#define MAX_LINES    256
#define MAX_LINE_W   120
#define MAX_FILES    4
#define CHAR_W       9
#define CHAR_H       13
#define LINE_NUM_W   36     /* ancho del gutter de números de línea */
#define TAB_H        22
#define STATUS_H     20

/* ---- Colores ------------------------------------------------- */
#define C_BG         0x1e1e2e
#define C_GUTTER     0x181825
#define C_LINENUM    0x585b70
#define C_CURSOR_BG  0x313244
#define C_SEL        0x45475a
#define C_TEXT       0xcdd6f4
#define C_KEYWORD    0xcba6f7   /* púrpura */
#define C_STRING     0xa6e3a1   /* verde */
#define C_COMMENT    0x6c7086   /* gris */
#define C_NUMBER     0xfab387   /* naranja */
#define C_FUNCTION   0x89b4fa   /* azul */
#define C_OPERATOR   0x89dceb   /* cyan */
#define C_PREPROC    0xf38ba8   /* rojo */
#define C_TAB_ACT    0x313244
#define C_TAB_INI    0x181825
#define C_TAB_TXT    0xcdd6f4
#define C_STATUS_OK  0xa6e3a1
#define C_STATUS_ERR 0xf38ba8
#define C_STATUS_BG  0x11111b

static inline uint32_t HC(uint32_t hex){
    return fb_color((hex>>16)&0xFF,(hex>>8)&0xFF,hex&0xFF);
}

/* ---- Tipos --------------------------------------------------- */
typedef struct {
    char lines[MAX_LINES][MAX_LINE_W+1];
    int  line_count;
    char filename[32];
    int  modified;
} code_file_t;

/* ---- Estado -------------------------------------------------- */
static code_file_t files[MAX_FILES];
static int         file_count  = 0;
static int         cur_file    = 0;

/* Cursor y scroll */
static int cur_line  = 0;
static int cur_col   = 0;
static int scroll_y  = 0;    /* primera línea visible */
static int scroll_x  = 0;    /* primera columna visible */

/* Compilador */
static char status_msg[80];
static int  status_ok  = 1;
static int  compile_flash = 0;   /* contador para animación */

/* Ctrl */
static int ctrl_held = 0;

/* ---- Helpers ------------------------------------------------- */
static int slen(const char *s){ int n=0; while(s[n]) n++; return n; }
static void scpy(char *d,const char *s,int n){int i=0;while(s[i]&&i<n-1){d[i]=s[i];i++;}d[i]='\0';}
static int sncmp(const char *a,const char *b,int n){
    for(int i=0;i<n;i++) if(a[i]!=b[i]) return 1;
    return 0;
}

/* ---- Keywords C ---------------------------------------------- */
static const char *KEYWORDS[] = {
    "int","char","void","return","if","else","while","for","do",
    "struct","typedef","static","const","unsigned","long","short",
    "uint8_t","uint16_t","uint32_t","uint64_t","int32_t","int16_t",
    "break","continue","switch","case","default","extern","include",
    "define","ifdef","ifndef","endif","sizeof","NULL","0",
    0
};

static const char *KW_FLOW[] = {
    "return","if","else","while","for","do","break","continue",
    "switch","case","default",0
};

static int is_keyword(const char *s, int len){
    for(int k=0;KEYWORDS[k];k++){
        int kl=slen(KEYWORDS[k]);
        if(kl==len && !sncmp(s,KEYWORDS[k],len)) return 1;
    }
    return 0;
}
static int is_flow(const char *s,int len){
    for(int k=0;KW_FLOW[k];k++){
        int kl=slen(KW_FLOW[k]);
        if(kl==len && !sncmp(s,KW_FLOW[k],len)) return 1;
    }
    return 0;
}
static int is_alpha(char c){ return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'; }
static int is_digit(char c){ return c>='0'&&c<='9'; }
static int is_alnum(char c){ return is_alpha(c)||is_digit(c); }

/* ---- Dibujar una línea con syntax highlighting --------------- */
static void draw_code_line(int px, int py, int max_w,
                            const char *line, int line_idx,
                            int cur_l, int cur_c, int scr_x){
    int n    = slen(line);
    int draw = px;
    int i    = scr_x;
    int in_str  = 0;
    int in_char = 0;
    int in_comment = 0;

    /* Detectar si es línea de comentario de bloque (simplificado) */
    for(int k=0;k<n-1;k++) if(line[k]=='/'&&line[k+1]=='/') { in_comment=k; break; }
    int comment_start = in_comment;
    in_comment=0;

    /* Resaltar línea del cursor */
    if(line_idx==cur_l)
        fb_fill_rect(px-LINE_NUM_W,py,draw+max_w*CHAR_W+LINE_NUM_W,CHAR_H,HC(C_CURSOR_BG));

    while(i<n && draw<px+max_w*CHAR_W){
        char c=line[i];

        /* Cursor */
        if(line_idx==cur_l && i==cur_c){
            uint32_t blink=(timer_ticks()/30)%2?HC(0xf5c2e7):HC(C_TEXT);
            fb_fill_rect(draw,py,2,CHAR_H-2,blink);
        }

        uint32_t col=HC(C_TEXT);

        /* Comentario de línea */
        if(i==comment_start && comment_start>0){
            /* Dibujar resto en gris */
            while(i<n && draw<px+max_w*CHAR_W){
                char buf[2]={(char)line[i],'\0'};
                fb_draw_str(draw,py,buf,HC(C_COMMENT),HC(C_BG));
                draw+=CHAR_W; i++;
            }
            return;
        }
        if(i+1<n && line[i]=='/'&&line[i+1]=='/'){
            while(i<n && draw<px+max_w*CHAR_W){
                char buf[2]={(char)line[i],'\0'};
                fb_draw_str(draw,py,buf,HC(C_COMMENT),HC(C_BG));
                draw+=CHAR_W; i++;
            }
            return;
        }

        /* String / char */
        if(!in_str && !in_char && c=='"') in_str=1;
        else if(in_str && c=='"') { in_str=0; col=HC(C_STRING); }
        if(!in_str && !in_char && c=='\'') in_char=1;
        else if(in_char && c=='\'') { in_char=0; col=HC(C_STRING); }

        if(in_str||in_char){ col=HC(C_STRING); }

        /* Preprocessor */
        else if(i==0 && c=='#'){ col=HC(C_PREPROC); }
        else if(line[0]=='#'){ col=HC(C_PREPROC); }

        /* Número */
        else if(!in_str && is_digit(c) && (i==0 || !is_alpha(line[i-1]))){
            col=HC(C_NUMBER);
        }

        /* Operadores */
        else if(!in_str && (c=='+'||c=='-'||c=='*'||c=='/'||c=='='||
                             c=='<'||c=='>'||c=='!'||c=='&'||c=='|')){
            col=HC(C_OPERATOR);
        }

        /* Identificador / keyword */
        else if(!in_str && is_alpha(c)){
            int j=i; while(j<n && is_alnum(line[j])) j++;
            int wlen=j-i;
            if(is_flow(line+i,wlen))    col=HC(C_KEYWORD);
            else if(is_keyword(line+i,wlen)) col=HC(C_FUNCTION);
            /* Detectar función: nombre seguido de '(' */
            else if(j<n && line[j]=='(') col=HC(C_FUNCTION);
        }

        char buf[2]={(char)c,'\0'};
        uint32_t bg=(line_idx==cur_l)?HC(C_CURSOR_BG):HC(C_BG);
        fb_draw_str(draw,py,buf,col,bg);
        draw+=CHAR_W;
        i++;
    }
    /* Cursor al final de línea */
    if(line_idx==cur_l && cur_c>=n && draw<px+max_w*CHAR_W){
        uint32_t blink=(timer_ticks()/30)%2?HC(0xf5c2e7):HC(C_TEXT);
        fb_fill_rect(draw,py,2,CHAR_H-2,blink);
    }
}

/* ---- Compilador simulado ------------------------------------- */
/* Analiza el código y genera mensajes tipo gcc */
static void do_compile(void){
    if(!file_count){ scpy(status_msg,"No hay archivo abierto",80); status_ok=0; return; }
    code_file_t *f=&files[cur_file];

    /* Buscar errores comunes */
    int errors=0;
    static char errmsg[80];
    errmsg[0]='\0';

    for(int l=0;l<f->line_count;l++){
        const char *line=f->lines[l];
        int n=slen(line);

        /* Llave sin cerrar heurístico */
        int opens=0,closes=0;
        for(int k=0;k<n;k++){
            if(line[k]=='{') opens++;
            if(line[k]=='}') closes++;
        }
        (void)opens;(void)closes;

        /* Punto y coma faltante después de } en línea con ; */
        /* Declaración sin ; */
        if(n>3 && line[n-1]!=';' && line[n-1]!='{' && line[n-1]!='}' &&
           line[n-1]!='\\' && line[0]!='/' && line[0]!='#' &&
           line[n-1]!=')' && n>5 && errors==0){
            /* Solo marcar si parece una declaración */
            int has_eq=0; for(int k=0;k<n;k++) if(line[k]=='=') has_eq=1;
            if(has_eq && line[n-1]!=','){
                static char tmp[80];
                int ti=0;
                tmp[ti++]='['; tmp[ti++]='!'; tmp[ti++]=']'; tmp[ti++]=' ';
                /* filename */
                const char *fn=f->filename; int fi=0;
                while(fn[fi]&&ti<30) tmp[ti++]=fn[fi++];
                tmp[ti++]=':';
                /* line number */
                int ln=l+1,lp=0; char ln_r[8];
                if(!ln){ln_r[lp++]='0';}
                else{int lc=ln;while(lc>0){ln_r[lp++]='0'+lc%10;lc/=10;}}
                for(int k=lp-1;k>=0;k--) tmp[ti++]=ln_r[k];
                tmp[ti++]=' '; tmp[ti++]='f'; tmp[ti++]='a'; tmp[ti++]='l';
                tmp[ti++]='t'; tmp[ti++]='a'; tmp[ti++]=' ';
                tmp[ti++]='\''; tmp[ti++]=';'; tmp[ti++]='\'';
                tmp[ti]='\0';
                scpy(errmsg,tmp,80);
                errors++;
            }
        }
    }

    if(errors>0){
        scpy(status_msg,errmsg,80);
        status_ok=0;
    } else {
        /* "Compilación exitosa" */
        const char *fn=f->filename;
        static char ok[80];
        int oi=0;
        ok[oi++]='['; ok[oi++]='O'; ok[oi++]='K'; ok[oi++]=']'; ok[oi++]=' ';
        int fi=0; while(fn[fi]&&oi<30) ok[oi++]=fn[fi++];
        const char *suf=" compilado. 0 errores.";
        fi=0; while(suf[fi]&&oi<78) ok[oi++]=suf[fi++];
        ok[oi]='\0';
        scpy(status_msg,ok,80);
        status_ok=1;
        compile_flash=20;
    }
}

/* ---- Guardar ------------------------------------------------- */
static void do_save(void){
    if(!file_count) return;
    code_file_t *f=&files[cur_file];
    /* Serializar líneas a buffer plano */
    static char buf[MAX_LINES*(MAX_LINE_W+2)];
    int bp=0;
    for(int l=0;l<f->line_count&&bp<(int)sizeof(buf)-3;l++){
        int n=slen(f->lines[l]);
        for(int k=0;k<n&&bp<(int)sizeof(buf)-3;k++) buf[bp++]=f->lines[l][k];
        buf[bp++]='\n';
    }
    buf[bp]='\0';
    fs_write(f->filename,buf,bp);
    f->modified=0;
    scpy(status_msg,"Guardado.",80);
    status_ok=1;
}

/* ---- Nuevo archivo ------------------------------------------- */
static void new_file(const char *name){
    if(file_count>=MAX_FILES) file_count=0;
    code_file_t *f=&files[file_count];
    scpy(f->filename,name,32);
    f->line_count=1;
    f->lines[0][0]='\0';
    f->modified=0;
    cur_file=file_count;
    file_count++;
    cur_line=0; cur_col=0; scroll_y=0; scroll_x=0;
    scpy(status_msg,"Nuevo archivo.",80); status_ok=1;
}

/* ---- API pública --------------------------------------------- */
void code_init(void){
    file_count=0; cur_file=0; ctrl_held=0;
    scpy(status_msg,"Ctrl+N nuevo  Ctrl+S guardar  Ctrl+R compilar",80);
    status_ok=1;
    /* Archivo de ejemplo */
    new_file("main.c");
    const char *ex[] = {
        "#include <stdint.h>",
        "",
        "/* Programa de ejemplo */",
        "int sumar(int a, int b) {",
        "    return a + b;",
        "}",
        "",
        "void main(void) {",
        "    int x = sumar(3, 4);",
        "    int y = x * 2;",
        "}",
        0
    };
    code_file_t *f=&files[cur_file];
    f->line_count=0;
    for(int i=0;ex[i]&&f->line_count<MAX_LINES;i++){
        scpy(f->lines[f->line_count],ex[i],MAX_LINE_W);
        f->line_count++;
    }
}

void code_load(const char *filename){
    if(!filename||!filename[0]) return;
    /* Buscar si ya está abierto */
    for(int i=0;i<file_count;i++){
        if(!sncmp(files[i].filename,filename,32)){ cur_file=i; return; }
    }
    new_file(filename);
    /* Leer del FS */
    static char fbuf[MAX_LINES*(MAX_LINE_W+2)];
    int n=fs_read(filename,fbuf,sizeof(fbuf)-1);
    if(n<0) return;
    fbuf[n]='\0';
    /* Parsear líneas */
    code_file_t *f=&files[cur_file];
    f->line_count=0;
    int li=0,col=0;
    for(int i=0;i<=n&&f->line_count<MAX_LINES;i++){
        if(fbuf[i]=='\n'||fbuf[i]=='\0'){
            f->lines[f->line_count][col]='\0';
            f->line_count++;
            col=0; li++;
        } else if(col<MAX_LINE_W){
            f->lines[f->line_count][col++]=fbuf[i];
        }
    }
    if(f->line_count==0) f->line_count=1;
}

/* ---- Draw ---------------------------------------------------- */
void code_draw(int wx, int wy, int ww, int wh){
    int bx=wx+BORDER, by=wy+TITLEBAR_H;
    int bw=ww-BORDER*2, bh=wh-TITLEBAR_H-BORDER;
    if(bw<80||bh<60) return;

    /* Fondo */
    fb_fill_rect(bx,by,bw,bh,HC(C_BG));

    /* ---- Tabs ---- */
    fb_fill_rect(bx,by,bw,TAB_H,HC(0x11111b));
    for(int i=0;i<file_count;i++){
        int tw=slen(files[i].filename)*CHAR_W+16;
        int tx=bx+2+i*(tw+2);
        uint32_t tbg=HC(i==cur_file?C_TAB_ACT:C_TAB_INI);
        fb_fill_rect(tx,by+2,tw,TAB_H-2,tbg);
        if(i==cur_file)
            fb_fill_rect(tx,by+2,tw,2,HC(C_PREPROC));
        fb_draw_str(tx+8,by+5,files[i].filename,
                    HC(i==cur_file?C_TAB_TXT:C_LINENUM),tbg);
        if(files[i].modified){
            fb_draw_str(tx+tw-14,by+5,"*",HC(C_NUMBER),tbg);
        }
    }

    if(!file_count){ fb_draw_str(bx+20,by+TAB_H+20,"Ctrl+N para nuevo archivo",HC(C_LINENUM),HC(C_BG)); return; }

    code_file_t *f=&files[cur_file];

    /* ---- Gutter (números de línea) ---- */
    int code_y=by+TAB_H;
    int code_h=bh-TAB_H-STATUS_H;
    int code_x=bx+LINE_NUM_W;
    int code_w=bw-LINE_NUM_W;
    int visible=code_h/CHAR_H;

    fb_fill_rect(bx,code_y,LINE_NUM_W,code_h,HC(C_GUTTER));

    /* Separador gutter */
    fb_fill_rect(bx+LINE_NUM_W-1,code_y,1,code_h,HC(C_SEL));

    /* ---- Líneas de código ---- */
    fb_fill_rect(code_x,code_y,code_w,code_h,HC(C_BG));

    int max_chars=(code_w/CHAR_W);

    for(int l=0;l<visible;l++){
        int li=l+scroll_y;
        if(li>=f->line_count) break;
        int py=code_y+l*CHAR_H;

        /* Número de línea */
        int lnum=li+1;
        char lnbuf[8]; int lj=0;
        char rev[8]; int rk=0;
        if(!lnum){rev[rk++]='0';}
        else{int lc=lnum;while(lc>0){rev[rk++]='0'+lc%10;lc/=10;}}
        for(int k=rk-1;k>=0;k--) lnbuf[lj++]=rev[k];
        lnbuf[lj]='\0';
        int lnw=slen(lnbuf)*CHAR_W;
        uint32_t lnc=li==cur_line?HC(C_TEXT):HC(C_LINENUM);
        fb_draw_str(bx+LINE_NUM_W-lnw-4,py,lnbuf,lnc,HC(C_GUTTER));

        /* Código */
        draw_code_line(code_x,py,max_chars,
                       f->lines[li],li,
                       cur_line,cur_col,scroll_x);
    }

    /* ---- Barra de estado ---- */
    int sy=by+bh-STATUS_H;
    fb_fill_rect(bx,sy,bw,STATUS_H,HC(C_STATUS_BG));
    fb_draw_str(bx+4,sy+4,status_msg,
                status_ok?HC(C_STATUS_OK):HC(C_STATUS_ERR),
                HC(C_STATUS_BG));

    /* Posición cursor (derecha) */
    static char pos[24];
    int pi=0;
    pos[pi++]='L';
    int ln=cur_line+1; char lr[8]; int lri=0;
    if(!ln){lr[lri++]='0';}else{int lc=ln;while(lc>0){lr[lri++]='0'+lc%10;lc/=10;}}
    for(int k=lri-1;k>=0;k--) pos[pi++]=lr[k];
    pos[pi++]=':'; pos[pi++]='C';
    int cn=cur_col+1; char cr[8]; int cri=0;
    if(!cn){cr[cri++]='0';}else{int cc=cn;while(cc>0){cr[cri++]='0'+cc%10;cc/=10;}}
    for(int k=cri-1;k>=0;k--) pos[pi++]=cr[k];
    pos[pi]='\0';
    int pw=slen(pos)*CHAR_W;
    fb_draw_str(bx+bw-pw-8,sy+4,pos,HC(C_LINENUM),HC(C_STATUS_BG));

    if(compile_flash>0) compile_flash--;
}

/* ---- Key ---------------------------------------------------- */
void code_key(int ch){
    if(!file_count) return;
    code_file_t *f=&files[cur_file];

    /* Ctrl */
    if(ch==KEY_CTRL) { ctrl_held=1; return; }
    if(ch==KEY_CTRL_REL){ ctrl_held=0; return; }

    if(ctrl_held){
        if(ch=='s'||ch=='S'){ do_save(); return; }
        if(ch=='r'||ch=='R'){ do_compile(); return; }
        if(ch=='n'||ch=='N'){
            static int nc=1;
            static char nn[16]="nuevo";
            nn[5]='0'+nc%10; nn[6]='.'; nn[7]='c'; nn[8]='\0';
            nc++;
            new_file(nn);
            return;
        }
        if(ch=='1') { cur_file=0; return; }
        if(ch=='2' && file_count>1) { cur_file=1; return; }
        if(ch=='3' && file_count>2) { cur_file=2; return; }
        if(ch=='4' && file_count>3) { cur_file=3; return; }
        return;
    }

    /* Navegación */
    if(ch==KEY_UP){
        if(cur_line>0){ cur_line--;
            int ll=slen(f->lines[cur_line]);
            if(cur_col>ll) cur_col=ll;
        }
        if(cur_line<scroll_y) scroll_y=cur_line;
        return;
    }
    if(ch==KEY_DOWN){
        if(cur_line<f->line_count-1){ cur_line++;
            int ll=slen(f->lines[cur_line]);
            if(cur_col>ll) cur_col=ll;
        }
        return;
    }
    if(ch==KEY_LEFT){
        if(cur_col>0) cur_col--;
        else if(cur_line>0){ cur_line--; cur_col=slen(f->lines[cur_line]); }
        if(cur_line<scroll_y) scroll_y=cur_line;
        return;
    }
    if(ch==KEY_RIGHT){
        int ll=slen(f->lines[cur_line]);
        if(cur_col<ll) cur_col++;
        else if(cur_line<f->line_count-1){ cur_line++; cur_col=0; }
        return;
    }
    if(ch==KEY_HOME){ cur_col=0; return; }
    if(ch==KEY_END) { cur_col=slen(f->lines[cur_line]); return; }
    if(ch==KEY_PGUP){ cur_line-=10; if(cur_line<0) cur_line=0; scroll_y=cur_line; return; }
    if(ch==KEY_PGDN){ cur_line+=10; if(cur_line>=f->line_count) cur_line=f->line_count-1; return; }

    /* Enter */
    if(ch=='\r'||ch=='\n'){
        if(f->line_count>=MAX_LINES) return;
        /* Mover resto de línea a la siguiente */
        char *cl=f->lines[cur_line];
        int ll=slen(cl);
        /* Desplazar líneas hacia abajo */
        for(int i=f->line_count;i>cur_line+1;i--)
            scpy(f->lines[i],f->lines[i-1],MAX_LINE_W);
        f->line_count++;
        /* Nueva línea: desde cur_col en adelante */
        scpy(f->lines[cur_line+1], cl+cur_col, MAX_LINE_W);
        cl[cur_col]='\0';
        /* Auto-indent: copiar espacios iniciales */
        int spaces=0; while(cl[spaces]==' '||cl[spaces]=='\t') spaces++;
        char indent[MAX_LINE_W+1]; int ii=0;
        for(int k=0;k<spaces&&ii<MAX_LINE_W;k++) indent[ii++]=' ';
        /* Añadir indent a la nueva línea */
        static char tmp[MAX_LINE_W+1];
        scpy(tmp,f->lines[cur_line+1],MAX_LINE_W);
        for(int k=0;k<ii;k++) f->lines[cur_line+1][k]=indent[k];
        scpy(f->lines[cur_line+1]+ii,tmp,MAX_LINE_W-ii);
        cur_line++;
        cur_col=ii;
        f->modified=1;
        return;
    }

    /* Backspace */
    if(ch==8||ch==127){
        char *cl=f->lines[cur_line];
        if(cur_col>0){
            int ll=slen(cl);
            for(int k=cur_col-1;k<ll;k++) cl[k]=cl[k+1];
            cur_col--;
            f->modified=1;
        } else if(cur_line>0){
            /* Unir con línea anterior */
            int prev_len=slen(f->lines[cur_line-1]);
            int cur_len=slen(cl);
            if(prev_len+cur_len<MAX_LINE_W){
                scpy(f->lines[cur_line-1]+prev_len,cl,MAX_LINE_W-prev_len);
                for(int i=cur_line;i<f->line_count-1;i++)
                    scpy(f->lines[i],f->lines[i+1],MAX_LINE_W);
                f->lines[f->line_count-1][0]='\0';
                f->line_count--;
                cur_line--;
                cur_col=prev_len;
                f->modified=1;
            }
        }
        return;
    }

    /* Tab → 4 espacios */
    if(ch=='\t'){
        char *cl=f->lines[cur_line];
        int ll=slen(cl);
        if(ll+4>=MAX_LINE_W) return;
        for(int k=ll+4;k>=cur_col+4;k--) cl[k]=cl[k-4];
        for(int k=0;k<4;k++) cl[cur_col+k]=' ';
        cur_col+=4;
        f->modified=1;
        return;
    }

    /* Carácter normal */
    if(ch>=' ' && ch<127){
        char *cl=f->lines[cur_line];
        int ll=slen(cl);
        if(ll>=MAX_LINE_W) return;
        for(int k=ll+1;k>cur_col;k--) cl[k]=cl[k-1];
        cl[cur_col]=(char)ch;
        cur_col++;
        f->modified=1;

        /* Auto-cerrar llaves/paréntesis */
        char close=0;
        if(ch=='{') close='}';
        else if(ch=='(') close=')';
        else if(ch=='[') close=']';
        if(close){
            ll=slen(cl);
            for(int k=ll+1;k>cur_col;k--) cl[k]=cl[k-1];
            cl[cur_col]=close;
            /* No avanzar cursor */
        }
    }

    /* Scroll automático */
    int visible=20; /* aproximado */
    if(cur_line>=scroll_y+visible) scroll_y=cur_line-visible+1;
    if(cur_line<scroll_y) scroll_y=cur_line;
}

void code_mouse(int bx,int by,int bw,int bh,int px,int py,int click){
    (void)bw;(void)bh;
    if(!click||!file_count) return;

    /* Click en tabs */
    int tab_y=by+TITLEBAR_H;
    if(py>=tab_y&&py<tab_y+TAB_H){
        int tx=bx+BORDER+2;
        for(int i=0;i<file_count;i++){
            int tw=slen(files[i].filename)*CHAR_W+16;
            if(px>=tx&&px<tx+tw){ cur_file=i; return; }
            tx+=tw+2;
        }
        return;
    }

    /* Click en área de código */
    int code_y=tab_y+TAB_H;
    int code_x=bx+BORDER+LINE_NUM_W;
    if(py<code_y) return;

    int row=(py-code_y)/CHAR_H+scroll_y;
    int col=(px-code_x)/CHAR_W+scroll_x;
    if(col<0) col=0;
    code_file_t *f=&files[cur_file];
    if(row>=f->line_count) row=f->line_count-1;
    if(row<0) row=0;
    int ll=slen(f->lines[row]);
    if(col>ll) col=ll;
    cur_line=row; cur_col=col;
}
