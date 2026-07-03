#ifndef FS_H
#define FS_H

#define FS_MAX_FILES 16
#define FS_NAME_LEN  24
#define FS_DATA_LEN  1024

void fs_init(void);
int  fs_find(const char *name);
int  fs_exists(const char *name);
int  fs_write(const char *name, const char *data, int len);
int  fs_read(const char *name, char *buf, int maxlen);
int  fs_delete(const char *name);
int  fs_get_entry(int index, char *name_out, int *size_out);
int  fs_total_used(void);

#endif
