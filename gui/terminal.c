#include "terminal.h"
#include "gui.h"
#include "framebuffer.h"
#include "fs.h"
#include "rtc.h"
#include "users.h"
#include "sound.h"
#include "timer.h"
#include "usb_printer.h"
#include "usb.h"
#include "usb_ehci.h"
#include "usb_msd.h"
#include "fat32.h"
#include "pci.h"
#include "net.h"
#include "rtl8139.h"
#include <stdint.h>

#define TERM_LINES 13
#define LINE_LEN   56
#define INPUT_LEN  60

static char lines[TERM_LINES][LINE_LEN];
static char input[INPUT_LEN];
static int  input_len = 0;

static int slen(const char *s){int n=0;while(s[n])n++;return n;}
static void scpy(char *d,const char *s,int max){int i=0;while(s[i]&&i<max-1){d[i]=s[i];i++;}d[i]='\0';}
static int seq(const char *a,const char *b){while(*a&&*b){if(*a!=*b)return 0;a++;b++;}return *a==*b;}

static int parse_ip(const char *s, uint8_t out[4]) {
    int part=0, val=0, got=0, i=0;
    while (1) {
        if (s[i]>='0' && s[i]<='9') { val = val*10 + (s[i]-'0'); got=1; i++; }
        else if (s[i]=='.') { if (!got || part>=3 || val>255) return 0; out[part++]=(uint8_t)val; val=0; got=0; i++; }
        else if (s[i]=='\0') { if (!got || part!=3 || val>255) return 0; out[part]=(uint8_t)val; return 1; }
        else return 0;
    }
}

static const char HEXD[] = "0123456789abcdef";
static void byte_to_hex2(uint8_t b, char *out) { out[0]=HEXD[b>>4]; out[1]=HEXD[b&0xF]; }

static void itoa10(int n, char *buf) {
    int i=0;
    if (n==0) { buf[i++]='0'; }
    else {
        if (n<0) { buf[i++]='-'; n=-n; }
        char tmp[10]; int j=0;
        while(n>0){tmp[j++]='0'+n%10;n/=10;}
        while(j>0)buf[i++]=tmp[--j];
    }
    buf[i]='\0';
}

static void push_line(const char *s) {
    int len = slen(s);
    if (len == 0) {
        for (int i=0;i<TERM_LINES-1;i++) scpy(lines[i],lines[i+1],LINE_LEN);
        lines[TERM_LINES-1][0]='\0';
        return;
    }
    int pos=0;
    while (pos<len) {
        char buf[LINE_LEN]; int n=0;
        while (pos<len && s[pos]!='\n' && n<LINE_LEN-1) buf[n++]=s[pos++];
        buf[n]='\0';
        if (pos<len && s[pos]=='\n') pos++;
        for (int i=0;i<TERM_LINES-1;i++) scpy(lines[i],lines[i+1],LINE_LEN);
        scpy(lines[TERM_LINES-1],buf,LINE_LEN);
    }
}

static const char *skip_spaces(const char *s) {
    while (*s==' ' || *s=='\t') s++;
    return s;
}

static int parse_http_url(const char *url, char *host, int host_max, char *path, int path_max) {
    if (!seq(url, "http://")) return 0;
    const char *p = url + 7;
    int hi = 0;
    while (*p && *p!='/') {
        if (hi < host_max-1) host[hi++] = *p;
        p++;
    }
    host[hi] = '\0';
    if (hi == 0) return 0;
    if (*p == '\0') {
        if (path_max < 2) return 0;
        path[0] = '/'; path[1] = '\0';
    } else {
        int pi = 0;
        while (*p && pi < path_max-1) path[pi++] = *p++;
        path[pi] = '\0';
    }
    return 1;
}

#define SCRIPT_VARS 8
static char script_var_names[SCRIPT_VARS][12];
static char script_var_values[SCRIPT_VARS][32];

static void script_set_var(const char *name, const char *value) {
    int idx = -1;
    for (int i=0;i<SCRIPT_VARS;i++) {
        if (script_var_names[i][0] && seq(script_var_names[i], name)) { idx = i; break; }
        if (idx < 0 && script_var_names[i][0] == '\0') idx = i;
    }
    if (idx < 0) return;
    scpy(script_var_names[idx], name, sizeof(script_var_names[0]));
    scpy(script_var_values[idx], value, sizeof(script_var_values[0]));
}

