#ifndef MOUSE_H
#define MOUSE_H
#include <stdint.h>

void mouse_init(void);
void mouse_get_pos(int *x, int *y);
int  mouse_left_pressed(void);
int  mouse_right_pressed(void);
int  mouse_get_wheel(void);
uint32_t mouse_packet_count(void);

#endif
