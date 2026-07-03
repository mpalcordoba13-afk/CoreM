/*
 * pong.c - Ping Pong de dos jugadores para MyOS.
 *
 * Jugador 1 (izquierda): W = arriba, S = abajo.
 * Jugador 2 (derecha):   Flecha UP = arriba, DOWN = abajo.
 * ENTER o ESPACIO: reiniciar cuando hay game over.
 *
 * Sin malloc, sin FPU: todo en enteros, posicion en 1/16 de pixel
 * para que la pelota se mueva suavemente incluso con la velocidad
 * baja del PIT a 100Hz.
 */
#include "pong.h"
#include "framebuffer.h"
#include "timer.h"
#include <stdint.h>

/* Unidades internas: 1 unidad = 1/16 pixel (fixed-point). */
#define FP 16

/* Dimensiones del area de juego (en pixeles reales). Se ajustan a la
 * ventana en cada draw: aqui solo como referencia del diseno. */
#define PAD_W   8   /* ancho de las paletas (px) */
#define PAD_H   48  /* alto de las paletas (px) */
#define BALL_R  5   /* radio de la pelota (px) */
#define PAD_SPD (4*FP)     /* velocidad paleta px/tick */
#define BALL_SPD_INIT (3*FP)   /* velocidad inicial pelota */
#define BALL_SPD_MAX  (7*FP)   /* velocidad maxima */
#define SCORE_WIN 7

typedef struct {
    int y;   /* centro de la paleta en fp */
    int dy;  /* movimiento pedido este tick */
    int score;
} paddle_t;

typedef struct {
    int x, y;   /* posicion del centro en fp */
    int dx, dy; /* velocidad en fp/tick */
    int visible; /* animacion de gol */
} ball_t;

static paddle_t p1, p2;
static ball_t ball;
static int field_w, field_h; /* guardados en el ultimo draw, px */
static int game_over = 0;
static int winner = 0; /* 1 o 2 */
static uint32_t last_tick = 0;
static int paused = 0; /* breve pausa post-gol */
static uint32_t pause_until = 0;

/* Numeros para el marcador dibujados a mano (5x7 pixeles): */
static const uint8_t digit_bmp[10][7] = {
    {0x7,0x5,0x5,0x5,0x5,0x5,0x7}, /* 0 */
    {0x2,0x2,0x2,0x2,0x2,0x2,0x2}, /* 1 */
    {0x7,0x1,0x1,0x7,0x4,0x4,0x7}, /* 2 */
    {0x7,0x1,0x1,0x7,0x1,0x1,0x7}, /* 3 */
    {0x5,0x5,0x5,0x7,0x1,0x1,0x1}, /* 4 */
    {0x7,0x4,0x4,0x7,0x1,0x1,0x7}, /* 5 */
    {0x7,0x4,0x4,0x7,0x5,0x5,0x7}, /* 6 */
    {0x7,0x1,0x1,0x1,0x1,0x1,0x1}, /* 7 */
    {0x7,0x5,0x5,0x7,0x5,0x5,0x7}, /* 8 */
    {0x7,0x5,0x5,0x7,0x1,0x1,0x7}, /* 9 */
};

static void draw_digit(int px, int py, int d, uint32_t color){
    if (d<0||d>9) return;
    for (int row=0;row<7;row++){
        for (int col=0;col<3;col++){
            if (digit_bmp[d][row] & (4>>col))
                fb_fill_rect(px+col*4, py+row*4, 3, 3, color);
        }
    }
}

static void draw_number(int px, int py, int n, uint32_t color){
    if (n>=10) draw_digit(px, py, n/10, color);
    draw_digit(px+14, py, n%10, color);
}

static void launch_ball(int toward_player){
    /* velocidad inicial diagonal */
    ball.dx = (toward_player==1) ? -BALL_SPD_INIT : BALL_SPD_INIT;
    /* angulo aleatorio ligero usando el tiempo como semilla */
    int r = (int)(timer_ticks() % 5) - 2;
    ball.dy = (BALL_SPD_INIT/2) + r*FP/2;
    ball.visible = 1;
}

void pong_restart(void){ pong_init(); }

void pong_init(void){
    p1.y = 100*FP; p1.dy = 0; p1.score = 0;
    p2.y = 100*FP; p2.dy = 0; p2.score = 0;
    ball.x = 200*FP; ball.y = 100*FP;
    ball.visible = 0;
    game_over = 0; winner = 0;
    field_w = 400; field_h = 260;
    paused = 1;
    pause_until = timer_ticks() + 60; /* 600ms antes de lanzar */
    launch_ball(1);
    last_tick = timer_ticks();
}

void pong_key(int key){
    if (game_over && (key==4)) { pong_init(); return; }
    /* 0=p1 up, 1=p1 down, 2=p2 up, 3=p2 down */
    if (key==0) p1.dy = -PAD_SPD;
    else if (key==1) p1.dy =  PAD_SPD;
    else if (key==2) p2.dy = -PAD_SPD;
    else if (key==3) p2.dy =  PAD_SPD;
}

