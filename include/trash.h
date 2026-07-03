#ifndef TRASH_H
#define TRASH_H
#define TRASH_MAX 8
void trash_init(void);
int  trash_add(const char *name, const char *data, int len);
int  trash_get_entry(int i, char *name_out, int *size_out);
int  trash_restore(int i);
int  trash_empty(void);
void trash_draw(int wx,int wy,int ww,int wh);
int  trash_click(int wx,int wy,int ww,int wh,int mx,int my);
#endif
