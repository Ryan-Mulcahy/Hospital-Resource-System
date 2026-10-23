// sound.h - sound effects. The sounds are made in code when the program
// starts, so there are no sound files to keep with the project.
// If the computer has no sound device, everything here quietly does nothing.

#ifndef SOUND_H
#define SOUND_H

#include <stdbool.h>
#include "sim.h"

typedef enum {
    SND_SIREN,      // an emergency is on the way by ambulance
    SND_BEEP,       // monitor alarm: an emergency patient in a bed with no doctor yet
    SND_CHIME,      // the entrance doors opening (3D view)
    SND_BUZZ,       // the operation game: you touched the edge
    SND_SUCCESS,    // the operation game: surgery went well
    SND_ROTOR,      // helicopter blades (played on a loop while one is flying)
    SND_RAIN,       // rain (on a loop while it rains, 3D view)
    SND_CHARGE,     // the defibrillator charging: a rising whine
    SND_ZAP,        // the defibrillator's shock
    SND_POWER_DOWN, // the power going off: a hum sinking away, and a clunk
    SND_GENERATOR,  // the generator starting: a cough, then a rising rumble
    SND_COUNT
} SoundId;

// Open the sound device and make the sounds. Call once after InitWindow().
void sound_init(void);

// Free the sounds and close the device. Call once before CloseWindow().
void sound_close(void);

// Play one sound (unless muted).
void sound_play(SoundId id);

// Keep a looping sound (the rotor, the rain) going while `on` is true, at
// `volume` (0 to 1), and stop it when it's false. Call every frame.
void sound_loop(SoundId id, bool on, float volume);

// M key: sound on or off.
void sound_toggle_mute(void);
bool sound_muted(void);

// Call every frame during a shift: plays the siren when an emergency
// arrives, and the monitor alarm while an emergency patient in a bed has
// no doctor yet.
void sound_update(const Sim *sim, bool paused);

#endif
