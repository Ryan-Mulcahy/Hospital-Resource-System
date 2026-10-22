// sound.c - sound effects, made in code.
//
// A sound is a list of samples: numbers between -32767 and 32767 that say
// where the speaker should be at each moment. 44100 of them make one second.
// A sine wave at 1000 Hz (1000 ups and downs a second) sounds like a beep;
// switching between two notes sounds like a siren; random numbers sound like
// a hiss, which is the start of rain and helicopter blades. Each sound is
// built into a raylib Wave, then turned into a Sound that can be played.

#include <math.h>
#include <stddef.h>
#include "raylib.h"
#include "sound.h"

#define RATE 44100              // samples per second

static Sound sounds[SND_COUNT];
static bool  ready = false;      // false if there's no sound device
static bool  muted = false;

// Random numbers for the hissing sounds. Our own, so making the sounds
// doesn't use up raylib's random numbers that the simulation uses.
static unsigned int seed = 12345;
static float noise(void)
{
    seed = seed * 1103515245u + 12345u;
    return ((seed >> 9) & 0xFFFF) / 32768.0f - 1.0f;      // -1 to 1
}

// Make a Wave of `seconds` long. The samples are filled in afterwards.
static Wave new_wave(float seconds)
{
    Wave w = { 0 };
    w.frameCount = (unsigned int)(seconds * RATE);
    w.sampleRate = RATE;
    w.sampleSize = 16;
    w.channels = 1;
    w.data = MemAlloc(w.frameCount * sizeof(short));
    return w;
}

// Turn a finished Wave into a Sound and free the Wave.
static Sound to_sound(Wave w, float volume)
{
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    SetSoundVolume(s, volume);
    return s;
}

// A gentle fade at the start and end, so sounds don't click.
static float edges(float t, float length, float in, float out)
{
    return fminf(1.0f, fminf(t / in, (length - t) / out));
}

// Two-tone siren: high, low, high, low...
static Sound make_siren(void)
{
    float len = 1.6f;
    Wave w = new_wave(len);
    short *out = (short *)w.data;
    float phase = 0;
    for (unsigned int i = 0; i < w.frameCount; i++) {
        float t = (float)i / RATE;
        float freq = (fmodf(t, 0.8f) < 0.4f) ? 950.0f : 750.0f;
        phase += 2 * PI * freq / RATE;
        float v = sinf(phase) + 0.3f * sinf(3 * phase);     // a bit of the third harmonic: more like a horn
        out[i] = (short)(v * edges(t, len, 0.05f, 0.1f) * 0.6f * 32767 / 1.3f);
    }
    return to_sound(w, 0.30f);
}

// Short monitor beep
static Sound make_beep(void)
{
    float len = 0.12f;
    Wave w = new_wave(len);
    short *out = (short *)w.data;
    for (unsigned int i = 0; i < w.frameCount; i++) {
        float t = (float)i / RATE;
        out[i] = (short)(sinf(2 * PI * 1000.0f * t) * edges(t, len, 0.01f, 0.02f) * 0.5f * 32767);
    }
    return to_sound(w, 0.22f);
}

// Ding-dong door chime: two notes that ring and die away
static Sound make_chime(void)
{
    Wave w = new_wave(1.0f);
    short *out = (short *)w.data;
    for (unsigned int i = 0; i < w.frameCount; i++) {
        float t = (float)i / RATE;
        float v = sinf(2 * PI * 659.0f * t) * expf(-4.0f * t);                                 // E
        if (t > 0.35f) v += sinf(2 * PI * 523.0f * (t - 0.35f)) * expf(-4.0f * (t - 0.35f)); // then C
        out[i] = (short)(v * fminf(1.0f, t / 0.005f) * 0.45f * 32767);
    }
    return to_sound(w, 0.20f);
}

// The buzzer you get for touching the edge in the operation game: a harsh,
// low square wave
static Sound make_buzz(void)
{
    float len = 0.7f;
    Wave w = new_wave(len);
    short *out = (short *)w.data;
    for (unsigned int i = 0; i < w.frameCount; i++) {
        float t = (float)i / RATE;
        float v = (fmodf(t * 110.0f, 1.0f) < 0.5f) ? 1.0f : -1.0f;          // square wave at 110 Hz
        v = 0.7f * v + 0.3f * sinf(2 * PI * 220.0f * t);
        out[i] = (short)(v * edges(t, len, 0.01f, 0.08f) * 0.5f * 32767);
    }
    return to_sound(w, 0.45f);
}

// A happy rising three-note jingle for a successful operation
static Sound make_success(void)
{
    float len = 0.9f, notes[3] = { 523.0f, 659.0f, 784.0f };   // C, E, G
    Wave w = new_wave(len);
    short *out = (short *)w.data;
    for (unsigned int i = 0; i < w.frameCount; i++) {
        float t = (float)i / RATE;
        int n = (int)(t / 0.18f);
        if (n > 2) n = 2;
        float tn = t - n * 0.18f;
        float v = sinf(2 * PI * notes[n] * t) * expf(-3.0f * tn);
        out[i] = (short)(v * edges(t, len, 0.005f, 0.1f) * 0.45f * 32767);
    }
    return to_sound(w, 0.35f);
}