void pong_tick(void){
    if (game_over) return;
    uint32_t now = timer_ticks();
    if (now == last_tick) return;
    last_tick = now;

    if (paused){
        if (now >= pause_until) paused = 0;
        /* Las paletas siguen respondiendo durante la pausa */
    }

    int gw = field_w, gh = field_h;
    int pad_margin = 12;
    int half_pad = PAD_H*FP/2;
    int half_ball = BALL_R*FP;

    /* Mover paletas */
    p1.y += p1.dy; p1.dy = 0;
    p2.y += p2.dy; p2.dy = 0;
    int top_lim = half_pad; int bot_lim = (gh-PAD_H/2)*FP - half_pad;
    if (p1.y < top_lim) p1.y = top_lim;
    if (p1.y > bot_lim) p1.y = bot_lim;
    if (p2.y < top_lim) p2.y = top_lim;
    if (p2.y > bot_lim) p2.y = bot_lim;

    if (paused) return;

    /* Mover pelota */
    ball.x += ball.dx;
    ball.y += ball.dy;

    /* Rebotar en techo/suelo */
    if (ball.y - half_ball < 0){
        ball.y = half_ball;
        ball.dy = -ball.dy;
    }
    if (ball.y + half_ball > gh*FP){
        ball.y = gh*FP - half_ball;
        ball.dy = -ball.dy;
    }

    /* Colision con paleta 1 (izquierda) */
    int p1x = (pad_margin + PAD_W)*FP;
    if (ball.dx < 0 && ball.x - half_ball <= p1x){
        int diff = ball.y - p1.y; /* diferencia desde el centro de la paleta */
        if (diff >= -half_pad && diff <= half_pad){
            ball.x = p1x + half_ball;
            ball.dx = -ball.dx;
            /* Angulo segun donde golpea: centro = recto, borde = mas angulo */
            ball.dy = diff * 3 / 2;
            /* Acelerar un poco cada rebote */
            if (ball.dx < BALL_SPD_MAX) ball.dx += FP/2;
        }
    }

    /* Colision con paleta 2 (derecha) */
    int p2x = (gw - pad_margin - PAD_W)*FP;
    if (ball.dx > 0 && ball.x + half_ball >= p2x){
        int diff = ball.y - p2.y;
        if (diff >= -half_pad && diff <= half_pad){
            ball.x = p2x - half_ball;
            ball.dx = -ball.dx;
            ball.dy = diff * 3 / 2;
            if (-ball.dx < BALL_SPD_MAX) ball.dx -= FP/2;
        }
    }

    /* Gol */
    if (ball.x < 0){
        p2.score++;
        ball.x = gw*FP/2; ball.y = gh*FP/2;
        if (p2.score >= SCORE_WIN){ game_over=1; winner=2; return; }
        paused=1; pause_until=timer_ticks()+70;
        launch_ball(2);
    }
    if (ball.x > gw*FP){
        p1.score++;
        ball.x = gw*FP/2; ball.y = gh*FP/2;
        if (p1.score >= SCORE_WIN){ game_over=1; winner=1; return; }
        paused=1; pause_until=timer_ticks()+70;
        launch_ball(1);
    }
}

void pong_draw(int wx, int wy, int ww, int wh){
    /* Area de juego: dentro del borde de la ventana */
    int title_h = 28, border = 3;
    int gx = wx + border;
    int gy = wy + title_h;
    int gw = ww - border*2;
    int gh = wh - title_h - border;
    if (gw < 80 || gh < 60) return;

    /* Guardar dimensiones para la fisica */
    field_w = gw; field_h = gh;

    /* Fondo negro */
    uint32_t BG   = fb_color(0x00,0x00,0x00);
    uint32_t FG   = fb_color(0xff,0xff,0xff);
    uint32_t BALL_C = fb_color(0xff,0xee,0x00);
    uint32_t P1C  = fb_color(0x44,0xaa,0xff);
    uint32_t P2C  = fb_color(0xff,0x55,0x55);
    uint32_t MID  = fb_color(0x33,0x33,0x33);

    fb_fill_rect(gx, gy, gw, gh, BG);

    /* Linea central punteada */
    for (int yy=0; yy<gh; yy+=8)
        fb_fill_rect(gx+gw/2-1, gy+yy, 2, 4, MID);

    /* Marcadores (escalados al ancho de la ventana) */
    int score_y = gy + 8;
    draw_number(gx + gw/4 - 14, score_y, p1.score, P1C);
    draw_number(gx + gw*3/4 - 14, score_y, p2.score, P2C);

    /* Paletas */
    int pad_margin = 12;
    int p1x_px = gx + pad_margin;
    int p1y_px = gy + p1.y/FP - PAD_H/2;
    fb_fill_rect(p1x_px, p1y_px, PAD_W, PAD_H, P1C);

    int p2x_px = gx + gw - pad_margin - PAD_W;
    int p2y_px = gy + p2.y/FP - PAD_H/2;
    fb_fill_rect(p2x_px, p2y_px, PAD_W, PAD_H, P2C);

    /* Pelota */
    int bx = gx + ball.x/FP - BALL_R;
    int by = gy + ball.y/FP - BALL_R;
    fb_fill_rect(bx, by, BALL_R*2, BALL_R*2, BALL_C);

    /* Mensajes */
    if (game_over){
        const char *msg = (winner==1) ? "Gano P1! ENTER=reiniciar" : "Gano P2! ENTER=reiniciar";
        int ml = 0; while(msg[ml]) ml++;
        fb_draw_str(gx+(gw-ml*9)/2, gy+gh/2-8, msg, FG, BG);
    } else if (paused && !game_over){
        const char *hint = "W/S  vs  UP/DOWN";
        int hl = 0; while(hint[hl]) hl++;
        fb_draw_str(gx+(gw-hl*9)/2, gy+gh-20, hint, MID, BG);
    }

    /* Borde del campo */
    fb_draw_rect(gx, gy, gw, gh, fb_color(0x33,0x33,0x33));
}
