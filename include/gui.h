#ifndef GUI_H
#define GUI_H
#include <stdint.h>

#define MAX_WINDOWS 36
#define TITLEBAR_H  20
#define BORDER      2

typedef enum {
    WIN_NORMAL = 0,
    WIN_CLOCK,
    WIN_WALLPAPER,
    WIN_ABOUT,
    WIN_CALC,
    WIN_TERMINAL,
    WIN_FILEMANAGER,
    WIN_NOTEPAD,
    WIN_SYSMONITOR,
    WIN_SETTINGS,
    WIN_SNAKE,
    WIN_TETRIS,
    WIN_TRASH,
    WIN_GALLERY,
    WIN_IMAGEVIEWER,
    WIN_MUSICPLAYER,
    WIN_GUIDE,
    WIN_PACMAN,
    WIN_PCIVIEWER,
    WIN_PONG,
    WIN_BROWSER,
    WIN_USBEXPLORER,
    WIN_CODEEDITOR,
    /* Nuevas */
    WIN_PAINT,
    WIN_SPREADSHEET,
    WIN_CALENDAR,
    WIN_ALARM,
    WIN_SYSLOG,
    WIN_MINESWEEPER,
    WIN_2048,
    WIN_CHESS,
    WIN_BREAKOUT,
    WIN_CONWAY,
} win_type_t;

typedef struct {
    int x, y, w, h;
    char title[32];
    int active, focused, dragging, minimized;
    int drag_ox, drag_oy;
    char content[512];
    win_type_t type;
    int maximized, resizing;
    int prev_x, prev_y, prev_w, prev_h;
} window_t;

void gui_init(void);
void gui_run(void);
int  gui_new_window(int x,int y,int w,int h,const char *title,const char *content);
int  gui_new_window_typed(int x,int y,int w,int h,const char *title,win_type_t type);
int  gui_window_at(int px, int py);
window_t* gui_get_window(int idx);
void open_window_by_title(const char *title);

#endif