// Helicopter blades: a low rumbling hiss that thumps about 7 times a second.
// One second long, made to loop smoothly (7 whole thumps fit in it).
static Sound make_rotor(void)
{
    Wave w = new_wave(1.0f);
    short *out = (short *)w.data;
    float low = 0;                                         // smoothed noise: a rumble, not a hiss
    for (unsigned int i = 0; i < w.frameCount; i++) {
        float t = (float)i / RATE;
        low += (noise() - low) * 0.05f;
        float thump = powf(fabsf(sinf(PI * 7.0f * t)), 6.0f);   // a sharp pulse, 7 times a second
        float v = low * 4.0f * (0.25f + thump) + 0.15f * sinf(2 * PI * 55.0f * t) * thump;
        if (v > 1) v = 1;
        if (v < -1) v = -1;
        out[i] = (short)(v * 0.8f * 32767);
    }
    return to_sound(w, 0.0f);                              // volume is set while it plays
}

// Rain: soft hiss with the odd louder drop. Two seconds, looped.
static Sound make_rain(void)
{
    Wave w = new_wave(2.0f);
    short *out = (short *)w.data;
    float low = 0;
    for (unsigned int i = 0; i < w.frameCount; i++) {
        low += (noise() - low) * 0.35f;
        float v = low * 0.5f;
        if ((seed >> 16) % 4000 == 0) v += noise();         // a drop
        if (v > 1) v = 1;
        if (v < -1) v = -1;
        out[i] = (short)(v * 0.6f * 32767);
    }
    return to_sound(w, 0.0f);
}

// The defibrillator charging: a whine that rises from 400 to 2400 Hz
static Sound make_charge(void)
{
    float len = 1.2f;
    Wave w = new_wave(len);
    short *out = (short *)w.data;
    float phase = 0;
    for (unsigned int i = 0; i < w.frameCount; i++) {
        float t = (float)i / RATE;
        float freq = 400.0f + 2000.0f * (t / len) * (t / len);
        phase += 2 * PI * freq / RATE;
        out[i] = (short)(sinf(phase) * edges(t, len, 0.02f, 0.05f) * 0.35f * 32767);
    }
    return to_sound(w, 0.25f);
}

// The shock: a short crack of noise with a low thump under it
static Sound make_zap(void)
{
    float len = 0.35f;
    Wave w = new_wave(len);
    short *out = (short *)w.data;
    for (unsigned int i = 0; i < w.frameCount; i++) {
        float t = (float)i / RATE;
        float v = noise() * expf(-12.0f * t) + 0.6f * sinf(2 * PI * 60.0f * t) * expf(-8.0f * t);
        if (v > 1) v = 1;
        if (v < -1) v = -1;
        out[i] = (short)(v * edges(t, len, 0.002f, 0.05f) * 0.8f * 32767);
    }
    return to_sound(w, 0.45f);
}

void sound_init(void)
{
    InitAudioDevice();
    ready = IsAudioDeviceReady();
    if (!ready) return;
    sounds[SND_SIREN]   = make_siren();
    sounds[SND_BEEP]    = make_beep();
    sounds[SND_CHIME]   = make_chime();
    sounds[SND_BUZZ]    = make_buzz();
    sounds[SND_SUCCESS] = make_success();
    sounds[SND_ROTOR]   = make_rotor();
    sounds[SND_RAIN]    = make_rain();
    sounds[SND_CHARGE]  = make_charge();
    sounds[SND_ZAP]     = make_zap();
}

void sound_close(void)
{
    if (!ready) return;
    for (int i = 0; i < SND_COUNT; i++) UnloadSound(sounds[i]);
    CloseAudioDevice();
    ready = false;
}

void sound_play(SoundId id)
{
    if (!ready || muted) return;
    if (id == SND_SIREN && IsSoundPlaying(sounds[id])) return;   // don't stack sirens
    PlaySound(sounds[id]);
}

void sound_loop(SoundId id, bool on, float volume)
{
    if (!ready) return;
    bool playing = IsSoundPlaying(sounds[id]);
    if (on && !muted) {
        SetSoundVolume(sounds[id], volume);
        if (!playing) PlaySound(sounds[id]);              // start it again each time it ends
    } else if (playing) {
        StopSound(sounds[id]);
    }
}

void sound_toggle_mute(void) { muted = !muted; }
bool sound_muted(void)       { return muted; }

static int    lastId = 0;       // highest patient id already checked for the siren
static double nextBeep = 0.0;

void sound_update(const Sim *sim, bool paused)
{
    // Siren: any new emergency coming by ambulance (not the helicopter ones)
    int newest = sim->nextId - 1;
    if (newest < lastId) lastId = newest;           // the day was restarted
    bool emergency = false;
    for (const Patient *p = sim->waiting; p != NULL; p = p->next) {
        if (p->id > lastId && p->severity == 1 && !p->rescue) emergency = true;
    }
    for (int b = 0; b < BED_COUNT; b++) {
        const Patient *p = sim->beds[b].occupant;
        if (p && p->id > lastId && p->severity == 1 && !p->rescue) emergency = true;
    }
    lastId = newest;
    if (emergency) sound_play(SND_SIREN);

    // Monitor alarm: one beep a second while an emergency patient in a bed
    // has no doctor with them yet
    if (paused) return;
    bool critical = false;
    for (int b = 0; b < BED_COUNT; b++) {
        const Patient *p = sim->beds[b].occupant;
        if (p && p->severity == 1 && p->walkLeft <= 0.0f && sim_doctor_at(sim, b) == -1) critical = true;
    }
    double now = GetTime();
    if (critical && now >= nextBeep) {
        sound_play(SND_BEEP);
        nextBeep = now + 1.0;
    }
}
