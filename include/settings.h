#ifndef SETTINGS_H
#define SETTINGS_H

extern int g_dark_theme;
extern int g_clock_12h;
extern int g_sound_on;

void settings_init(void);
void settings_draw(int wx,int wy,int ww,int wh);
int  settings_click(int wx,int wy,int ww,int wh,int mx,int my);

#endif
