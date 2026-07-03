#include "fs.h"

typedef struct {
    char name[FS_NAME_LEN];
    char data[FS_DATA_LEN];
    int  size;
    int  used;
} fs_file_t;

static fs_file_t files[FS_MAX_FILES];

static void scpy(char *d, const char *s, int max) {
    int i=0; while (s[i] && i<max-1) { d[i]=s[i]; i++; } d[i]='\0';
}
static int seq(const char *a, const char *b) {
    while (*a && *b) { if (*a!=*b) return 0; a++; b++; }
    return *a==*b;
}
static int slen(const char *s){int n=0;while(s[n])n++;return n;}

int fs_find(const char *name) {
    for (int i=0;i<FS_MAX_FILES;i++)
        if (files[i].used && seq(files[i].name, name)) return i;
    return -1;
}
int fs_exists(const char *name) { return fs_find(name) >= 0; }

int fs_write(const char *name, const char *data, int len) {
    int idx = fs_find(name);
    if (idx < 0) {
        for (int i=0;i<FS_MAX_FILES;i++) if (!files[i].used) { idx=i; break; }
        if (idx < 0) return -1;
        scpy(files[idx].name, name, FS_NAME_LEN);
        files[idx].used = 1;
    }
    if (len > FS_DATA_LEN-1) len = FS_DATA_LEN-1;
    for (int i=0;i<len;i++) files[idx].data[i] = data[i];
    files[idx].data[len] = '\0';
    files[idx].size = len;
    return len;
}

int fs_read(const char *name, char *buf, int maxlen) {
    int idx = fs_find(name);
    if (idx < 0) return -1;
    int len = files[idx].size;
    if (len > maxlen-1) len = maxlen-1;
    for (int i=0;i<len;i++) buf[i]=files[idx].data[i];
    buf[len]='\0';
    return len;
}

int fs_delete(const char *name) {
    int idx = fs_find(name);
    if (idx < 0) return -1;
    files[idx].used = 0;
    return 0;
}

int fs_get_entry(int index, char *name_out, int *size_out) {
    int c=0;
    for (int i=0;i<FS_MAX_FILES;i++) {
        if (files[i].used) {
            if (c==index) { scpy(name_out, files[i].name, FS_NAME_LEN); *size_out=files[i].size; return 1; }
            c++;
        }
    }
    return 0;
}

int fs_total_used(void) {
    int t=0;
    for (int i=0;i<FS_MAX_FILES;i++) if (files[i].used) t += files[i].size;
    return t;
}

void fs_init(void) {
    for (int i=0;i<FS_MAX_FILES;i++) files[i].used=0;

    const char *welcome =
        "Bienvenido a CoreM\n\n"
        "Este es un archivo de\n"
        "ejemplo guardado en el\n"
        "sistema de archivos\n"
        "en RAM.\n\n"
        "Proba en la Terminal:\n"
        "  cat bienvenida.txt\n"
        "  ls\n"
        "  help";
    fs_write("bienvenida.txt", welcome, slen(welcome));
    fs_write("notas.txt", "", 0);
}
