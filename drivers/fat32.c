/*
 * fat32.c  –  Lector FAT32 mínimo para MyOS
 *
 * Capacidades:
 *   - Parsear BPB (BIOS Parameter Block) de FAT32.
 *   - Leer la tabla FAT para seguir cadenas de clusters.
 *   - Listar el directorio raíz (y subdirectorios).
 *   - Leer el contenido de un archivo (hasta FAT32_MAX_FILE bytes).
 *   - Sin malloc: buffers estáticos, un sector a la vez.
 *   - Compatible con MBR (lee la primera partición) y volúmenes sin MBR.
 *
 * Limitaciones deliberadas (OS de juguete):
 *   - Solo lectura (no escritura).
 *   - Nombres 8.3 + LFN básico (toma el primer fragmento LFN).
 *   - Un nivel de directorio a la vez (no recursivo en la API pública).
 *   - Máx. FAT32_MAX_FILES entradas por directorio.
 */

#include "fat32.h"
#include "usb_msd.h"
#include <stdint.h>

/* ---- Configuración ------------------------------------------------ */
#define FAT32_MAX_FILES   64
#define FAT32_MAX_FILE    (128*1024)  /* 128 KB máx por archivo leído */
#define FAT32_MAX_PATH    128

/* ---- Sector buffer (reusar para todo, una operación a la vez) ------ */
static uint8_t  sec[512];

/* ---- BPB parseado ------------------------------------------------- */
static uint32_t fat32_start_lba   = 0;   /* LBA del primer sector del volumen */
static uint32_t bytes_per_sec     = 512;
static uint32_t secs_per_clus     = 0;
static uint32_t reserved_secs     = 0;
static uint32_t num_fats          = 0;
static uint32_t fat_size_secs     = 0;   /* sectores por FAT */
static uint32_t root_clus         = 0;   /* cluster raíz */
static uint32_t fat_lba           = 0;   /* LBA inicio FAT1 */
static uint32_t data_lba          = 0;   /* LBA inicio área de datos */
static int      fat32_ok          = 0;

/* ---- Directorio actual -------------------------------------------- */
static fat32_entry_t dir_entries[FAT32_MAX_FILES];
static int           dir_count   = 0;
static uint32_t      cur_dir_clus= 0;    /* cluster del directorio visible */

/* ---- Archivo leído ------------------------------------------------ */
static uint8_t   file_buf[FAT32_MAX_FILE];
static uint32_t  file_len = 0;

/* ================================================================== */
/* Helpers                                                             */
/* ================================================================== */
static uint16_t u16le(const uint8_t *p){ return (uint16_t)(p[0]|(p[1]<<8)); }
static uint32_t u32le(const uint8_t *p){
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}

static int read_sec(uint32_t lba){
    return usb_msd_read_sectors(lba, 1, sec);
}

/* Convierte cluster → primer LBA de datos */
static uint32_t clus_to_lba(uint32_t clus){
    return data_lba + (clus - 2) * secs_per_clus;
}

/* Siguiente cluster en la cadena FAT (0x0FFFFFFF = fin) */
static uint32_t fat_next(uint32_t clus){
    uint32_t fat_offset = clus * 4;
    uint32_t fat_sec    = fat_lba + fat_offset / 512;
    uint32_t fat_ent    = fat_offset % 512;
    if (read_sec(fat_sec) != 0) return 0x0FFFFFFF;
    return u32le(sec + fat_ent) & 0x0FFFFFFF;
}

/* Copia hasta max-1 chars de src (UCS-2 LE, 2 bytes/char) → dst ASCII */
static void ucs2_to_ascii(const uint8_t *src, int chars, char *dst, int max){
    int o=0;
    for(int i=0;i<chars&&o<max-1;i++){
        uint16_t c = u16le(src + i*2);
        if(c==0xFFFF||c==0) break;
        dst[o++] = (c<128) ? (char)c : '?';
    }
    dst[o]='\0';
}