static const char *script_get_var(const char *name) {
    for (int i=0;i<SCRIPT_VARS;i++) if (script_var_names[i][0] && seq(script_var_names[i], name)) return script_var_values[i];
    return "";
}

static void run_command(char *cmd);

static void script_expand_text(const char *src, char *dst, int maxlen) {
    int di = 0;
    for (int si = 0; src[si] && di < maxlen-1; si++) {
        if (src[si] == '$' && src[si+1]) {
            si++;
            char name[12]; int ni = 0;
            while (src[si] && src[si] != ' ' && src[si] != '\t' && ni < (int)(sizeof(name)-1)) name[ni++] = src[si++];
            name[ni] = '\0';
            const char *val = script_get_var(name);
            for (int k = 0; val[k] && di < maxlen-1; k++) dst[di++] = val[k];
            si--;
        } else {
            dst[di++] = src[si];
        }
    }
    dst[di] = '\0';
}

static int script_eval_condition(const char *line) {
    char lhs[32], rhs[32], op[3];
    int i=0, j=0;
    line = skip_spaces(line);
    while (line[i] && line[i] != ' ' && line[i] != '\t' && line[i] != '=' && line[i] != '!') {
        if (j < (int)sizeof(lhs)-1) lhs[j++] = line[i];
        i++;
    }
    lhs[j] = '\0';
    line = skip_spaces(line + i);
    i = 0; j = 0;
    while (line[i] && (line[i]=='=' || line[i]=='!') && j < 2) op[j++] = line[i++];
    op[j] = '\0';
    line = skip_spaces(line + i);
    j = 0;
    while (line[j] && line[j] != ' ' && line[j] != '\t') {
        rhs[j] = line[j];
        j++;
    }
    rhs[j] = '\0';
    if (lhs[0] == '$') {
        const char *val = script_get_var(lhs+1);
        scpy(lhs, val, sizeof(lhs));
    }
    if (rhs[0] == '$') {
        const char *val = script_get_var(rhs+1);
        scpy(rhs, val, sizeof(rhs));
    }
    if (seq(op, "==")) return seq(lhs, rhs);
    if (seq(op, "!=")) return !seq(lhs, rhs);
    return 0;
}

static void script_execute_file(const char *name) {
    char buf[FS_DATA_LEN];
    int n = fs_read(name, buf, sizeof(buf));
    if (n < 0) { push_line("script no encontrado"); return; }
    buf[n] = '\0';
    for (int i=0;i<SCRIPT_VARS;i++) script_var_names[i][0] = '\0';
    int pos = 0;
    int skip_until_endif = 0;
    while (pos < n) {
        char line[LINE_LEN]; int li = 0;
        while (pos < n && buf[pos] != '\n' && li < LINE_LEN-1) line[li++] = buf[pos++];
        if (pos < n && buf[pos] == '\n') pos++;
        while (li > 0 && (line[li-1] == '\r' || line[li-1] == '\n')) li--;
        line[li] = '\0';
        const char *text = skip_spaces(line);
        if (text[0] == '\0' || text[0] == '#') continue;
        char cmd[16]; int ci = 0;
        int ti = 0;
        while (text[ti] && text[ti] != ' ' && text[ti] != '\t' && ci < (int)sizeof(cmd)-1) cmd[ci++] = text[ti++];
        cmd[ci] = '\0';
        const char *args = skip_spaces(text + ti);
        if (skip_until_endif) {
            if (seq(cmd, "endif")) skip_until_endif = 0;
            continue;
        }
        if (seq(cmd, "print") || seq(cmd, "echo")) {
            char expanded[LINE_LEN];
            script_expand_text(args, expanded, sizeof(expanded));
            push_line(expanded);
        } else if (seq(cmd, "set")) {
            char name[12], value[32]; int vi=0, wi=0;
            while (args[wi] && args[wi] != ' ' && args[wi] != '\t' && vi < (int)sizeof(name)-1) name[vi++] = args[wi++];
            name[vi] = '\0';
            args = skip_spaces(args + wi);
            while (*args && vi < (int)sizeof(value)-1) value[vi++] = *args++;
            value[vi] = '\0';
            script_set_var(name, value);
        } else if (seq(cmd, "run")) {
            char expanded[LINE_LEN];
            script_expand_text(args, expanded, sizeof(expanded));
            run_command(expanded);
        } else if (seq(cmd, "if")) {
            if (!script_eval_condition(args)) skip_until_endif = 1;
        } else if (seq(cmd, "endif")) {
            /* nada */
        } else if (seq(cmd, "sleep")) {
            int ms = 0; while (*args >= '0' && *args <= '9') { ms = ms*10 + (*args - '0'); args++; }
            timer_sleep(ms);
        } else {
            push_line("Comando de script no reconocido");
        }
    }
}

