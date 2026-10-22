// gameui.h - drawing shared by the mini-games: the bar along the top (title,
// who it's for, the clock) and the result box at the end.
//
// All of it is in the 1000x600 layout, so call it inside BeginMode2D(fit).

#ifndef GAMEUI_H
#define GAMEUI_H

#include <stdbool.h>
#include "raylib.h"

// Text centred across the screen with its top at y.
void gameui_centre_text(const char *text, int y, int size, Color colour);

// The bar along the top: the game's title, who it's for (or "Practice
// round..."), and the clock running down from timeLimit. `bar` is the bar's
// background colour.
void gameui_top_bar(const char *title, const char *who, float timeLeft, float timeLimit, Color bar);

// The result: the screen tinted green (won) or red (lost), and a box with a
// heading, a line saying what it means for the patient, an optional extra
// line (NULL for none), and how to carry on (R to try again in practice).
// `showHint` false hides the last line for a moment, so a key that was held
// down in the game doesn't skip the result.
void gameui_result(bool won, const char *heading, const char *body, const char *extra,
                   bool practice, bool showHint);

// "Q give up" in the bottom right corner.
void gameui_give_up_hint(void);

#endif