/* ================================================================== */
/* Parsear BPB                                                         */
/* ================================================================== */
static int try_parse_bpb(uint32_t start_lba){
    if (read_sec(start_lba) != 0) return -1;
    /* Firma de boot sector */
    if (sec[510]!=0x55 || sec[511]!=0xAA) return -1;

    bytes_per_sec = u16le(sec+11);
    if (bytes_per_sec != 512) return -1;  /* solo soportamos 512 */

    secs_per_clus = sec[13];
    reserved_secs = u16le(sec+14);
    num_fats      = sec[16];

    /* FAT32: root_entry_count == 0 y fat_size_16 == 0 */
    uint16_t root_ent_cnt = u16le(sec+17);
    uint16_t fat_size_16  = u16le(sec+22);
    if (root_ent_cnt != 0) return -1;   /* no es FAT32 */
    if (fat_size_16  != 0) return -1;

    fat_size_secs = u32le(sec+36);      /* BPB_FATSz32 */
    root_clus     = u32le(sec+44);      /* BPB_RootClus */

    fat_lba  = start_lba + reserved_secs;
    data_lba = fat_lba + num_fats * fat_size_secs;

    fat32_start_lba = start_lba;
    return 0;
}

/* ================================================================== */
/* Init: busca FAT32 en el MBR o directamente en sector 0             */
/* ================================================================== */
int fat32_init(void){
    fat32_ok    = 0;
    dir_count   = 0;
    file_len    = 0;

    if (!usb_msd_present()) return -1;

    /* Intentar MBR (sector 0): leer tabla de particiones */
    if (read_sec(0) != 0) return -1;

    int found = 0;
    if (sec[510]==0x55 && sec[511]==0xAA){
        /* 4 entradas de partición en 0x1BE..0x1FD */
        for (int p=0;p<4&&!found;p++){
            uint8_t *pe = sec + 0x1BE + p*16;
            uint8_t  type = pe[4];
            uint32_t lba  = u32le(pe+8);
            /* tipos FAT32: 0x0B, 0x0C, 0x0E, 0x83 (linux ext, ignorar) */
            if ((type==0x0B||type==0x0C||type==0x0E) && lba>0){
                if (try_parse_bpb(lba) == 0) found = 1;
            }
        }
        /* Si ninguna partición, intentar el propio sector 0 como volumen */
        if (!found) found = (try_parse_bpb(0) == 0);
    } else {
        found = (try_parse_bpb(0) == 0);
    }

    if (!found) return -1;

    fat32_ok    = 1;
    cur_dir_clus= root_clus;
    fat32_read_dir(root_clus);
    return 0;
}

int fat32_ready(void){ return fat32_ok; }

