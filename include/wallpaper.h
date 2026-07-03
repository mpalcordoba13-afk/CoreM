#ifndef WALLPAPER_H
#define WALLPAPER_H
#include <stdint.h>

typedef enum {
    WP_SOLID = 0,
    WP_GRADIENT,
    WP_STARS,
    WP_GRID,
    WP_SUNSET,
    WP_GALLERY0,
    WP_GALLERY1,
    WP_GALLERY2,
    WP_GALLERY3,
    WP_ASSET0,
    WP_ASSET1,
    WP_ASSET2,
    WP_ASSET3,
    WP_ASSET4,
    WP_ASSET5,
} wallpaper_t;

#define WP_COUNT 15

void wallpaper_set(wallpaper_t type);
void wallpaper_draw(void);
void wallpaper_invalidate(void);
wallpaper_t wallpaper_next(void);
wallpaper_t wallpaper_current(void);

#endif
