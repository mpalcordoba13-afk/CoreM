#ifndef CALENDAR_H
#define CALENDAR_H
void calendar_init(void);
void calendar_draw(int wx,int wy,int ww,int wh);
int  calendar_click(int wx,int wy,int ww,int wh,int mx,int my);
void calendar_key(char c);
#endif