/* ================================================================== */
/* Leer directorio                                                     */
/* ================================================================== */
void fat32_read_dir(uint32_t clus){
    dir_count = 0;
    cur_dir_clus = clus;

    /* Nombre LFN acumulado (un buffer de 256 chars) */
    static char lfn_buf[256];
    int         lfn_valid = 0;
    lfn_buf[0]='\0';

    uint32_t c = clus;
    while (c < 0x0FFFFFF8 && dir_count < FAT32_MAX_FILES){
        uint32_t lba = clus_to_lba(c);
        for (uint32_t s=0; s<secs_per_clus && dir_count<FAT32_MAX_FILES; s++){
            if (read_sec(lba+s) != 0) goto done;
            for (int e=0; e<16; e++){
                uint8_t *ent = sec + e*32;
                uint8_t  attr = ent[11];

                if (ent[0]==0x00) goto done;    /* fin del directorio */
                if (ent[0]==0xE5) { lfn_valid=0; continue; } /* borrado */

                /* LFN entry */
                if (attr==0x0F){
                    /* Tomar los 13 caracteres UCS-2 de esta entrada */
                    /* Solo usamos el primer fragmento (seq & 0x3F == 1) para
                     * simplicidad; para LFN largo concatenaríamos todos. */
                    static char frag[14];
                    int o=0;
                    /* 5 chars en offset 1 */
                    for(int k=0;k<5;k++){
                        uint16_t uc=u16le(ent+1+k*2);
                        if(!uc||uc==0xFFFF) break;
                        frag[o++]=(uc<128)?(char)uc:'?';
                    }
                    /* 6 chars en offset 14 */
                    for(int k=0;k<6&&o<13;k++){
                        uint16_t uc=u16le(ent+14+k*2);
                        if(!uc||uc==0xFFFF) break;
                        frag[o++]=(uc<128)?(char)uc:'?';
                    }
                    /* 2 chars en offset 28 */
                    for(int k=0;k<2&&o<13;k++){
                        uint16_t uc=u16le(ent+28+k*2);
                        if(!uc||uc==0xFFFF) break;
                        frag[o++]=(uc<128)?(char)uc:'?';
                    }
                    frag[o]='\0';

                    uint8_t seq = ent[0] & 0x3F;
                    if (seq==1){
                        /* Este es el último LFN en el grupo (se almacena al revés)
                         * → para un nombre simple, es suficiente con este fragmento */
                        int k=0;
                        while(frag[k]) lfn_buf[k]=frag[k],k++;
                        lfn_buf[k]='\0';
                        lfn_valid=1;
                    }
                    continue;
                }

                /* Entrada normal */
                if (attr & 0x08) { lfn_valid=0; continue; } /* volume label */

                fat32_entry_t *fe = &dir_entries[dir_count];

                /* Nombre 8.3 como fallback */
                char name83[13];
                int ni=0;
                for(int k=0;k<8&&ent[k]!=' ';k++) name83[ni++]=(char)ent[k];
                if(ent[8]!=' '){ name83[ni++]='.'; }
                for(int k=8;k<11&&ent[k]!=' ';k++) name83[ni++]=(char)ent[k];
                name83[ni]='\0';

                /* Preferir LFN si disponible */
                if (lfn_valid && lfn_buf[0]){
                    int k=0;
                    while(lfn_buf[k]&&k<FAT32_NAME_LEN-1){ fe->name[k]=lfn_buf[k]; k++; }
                    fe->name[k]='\0';
                } else {
                    int k=0;
                    while(name83[k]&&k<FAT32_NAME_LEN-1){ fe->name[k]=name83[k]; k++; }
                    fe->name[k]='\0';
                }
                lfn_valid=0; lfn_buf[0]='\0';

                fe->is_dir = (attr & 0x10) ? 1 : 0;
                fe->size   = u32le(ent+28);
                fe->cluster= ((uint32_t)u16le(ent+20)<<16) | u16le(ent+26);

                /* Ignorar . y .. */
                if (fe->name[0]=='.' && (fe->name[1]=='\0'||
                    (fe->name[1]=='.'&&fe->name[2]=='\0'))) continue;

                dir_count++;
            }
        }
        c = fat_next(c);
    }
done:;
}

int fat32_dir_count(void){ return dir_count; }

const fat32_entry_t* fat32_dir_entry(int i){
    if (i<0||i>=dir_count) return 0;
    return &dir_entries[i];
}

/* ================================================================== */
/* Leer archivo                                                        */
/* ================================================================== */
int fat32_read_file(uint32_t start_clus, uint32_t size, uint8_t **out_buf){
    file_len = 0;
    if (!fat32_ok) return -1;
    if (size == 0){ *out_buf = file_buf; return 0; }

    uint32_t to_read = size < FAT32_MAX_FILE ? size : FAT32_MAX_FILE;
    uint32_t off = 0;
    uint32_t c   = start_clus;

    while (c < 0x0FFFFFF8 && off < to_read){
        uint32_t lba = clus_to_lba(c);
        for (uint32_t s=0; s<secs_per_clus && off<to_read; s++){
            if (read_sec(lba+s) != 0) goto done_file;
            uint32_t chunk = to_read - off;
            if (chunk > 512) chunk = 512;
            for (uint32_t k=0;k<chunk;k++) file_buf[off+k]=sec[k];
            off += chunk;
        }
        c = fat_next(c);
    }
done_file:
    file_len = off;
    *out_buf = file_buf;
    return (int)off;
}

uint32_t fat32_file_len(void){ return file_len; }