static int pkg_parse_and_install(const char *buf, int len) {
    int pos = 0;
    int installed = 0;
    while (pos < len) {
        while (pos < len && buf[pos] != 'F') pos++;
        if (pos >= len || !seq(buf+pos, "FILE:")) break;
        pos += 5;
        while (pos < len && (buf[pos]==' '||buf[pos]=='\t')) pos++;
        char name[FS_NAME_LEN]; int ni = 0;
        while (pos < len && buf[pos] != '\n' && buf[pos] != '\r' && ni < FS_NAME_LEN-1) name[ni++] = buf[pos++];
        name[ni] = '\0';
        while (pos < len && buf[pos] != '\n') pos++;
        if (pos < len && buf[pos] == '\n') pos++;
        if (!seq(buf+pos, "DATA:")) break;
        while (pos < len && buf[pos] != '\n') pos++;
        if (pos < len && buf[pos] == '\n') pos++;
        char data[FS_DATA_LEN]; int di = 0;
        while (pos < len) {
            if (seq(buf+pos, "END\n") || (seq(buf+pos, "END") && (pos+3==len || buf[pos+3]=='\n' || buf[pos+3]=='\r'))) {
                if (seq(buf+pos, "END\n")) pos += 4;
                else pos += 3;
                break;
            }
            if (di < FS_DATA_LEN-1) data[di++] = buf[pos];
            pos++;
        }
        data[di] = '\0';
        fs_write(name, data, di);
        installed++;
    }
    return installed;
}

static void pkg_install_url(const char *url) {
    char host[64], path[128];
    if (!parse_http_url(url, host, sizeof(host), path, sizeof(path))) {
        push_line("URL invalida. Ej: http://host/paquete.txt");
        return;
    }
    static char pkgbuf[2048];
    int n = net_http_get(host, path, pkgbuf, sizeof(pkgbuf));
    if (n < 0) { push_line("Error al descargar paquete"); return; }
    int installed = pkg_parse_and_install(pkgbuf, n);
    if (installed <= 0) push_line("Paquete invalido o sin archivos");
    else {
        char msg[64]; int p=0;
        const char *pre="Paquete instalado: ";
        for (int i=0; pre[i]; i++) msg[p++]=pre[i];
        char nb[12]; int ni=0;
        int x = installed;
        if (x==0) nb[ni++]='0'; else { char tmp[12]; int ti=0; while(x>0){tmp[ti++]='0'+(x%10);x/=10;} while(ti>0) nb[ni++]=tmp[--ti]; }
        for (int i=0; i<ni; i++) msg[p++]=nb[i];
        msg[p++]=' '; msg[p++]='f'; msg[p++]='i'; msg[p++]='c'; msg[p++]='h'; msg[p++]='e'; msg[p++]='r'; msg[p++]='o'; msg[p++]='s'; msg[p]='\0';
        push_line(msg);
    }
}

static void pkg_list(void) {
    int count = 0;
    char buf[48];
    for (int i = 0; ; i++) {
        char name[FS_NAME_LEN]; int size;
        if (!fs_get_entry(i, name, &size)) break;
        int p=0;
        for (int j=0; name[j]; j++) buf[p++]=name[j];
        buf[p++]=' '; buf[p++]='('; char nb[12]; int ni=0; int x=size;
        if (x==0) nb[ni++]='0'; else { char tmp[12]; int ti=0; while(x>0){tmp[ti++]='0'+(x%10);x/=10;} while(ti>0) nb[ni++]=tmp[--ti]; }
        for (int j=0; j<ni; j++) buf[p++]=nb[j];
        buf[p++]='B'; buf[p++]=')'; buf[p]='\0';
        push_line(buf);
        count++;
    }
    if (count == 0) push_line("No hay archivos instalados");
}

