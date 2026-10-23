// gameui.c - drawing shared by the mini-games (see gameui.h).

#include <math.h>
#include <stddef.h>
#include "raylib.h"
#include "draw.h"            // screen size
#include "gameui.h"

void gameui_centre_text(const char *text, int y, int size, Color colour)
{
    DrawText(text, SCREEN_W / 2 - MeasureText(text, size) / 2, y, size, colour);
}

void gameui_top_bar(const char *title, const char *who, float timeLeft, float timeLimit, Color bar)
{
    DrawRectangle(0, 0, SCREEN_W, 84, bar);
    DrawText(title, 24, 14, 26, WHITE);
    DrawText(who, 24, 50, 16, (Color){ 190, 200, 215, 255 });
    float frac = (timeLimit > 0) ? timeLeft / timeLimit : 0;
    if (frac < 0) frac = 0;
    Color timeCol = frac > 0.5f ? (Color){ 90, 220, 140, 255 } : frac > 0.25f ? (Color){ 240, 190, 60, 255 } : (Color){ 240, 80, 80, 255 };
    DrawRectangleRounded((Rectangle){ 700, 54, 270, 12 }, 1.0f, 6, (Color){ 10, 14, 20, 255 });
    if (frac > 0) DrawRectangleRounded((Rectangle){ 700, 54, 270 * frac, 12 }, 1.0f, 6, timeCol);
    DrawText(TextFormat("%.0f s", ceilf(timeLeft)), 700, 30, 18, timeCol);
}

void gameui_result(bool won, const char *heading, const char *body, const char *extra,
                   bool practice, bool showHint)
{
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, won ? (Color){ 20, 80, 50, 150 } : (Color){ 120, 10, 20, 150 });
    Rectangle box = { 230, 190, 540, 220 };
    DrawRectangleRounded(box, 0.15f, 10, won ? (Color){ 30, 120, 80, 245 } : (Color){ 150, 30, 40, 245 });
    gameui_centre_text(heading, 215, 32, WHITE);
    gameui_centre_text(body, 270, 20, (Color){ 235, 245, 240, 255 });
    if (extra != NULL) gameui_centre_text(extra, 304, 16, (Color){ 255, 225, 205, 255 });
    const char *hint = practice ? "R try again    -    click or ENTER to carry on" : "Click or press ENTER to carry on";
    if (showHint) gameui_centre_text(hint, 352, 18, (Color){ 230, 240, 235, 255 });
}

void gameui_give_up_hint(void)
{
    DrawText("Q give up", SCREEN_W - 110, 578, 14, (Color){ 150, 165, 180, 255 });
}
