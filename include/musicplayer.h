#ifndef MUSICPLAYER_H
#define MUSICPLAYER_H

#define TRACK_COUNT 4

void musicplayer_init(void);
void musicplayer_tick(void);
void musicplayer_draw(int wx,int wy,int ww,int wh);
int  musicplayer_click(int wx,int wy,int ww,int wh,int mx,int my);

#endif
