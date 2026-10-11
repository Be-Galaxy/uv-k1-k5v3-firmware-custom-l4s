/* Host display hooks using production UI renderer and firmware fonts. No hardware access. */
#include "app/l4s_control.h"
#include "ui/l4s.h"
#include "font.h"
#include <string.h>
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
static uint8_t framebuffer[7][128];
static char lines[7][24];
static unsigned overflow;
void _putchar(char ch) { (void)ch; }
void UI_DisplayClear(void) { memset(framebuffer,0,sizeof(framebuffer)); memset(lines,0,sizeof(lines)); overflow=0; }
void ST7565_BlitFullScreen(void) {}
void UI_PrintString(const char *text,uint8_t start,uint8_t end,uint8_t row,uint8_t width)
{
    (void)end;
    size_t n=strlen(text);
    if (row>=6 || start+n*width>128 || n>=sizeof(lines[0])) { overflow++; return; }
    memcpy(lines[row],text,n+1);
    for (unsigned i=0;i<n;i++) if (text[i]>' ' && text[i]<127) {
        memcpy(framebuffer[row]+start+i*width,gFontBig[text[i]-' '-1],7);
        memcpy(framebuffer[row+1]+start+i*width,gFontBig[text[i]-' '-1]+7,7);
    }
}
void UI_PrintStringSmallNormal(const char *text,uint8_t start,uint8_t end,uint8_t row)
{
    size_t n=strlen(text);
    if (end>start) {
        if (n*7>(unsigned)(end-start+1)) { overflow++; return; }
        start+=(end-start-n*7+1)/2;
    }
    if (row>=7 || start+n*7>128 || n>=sizeof(lines[0])) { overflow++; return; }
    memcpy(lines[row],text,n+1);
    for (unsigned i=0;i<n;i++) if (text[i]>' ' && text[i]<127)
        memcpy(framebuffer[row]+start+i*7+1,gFontSmall[text[i]-' '-1],6);
}
EXPORT unsigned Host_Render(uint8_t *out) { UI_DisplayL4s(); memcpy(out,framebuffer,sizeof(framebuffer)); return overflow; }
EXPORT const char *Host_DisplayText(unsigned row) { return row<7 ? lines[row] : ""; }