static void term_clear(void) {
    for (int i=0;i<TERM_LINES;i++) lines[i][0]='\0';
}

static void run_command(char *cmd) {
    char args[5][32]; int argc=0;
    int i=0;
    while (cmd[i] && argc<5) {
        while (cmd[i]==' ') i++;
        if (!cmd[i]) break;
        int j=0;
        while (cmd[i] && cmd[i]!=' ' && j<31) args[argc][j++]=cmd[i++];
        args[argc][j]='\0';
        argc++;
    }
    if (argc==0) return;

    if (seq(args[0],"help")) {
        push_line("Comandos disponibles:");
        push_line("help ls cat echo clear date");
        push_line("whoami sysinfo mem uptime");
        push_line("passwd touch write rm");
        push_line("settime setdate beep reboot");
        push_line("printer print <texto>");
        push_line("ifconfig setip ping nslookup wget <url>");
        push_line("printer print <texto>  usbinfo");
    }
    else if (seq(args[0],"clear")||seq(args[0],"cls")) {
        term_clear();
    }
    else if (seq(args[0],"ls")) {
        char name[FS_NAME_LEN]; int size; int n=0;
        for (int k=0;;k++) {
            if (!fs_get_entry(k,name,&size)) break;
            char buf[48]; int p=0;
            for(int q=0;name[q];q++) buf[p++]=name[q];
            buf[p++]=' '; buf[p++]=' '; buf[p++]='(';
            char nb[12]; itoa10(size,nb);
            for(int q=0;nb[q];q++) buf[p++]=nb[q];
            buf[p++]='B'; buf[p++]=')'; buf[p]='\0';
            push_line(buf);
            n++;
        }
        if (n==0) push_line("(sin archivos)");
    }
    else if (seq(args[0],"cat")) {
        if (argc<2) push_line("uso: cat <archivo>");
        else {
            char buf[FS_DATA_LEN];
            int r = fs_read(args[1],buf,sizeof(buf));
            if (r<0) push_line("archivo no encontrado");
            else if (r==0) push_line("(vacio)");
            else push_line(buf);
        }
    }
    else if (seq(args[0],"echo")) {
        char out[200]; int p=0;
        for (int a=1;a<argc;a++) {
            for(int q=0;args[a][q];q++) out[p++]=args[a][q];
            if (a<argc-1) out[p++]=' ';
        }
        out[p]='\0';
        push_line(out);
    }
    else if (seq(args[0],"date")) {
        rtc_time_t t; rtc_read(&t);
        char buf[40]; int p=0;
        char nb[12];
        itoa10(t.day,nb); for(int q=0;nb[q];q++)buf[p++]=nb[q]; buf[p++]='/';
        itoa10(t.month,nb); for(int q=0;nb[q];q++)buf[p++]=nb[q]; buf[p++]='/';
        itoa10(t.year,nb); for(int q=0;nb[q];q++)buf[p++]=nb[q]; buf[p++]=' ';
        itoa10(t.hour,nb); for(int q=0;nb[q];q++)buf[p++]=nb[q]; buf[p++]=':';
        itoa10(t.min,nb); for(int q=0;nb[q];q++)buf[p++]=nb[q]; buf[p++]=':';
        itoa10(t.sec,nb); for(int q=0;nb[q];q++)buf[p++]=nb[q];
        buf[p]='\0';
        push_line(buf);
    }
    else if (seq(args[0],"whoami")) {
        push_line(users_get_current());
    }
    else if (seq(args[0],"sysinfo")) {
        push_line("CoreM v1.0 - x86 32-bit");
        push_line("Modo protegido, VBE 1280x720 32bpp");
        push_line("RAM asignada: 64 MB");
        push_line("Mouse PS/2 + USB tablet, audio PC speaker");
    }
    else if (seq(args[0],"mem")) {
        char buf[48]; int p=0;
        char nb[12]; itoa10(fs_total_used(),nb);
        for(int q=0;nb[q];q++) buf[p++]=nb[q];
        const char *suf=" / 16384 bytes usados (FS)";
        for(int q=0;suf[q];q++) buf[p++]=suf[q];
        buf[p]='\0';
        push_line(buf);
    }
    else if (seq(args[0],"uptime")) {
        char buf[32]; int p=0;
        char nb[12]; itoa10((int)timer_seconds(),nb);
        for(int q=0;nb[q];q++) buf[p++]=nb[q];
        const char *suf=" segundos desde el arranque";
        for(int q=0;suf[q];q++) buf[p++]=suf[q];
        buf[p]='\0';
        push_line(buf);
    }
    else if (seq(args[0],"passwd")) {
        if (argc<2) push_line("uso: passwd <nueva_clave>");
        else {
            users_set_password(users_get_current(),args[1]);
            push_line("Clave actualizada correctamente");
        }
    }
    else if (seq(args[0],"touch")) {
        if (argc<2) push_line("uso: touch <archivo>");
        else { fs_write(args[1],"",0); push_line("archivo creado"); }
    }
    else if (seq(args[0],"write")) {
        if (argc<3) push_line("uso: write <archivo> <texto>");
        else {
            char out[200]; int p=0;
            for (int a=2;a<argc;a++) {
                for(int q=0;args[a][q];q++) out[p++]=args[a][q];
                if (a<argc-1) out[p++]=' ';
            }
            out[p]='\0';
            fs_write(args[1],out,p);
            push_line("archivo guardado");
        }
    }
    else if (seq(args[0],"rm")) {
        if (argc<2) push_line("uso: rm <archivo>");
        else {
            if (fs_delete(args[1])==0) push_line("archivo eliminado");
            else push_line("archivo no encontrado");
        }
    }
    else if (seq(args[0],"settime")) {
        if (argc<4) push_line("uso: settime HH MM SS");
        else {
            int h=0,m=0,s=0;
            for(int q=0;args[1][q];q++) h=h*10+(args[1][q]-'0');
            for(int q=0;args[2][q];q++) m=m*10+(args[2][q]-'0');
            for(int q=0;args[3][q];q++) s=s*10+(args[3][q]-'0');
            rtc_set_time((uint8_t)h,(uint8_t)m,(uint8_t)s);
            push_line("hora actualizada");
        }
    }
    else if (seq(args[0],"setdate")) {
        if (argc<4) push_line("uso: setdate DD MM AAAA");
        else {
            int d=0,mo=0,y=0;
            for(int q=0;args[1][q];q++) d=d*10+(args[1][q]-'0');
            for(int q=0;args[2][q];q++) mo=mo*10+(args[2][q]-'0');
            for(int q=0;args[3][q];q++) y=y*10+(args[3][q]-'0');
            rtc_set_date((uint8_t)d,(uint8_t)mo,(uint16_t)y);
            push_line("fecha actualizada");
        }
    }
    else if (seq(args[0],"beep")) {
        sound_beep(880,150);
    }
    else if (seq(args[0],"printer")) {
        if (usb_printer_init()) push_line("Impresora USB detectada y lista");
        else push_line("No se encontro ninguna impresora USB");
    }
    else if (seq(args[0],"print")) {
        if (argc<2) { push_line("uso: print <texto>"); }
        else if (!usb_printer_present() && !usb_printer_init()) {
            push_line("No se encontro ninguna impresora USB");
            push_line("(probá 'printer' para reintentar)");
        }
        else {
            char out[200]; int p=0;
            for (int a=1;a<argc;a++) {
                for(int q=0;args[a][q];q++) out[p++]=args[a][q];
                if (a<argc-1) out[p++]=' ';
            }
            out[p]='\0';
            if (usb_printer_print_text(out)==0) push_line("Enviado a la impresora");
            else push_line("Error al imprimir");
        }
    }
    else if (seq(args[0],"ifconfig")) {
        if (!rtl8139_present()) {
            push_line("No se detecto tarjeta de red (RTL8139)");
        } else {
            uint8_t m[6]; rtl8139_get_mac(m);
            char buf[40]; int p=0;
            const char *pre="MAC: ";
            for(int q=0;pre[q];q++) buf[p++]=pre[q];
            for(int i=0;i<6;i++){ char h[2]; byte_to_hex2(m[i],h); buf[p++]=h[0]; buf[p++]=h[1]; if(i<5) buf[p++]=':'; }
            buf[p]='\0';
            push_line(buf);
            if (net_is_configured()) {
                char ipbuf[24]; int q=0;
                const char *ipre="IP: ";
                for(int k=0;ipre[k];k++) ipbuf[q++]=ipre[k];
                const char *ip=net_get_ip_str();
                for(int k=0;ip[k];k++) ipbuf[q++]=ip[k];
                ipbuf[q]='\0';
                push_line(ipbuf);
            } else {
                push_line("Sin IP configurada (usa: setip <ip> <gateway>)");
            }
        }
    }
    else if (seq(args[0],"setip")) {
        if (argc<4) { push_line("uso: setip <ip> <gateway> <dns>  (mascara fija /24)"); }
        else {
            uint8_t ip[4], gw[4], dns[4];
            if (!parse_ip(args[1],ip) || !parse_ip(args[2],gw) || !parse_ip(args[3],dns)) {
                push_line("IP invalida, formato: 192.168.1.50");
            } else if (!rtl8139_present()) {
                push_line("No se detecto tarjeta de red (RTL8139)");
            } else {
                net_set_ip(ip[0],ip[1],ip[2],ip[3], 255,255,255,0,
                           gw[0],gw[1],gw[2],gw[3], dns[0],dns[1],dns[2],dns[3]);
                push_line("IP configurada");
            }
        }
    }
    else if (seq(args[0],"nslookup")) {
        if (argc<2) { push_line("uso: nslookup <dominio>"); }
        else if (!net_is_configured()) {
            push_line("Configura una IP primero con 'setip'");
        } else {
            uint8_t ip[4];
            if (net_dns_resolve(args[1], ip, 2000)==0) {
                char buf[40]; int p=0;
                char nb[12];
                for (int i=0;i<4;i++){
                    itoa10(ip[i],nb);
                    for(int q=0;nb[q];q++) buf[p++]=nb[q];
                    if (i<3) buf[p++]='.';
                }
                buf[p]='\0';
                push_line(buf);
            } else {
                push_line("No se pudo resolver el dominio");
            }
        }
    }
    else if (seq(args[0],"ping")) {
        if (argc<2) { push_line("uso: ping <ip>"); }
        else if (!net_is_configured()) {
            push_line("Configura una IP primero con 'setip'");
        } else {
            uint8_t ip[4];
            if (!parse_ip(args[1],ip)) { push_line("IP invalida"); }
            else {
                int rtt = net_ping(ip[0],ip[1],ip[2],ip[3], 1000);
                if (rtt<0) push_line("Sin respuesta (timeout)");
                else {
                    char buf[40]; int p=0;
                    const char *pre="Respuesta en ";
                    for(int q=0;pre[q];q++) buf[p++]=pre[q];
                    char nb[12]; itoa10(rtt,nb);
                    for(int q=0;nb[q];q++) buf[p++]=nb[q];
                    const char *suf=" ms";
                    for(int q=0;suf[q];q++) buf[p++]=suf[q];
                    buf[p]='\0';
                    push_line(buf);
                }
            }
        }
    }
    else if (seq(args[0],"wget")) {
        /* uso: wget <host> [path]   ej: wget example.com /index.html */
        if (argc<2) { push_line("uso: wget <host> [/path]"); }
        else if (!net_is_configured()) {
            push_line("Configura una IP primero con 'setip'");
        } else {
            const char *host = args[1];
            const char *path = (argc>=3) ? args[2] : "/";
            push_line("Conectando...");
            static char http_buf[2048];
            int n = net_http_get(host, path, http_buf, sizeof(http_buf));
            if (n < 0) {
                push_line("Error: no se pudo conectar o resolver el host");
            } else {
                /* Muestra hasta 8 lineas del contenido */
                int lines=0, lstart=0;
                static char linebuf[82];
                for (int i=0; i<=n && lines<8; i++){
                    if (http_buf[i]=='\n'||http_buf[i]=='\r'||http_buf[i]=='\0'){
                        int llen=i-lstart; if(llen>80)llen=80;
                        for(int k=0;k<llen;k++) linebuf[k]=http_buf[lstart+k];
                        linebuf[llen]='\0';
                        if (llen>0){ push_line(linebuf); lines++; }
                        lstart=i+1;
                        if (http_buf[i]=='\r'&&http_buf[i+1]=='\n') lstart++;
                    }
                }
                if (n>0) { char nb[16]; itoa10(n,nb); char out[32]; int p=0;
                    const char *pre="["; for(int q=0;pre[q];q++) out[p++]=pre[q];
                    for(int k=0;nb[k];k++) out[p++]=nb[k];
                    const char *suf=" bytes]"; for(int k=0;suf[k];k++) out[p++]=suf[k];
                    out[p]='\0'; push_line(out); }
            }
        }
    }
    else if (seq(args[0],"pkg")) {
        if (argc<2) { push_line("uso: pkg install <url> | pkg list | pkg run <archivo>"); }
        else if (seq(args[1],"install")) {
            if (argc<3) push_line("uso: pkg install http://host/paquete.txt");
            else pkg_install_url(args[2]);
        } else if (seq(args[1],"list")) {
            pkg_list();
        } else if (seq(args[1],"run")) {
            if (argc<3) push_line("uso: pkg run <archivo>");
            else script_execute_file(args[2]);
        } else {
            push_line("uso: pkg install <url> | pkg list | pkg run <archivo>");
        }
    }
    else if (seq(args[0],"reboot")) {
        push_line("Reiniciando...");
        __asm__ volatile ("movb $0xFE,%%al\noutb %%al,$0x64\n" ::: "eax");
    }
    else if (seq(args[0],"usbinfo")) {
        /* Diagnóstico USB: muestra controladores y dispositivos detectados */
        extern int usb_controller_present(void);
        extern int ehci_controller_present(void);
        extern int usb_device_count(void);
        extern int usb_msd_present(void);
        extern uint32_t usb_msd_sector_count(void);
        extern int fat32_ready(void);

        char buf[64];
        push_line("=== Diagnóstico USB ===");

        /* Controladores */
        push_line(usb_controller_present() ? "[OK] UHCI (USB 1.1) activo"
                                           : "[--] UHCI no encontrado");
        push_line(ehci_controller_present() ? "[OK] EHCI (USB 2.0) activo"
                                            : "[--] EHCI no encontrado");

        /* Escanear PCI buscando controladores USB */
        push_line("--- Controladores USB en PCI ---");
        int found_usb=0;
        for(int i=0;i<pci_count();i++){
            const pci_device_t *d=pci_get(i);
            if(d->class_code==0x0C && d->subclass==0x03){
                const char *tipo="?";
                if(d->prog_if==0x00) tipo="UHCI";
                else if(d->prog_if==0x10) tipo="OHCI";
                else if(d->prog_if==0x20) tipo="EHCI";
                else if(d->prog_if==0x30) tipo="xHCI";
                /* "bus:slot.func tipo vendor:device" */
                int p=0;
                buf[p++]='0'+d->bus/10; buf[p++]='0'+d->bus%10;
                buf[p++]=':';
                buf[p++]='0'+d->slot/10; buf[p++]='0'+d->slot%10;
                buf[p++]='.'; buf[p++]='0'+d->func;
                buf[p++]=' ';
                for(int k=0;tipo[k];k++) buf[p++]=tipo[k];
                buf[p++]=' ';
                /* vendor hex */
                static const char hex[]="0123456789ABCDEF";
                buf[p++]=hex[(d->vendor_id>>12)&0xF];
                buf[p++]=hex[(d->vendor_id>>8)&0xF];
                buf[p++]=hex[(d->vendor_id>>4)&0xF];
                buf[p++]=hex[d->vendor_id&0xF];
                buf[p++]=':';
                buf[p++]=hex[(d->device_id>>12)&0xF];
                buf[p++]=hex[(d->device_id>>8)&0xF];
                buf[p++]=hex[(d->device_id>>4)&0xF];
                buf[p++]=hex[d->device_id&0xF];
                buf[p]='\0';
                push_line(buf);
                found_usb++;
            }
        }
        if(!found_usb) push_line("  (ninguno detectado en PCI)");

        /* Dispositivos enumerados */
        int ndev=usb_device_count();
        push_line("--- Dispositivos USB enumerados ---");
        if(ndev==0){
            push_line("  (ninguno)");
        } else {
            for(int i=0;i<ndev;i++){
                /* Mostrar clase/subclase/protocol */
                extern usb_device_t* usb_get_device(int);
                usb_device_t *dev=usb_get_device(i);
                if(!dev||!dev->valid) continue;
                int p=0;
                buf[p++]='#'; buf[p++]='0'+i; buf[p++]=' ';
                buf[p++]='c';
                static const char hex2[]="0123456789ABCDEF";
                buf[p++]=hex2[dev->dev_class>>4];
                buf[p++]=hex2[dev->dev_class&0xF];
                buf[p++]='/';
                buf[p++]=hex2[dev->dev_subclass>>4];
                buf[p++]=hex2[dev->dev_subclass&0xF];
                buf[p++]='/';
                buf[p++]=hex2[dev->dev_protocol>>4];
                buf[p++]=hex2[dev->dev_protocol&0xF];
                buf[p++]=' ';
                if(dev->dev_class==0x08){
                    buf[p++]='M';buf[p++]='S';buf[p++]='D';
                } else if(dev->dev_class==0x09){
                    buf[p++]='H';buf[p++]='U';buf[p++]='B';
                } else if(dev->dev_class==0x07){
                    buf[p++]='P';buf[p++]='R';buf[p++]='N';
                } else {
                    buf[p++]='?';
                }
                buf[p++]=' '; buf[p++]=dev->low_speed?'F':'H';
                buf[p++]='S';
                buf[p]='\0';
                push_line(buf);
            }
        }

        /* MSD y FAT32 */
        push_line("--- Mass Storage ---");
        if(usb_msd_present()){
            uint32_t secs=usb_msd_sector_count();
            uint32_t mb=secs/2048;
            int p=0;
            buf[p++]='[';buf[p++]='O';buf[p++]='K';buf[p++]=']';buf[p++]=' ';
            char tmp[12]; int n=(int)mb,j=0;
            if(n==0){tmp[j++]='0';}
            else{char r[10];int k=0;while(n>0){r[k++]='0'+n%10;n/=10;}while(k>0)tmp[j++]=r[--k];}
            for(int k=0;k<j;k++) buf[p++]=tmp[k];
            buf[p++]=' ';buf[p++]='M';buf[p++]='B';buf[p]='\0';
            push_line(buf);
            push_line(fat32_ready()?"[OK] FAT32 montado":"[!!] No es FAT32");
        } else {
            push_line("  No hay USB Mass Storage");
            push_line("  QEMU: agrega -device usb-ehci -device usb-storage");
        }
    }
    else {
        push_line("comando no encontrado, probá 'help'");
    }
}

