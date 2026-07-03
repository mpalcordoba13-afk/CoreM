#include "musicplayer.h"
#include "gui.h"
#include "framebuffer.h"
#include "sound.h"
#include <stdint.h>

typedef struct { uint32_t freq; uint32_t dur_ticks; } note_t;

#define MAXNOTES 24

/* Cada pista: lista de notas (frecuencia, duracion en ticks de loop ~16ms) */
static const note_t track0[] = { /* Himno simple */
    {523,18},{523,18},{659,18},{784,30},{659,30},{523,18},{659,18},{784,30},{1046,40},{0,10}
};
static const note_t track1[] = { /* Alerta */
    {880,10},{0,6},{880,10},{0,6},{880,10},{0,20},{660,18},{0,10}
};
static const note_t track2[] = { /* Victoria */
    {523,12},{659,12},{784,12},{1046,12},{784,12},{1046,24},{1318,36},{0,16}
};
static const note_t track3[] = { /* Ambiente */
    {392,30},{440,30},{392,30},{349,30},{392,40},{0,20}
};

static const note_t* tracks[TRACK_COUNT] = {track0,track1,track2,track3};
static const int track_len[TRACK_COUNT] = {10,8,8,6};
static const char* track_names[TRACK_COUNT] = {
    "01 - Himno.snd","02 - Alerta.snd","03 - Victoria.snd","04 - Ambiente.snd"
};

static int current_track = 0;
static int playing = 0;
static int note_idx = 0;
static int note_tick = 0;
static int loop_counter = 0;

void musicplayer_init(void){
    current_track=0; playing=0; note_idx=0; note_tick=0;
}

static void start_note(void){
    const note_t *n = &tracks[current_track][note_idx];
    if(n->freq>0){
        /* Iniciar tono: usamos beep corto repetido via tick para no bloquear */
        sound_beep(n->freq, 4);
    }
}

void musicplayer_tick(void){
    if(!playing) return;
    loop_counter++;
    if(loop_counter < 3) return; /* ritmo del reproductor */
    loop_counter=0;

    const note_t *n = &tracks[current_track][note_idx];
    if(note_tick==0) start_note();
    note_tick++;
    if(note_tick >= (int)n->dur_ticks){
        note_tick=0;
        note_idx++;
        if(note_idx >= track_len[current_track]){
            note_idx=0; /* loop de la pista */
        }
    }
}

static void play_pause(void){ playing=!playing; }
static void next_track(void){ current_track=(current_track+1)%TRACK_COUNT; note_idx=0; note_tick=0; }
static void prev_track(void){ current_track=(current_track+TRACK_COUNT-1)%TRACK_COUNT; note_idx=0; note_tick=0; }

void musicplayer_draw(int wx,int wy,int ww,int wh){
    (void)wh;
    int ox=wx+BORDER+10, oy=wy+TITLEBAR_H+10;
    int iw=ww-BORDER*2-20;
    uint32_t bg=fb_color(0xf2,0xf2,0xf2);

    fb_draw_str(ox,oy,"Carpeta: /sounds",fb_color(0x0f,0x34,0x60),bg); oy+=24;

    /* lista de pistas */
    for(int i=0;i<TRACK_COUNT;i++){
        int by=oy+i*24;
        uint32_t rowbg = (i==current_track) ? fb_color(0x20,0x55,0x95) : fb_color(0xe4,0xe4,0xf2);
        uint32_t txt = (i==current_track) ? fb_color(0xff,0xff,0xff) : fb_color(0x11,0x11,0x22);
        fb_fill_rect(ox,by,iw,20,rowbg);
        fb_draw_str(ox+8,by+6,track_names[i],txt,rowbg);
    }
    oy += TRACK_COUNT*24+14;

    /* Visualizador simple: barras que laten si esta reproduciendo */
    int barw=iw, barh=40;
    fb_fill_rect(ox,oy,barw,barh,fb_color(0x10,0x10,0x20));
    if(playing){
        for(int b=0;b<24;b++){
            int hgt = 4 + ((note_idx*7+b*5)%barh-6 < 0 ? 6 : (note_idx*7+b*5)%(barh-6));
            fb_fill_rect(ox+4+b*(barw/24),oy+barh-hgt,barw/24-2,hgt,fb_color(0x44,0xcc,0x88));
        }
    } else {
        fb_draw_str(ox+barw/2-50,oy+16,"(en pausa)",fb_color(0x66,0x66,0x88),fb_color(0x10,0x10,0x20));
    }
    oy += barh+14;

    /* Controles */
    int bw=70,bh=32,gap=10;
    int bx = ox + (iw - (bw*3+gap*2))/2;
    fb_fill_rect(bx,oy,bw,bh,fb_color(0x33,0x55,0x77));
    fb_draw_str(bx+24,oy+12,"<<",fb_color(0xff,0xff,0xff),fb_color(0x33,0x55,0x77));

    uint32_t pcol = playing ? fb_color(0x20,0x80,0x40) : fb_color(0xcc,0x88,0x22);
    fb_fill_rect(bx+bw+gap,oy,bw,bh,pcol);
    fb_draw_str(bx+bw+gap+18,oy+12, playing?"Pausa":"Play", fb_color(0xff,0xff,0xff),pcol);

    fb_fill_rect(bx+(bw+gap)*2,oy,bw,bh,fb_color(0x33,0x55,0x77));
    fb_draw_str(bx+(bw+gap)*2+24,oy+12,">>",fb_color(0xff,0xff,0xff),fb_color(0x33,0x55,0x77));
}

int musicplayer_click(int wx,int wy,int ww,int wh,int mx,int my){
    (void)wh;
    int ox=wx+BORDER+10, oy=wy+TITLEBAR_H+10+24;
    int iw=ww-BORDER*2-20;

    for(int i=0;i<TRACK_COUNT;i++){
        int by=oy+i*24;
        if(mx>=ox&&mx<ox+iw&&my>=by&&my<by+20){
            current_track=i; note_idx=0; note_tick=0;
            return 1;
        }
    }
    oy += TRACK_COUNT*24+14+40+14;

    int bw=70,bh=32,gap=10;
    int bx = ox + (iw - (bw*3+gap*2))/2;
    if(mx>=bx&&mx<bx+bw&&my>=oy&&my<oy+bh){ prev_track(); return 1; }
    if(mx>=bx+bw+gap&&mx<bx+bw+gap+bw&&my>=oy&&my<oy+bh){ play_pause(); return 1; }
    if(mx>=bx+(bw+gap)*2&&mx<bx+(bw+gap)*2+bw&&my>=oy&&my<oy+bh){ next_track(); return 1; }
    return 0;
}
