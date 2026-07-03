#ifndef SYSLOG_H
#define SYSLOG_H
void syslog_init(void);
void syslog_write(const char *msg);
void syslog_draw(int wx,int wy,int ww,int wh);
int  syslog_click(int wx,int wy,int ww,int wh,int mx,int my);
#endif
