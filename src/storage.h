#pragma once
#include "clubs.h"

// ─── NVS persistence ──────────────────────────────────────────────────────────
// All functions open and close the "openscope" NVS namespace internally.

// Load all persisted settings and per-club stats into the provided variables.
// blue_theme / layout are the LM1 UI preferences (default Black / Advanced).
void nvs_load(float& threshold, bool& use_mph, int& club,
              bool& blue_theme, int& layout,
              ClubStats* stats, int num_clubs);

// Persist the settings scalars (units, club, theme, layout, threshold).
void nvs_save_settings(float threshold, bool use_mph, int club,
                       bool blue_theme, int layout);

// Persist one club's stats entry.
void nvs_save_stats(int idx, const ClubStats& s);

// Record a carry distance for club idx, update stats, and persist.
void record_carry(int club_idx, float carry, ClubStats* stats);

// Zero out stats for club idx and persist.
void reset_stats(int idx, ClubStats* stats);

// ─── Shot history ─────────────────────────────────────────────────────────────
// A ring of the most recent HISTORY_MAX shots across all clubs, persisted as a
// single NVS blob. Kept oldest→newest in RAM; the UI renders it newest-first.

#define HISTORY_MAX 50

struct ShotRecord {
    uint8_t club;       // index into CLUBS[]
    float   ball_kmh;
    float   club_kmh;   // 0 = club head not detected (smash unavailable)
    float   carry_m;
    float   total_m;
};

// Load the persisted shot log into shots[HISTORY_MAX]; sets count (0 if none
// stored, or if the stored blob doesn't match the current record size).
void nvs_load_history(ShotRecord* shots, int& count);

// Append a shot (dropping the oldest once full) and persist the log.
void record_shot(const ShotRecord& rec, ShotRecord* shots, int& count);

// Erase the shot log (RAM + NVS).
void clear_history(ShotRecord* shots, int& count);

// ─── Touch calibration (XPT2046) ──────────────────────────────────────────────

// Load the 5-value TFT_eSPI touch calibration blob.
// Returns true if a valid calibration was previously stored.
bool nvs_load_touch_cal(uint16_t cal[5]);

// Persist the 5-value touch calibration blob.
void nvs_save_touch_cal(const uint16_t cal[5]);
