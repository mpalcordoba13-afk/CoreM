#include "volume.h"
#include "sound.h"

int g_volume = 7;

void volume_up(void){
    if(g_volume<10) g_volume++;
    sound_beep(800+g_volume*40,30);
}
void volume_down(void){
    if(g_volume>0) g_volume--;
    if(g_volume>0) sound_beep(800+g_volume*40,30);
}
