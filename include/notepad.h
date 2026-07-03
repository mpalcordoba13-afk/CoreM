#ifndef NOTEPAD_H
#define NOTEPAD_H

void notepad_init(void);
void notepad_load(const char *name);
void notepad_set_content(const char *text);
void notepad_putchar(int c);
void notepad_draw(int wx,int wy,int ww,int wh);
int  notepad_click(int wx,int wy,int ww,int wh,int mx,int my);
const char* notepad_filename(void);

#endif
