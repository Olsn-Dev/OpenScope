#pragma once
#include <stdint.h>

// ─── Club data ────────────────────────────────────────────────────────────────

struct Club {
    const char* name;
    const char* abbr;
    float carry_f;      // carry [m] = ball_kmh × carry_f  (at typical launch)
    float roll_f;       // roll  [m] = carry × roll_f
    float typ_launch;   // typical launch angle [°] for this club
};

struct ClubStats {
    uint32_t count;
    float    sum;    // sum of carry distances (for avg)
    float    max_c;
    float    min_c;
};

#define NUM_CLUBS 14

// Defined in clubs.cpp
extern const Club CLUBS[NUM_CLUBS];
