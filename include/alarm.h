#ifndef ALARM_H
#define ALARM_H
void alarm_init(void);
void alarm_draw(int wx,int wy,int ww,int wh);
int  alarm_click(int wx,int wy,int ww,int wh,int mx,int my);
void alarm_tick(void);
#endif
