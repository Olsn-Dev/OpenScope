#pragma once
#include "clubs.h"

// ─── NVS persistence ──────────────────────────────────────────────────────────
// All functions open and close the "openscope" NVS namespace internally.

// Load all persisted settings and per-club stats into the provided variables.
void nvs_load(float& threshold, bool& use_mph, int& club,
              ClubStats* stats, int num_clubs);

// Persist the four settings scalars.
void nvs_save_settings(float threshold, bool use_mph, int club);

// Persist one club's stats entry.
void nvs_save_stats(int idx, const ClubStats& s);

// Record a carry distance for club idx, update stats, and persist.
void record_carry(int club_idx, float carry, ClubStats* stats);

// Zero out stats for club idx and persist.
void reset_stats(int idx, ClubStats* stats);
