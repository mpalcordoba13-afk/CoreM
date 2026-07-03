#ifndef FAT32_H
#define FAT32_H
#include <stdint.h>

#define FAT32_NAME_LEN 128

typedef struct {
    char     name[FAT32_NAME_LEN];
    uint8_t  is_dir;
    uint32_t size;
    uint32_t cluster;
} fat32_entry_t;

/* Inicia el driver FAT32 (busca en el USB MSD).
 * Retorna 0 si OK, -1 si no hay USB o no es FAT32. */
int fat32_init(void);

/* 1 si el filesystem está montado y listo */
int fat32_ready(void);

/* Lee el directorio cuyo cluster raíz es 'clus'
 * (usa fat32_root_clus() para el raíz). */
void fat32_read_dir(uint32_t clus);

/* Número de entradas válidas en el directorio actual */
int fat32_dir_count(void);

/* Acceso a una entrada del directorio actual (i = 0..dir_count-1) */
const fat32_entry_t* fat32_dir_entry(int i);

/* Lee el contenido de un archivo en el buffer interno (máx 128 KB).
 * start_clus: cluster inicial (de la fat32_entry_t).
 * size       : tamaño declarado en el directorio.
 * out_buf    : puntero al buffer interno donde se guardaron los datos.
 * Retorna bytes leídos, o -1 si error. */
int fat32_read_file(uint32_t start_clus, uint32_t size, uint8_t **out_buf);

/* Bytes efectivamente leídos en la última llamada a fat32_read_file */
uint32_t fat32_file_len(void);

#endif
