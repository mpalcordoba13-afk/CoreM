#ifndef SPREADSHEET_H
#define SPREADSHEET_H
void spreadsheet_init(void);
void spreadsheet_draw(int wx,int wy,int ww,int wh);
int  spreadsheet_click(int wx,int wy,int ww,int wh,int mx,int my);
void spreadsheet_key(char c);
#endif