void terminal_init(void) {
    term_clear();
    push_line("CoreM Terminal v1.0");
    push_line("Escribi 'help' para ver los comandos.");
    push_line("");
    input_len=0; input[0]='\0';
}

void terminal_putchar(int c) {
    if (c=='\n' || c=='\r') {
        char full[INPUT_LEN+4];
        full[0]='$'; full[1]=' ';
        scpy(full+2,input,INPUT_LEN);
        push_line(full);
        if (input_len>0) run_command(input);
        input_len=0; input[0]='\0';
    } else if (c=='\b') {
        if (input_len>0) { input_len--; input[input_len]='\0'; }
    } else if (c>=32 && c<127 && input_len<INPUT_LEN-1) {
        input[input_len++]=c; input[input_len]='\0';
    }
}

void terminal_draw(int wx,int wy,int ww,int wh) {
    fb_fill_rect(wx+BORDER,wy+TITLEBAR_H,ww-BORDER*2,wh-TITLEBAR_H-BORDER,fb_color(0x06,0x08,0x14));
    uint32_t fg=fb_color(0x66,0xff,0x88), bg=fb_color(0x06,0x08,0x14);
    int ox=wx+BORDER+6, oy=wy+TITLEBAR_H+6;
    /* Cuantas lineas caben segun altura actual */
    int visible = (wh - TITLEBAR_H - BORDER - 20) / 13;
    if(visible > TERM_LINES) visible = TERM_LINES;
    if(visible < 1) visible = 1;
    int start = TERM_LINES - visible;
    for (int i=0;i<visible;i++)
        fb_draw_str(ox,oy+i*13,lines[start+i],fg,bg);
    char prompt[64];
    prompt[0]='$'; prompt[1]=' ';
    scpy(prompt+2,input,INPUT_LEN);
    int pl=slen(prompt);
    prompt[pl]='_'; prompt[pl+1]='\0';
    fb_draw_str(ox,oy+visible*13,prompt,fb_color(0xff,0xff,0x55),bg);
}
