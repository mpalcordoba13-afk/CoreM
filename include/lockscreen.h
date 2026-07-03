#ifndef LOCKSCREEN_H
#define LOCKSCREEN_H
void lockscreen_init(void);
int  lockscreen_active(void);
void lockscreen_lock(void);
void lockscreen_draw(void);
void lockscreen_key(char c);
#endif
