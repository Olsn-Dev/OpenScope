#include <Arduino.h>
#include <Preferences.h>
#include "storage.h"

static Preferences prefs;

void nvs_load(float& threshold, bool& use_mph, int& club,
              bool& blue_theme, int& layout,
              ClubStats* stats, int num_clubs)
{
    prefs.begin("openscope", false);
    threshold  = prefs.getFloat("thresh", 80.0f);
    use_mph    = prefs.getBool("mph",    false);
    club       = (int)prefs.getUInt("club", 0);
    blue_theme = prefs.getBool("blue", false);
    layout     = (int)prefs.getUInt("layout", 0);
    if (club < 0 || club >= num_clubs) club = 0;
    for (int i = 0; i < num_clubs; i++) {
        char k[10];
        snprintf(k, sizeof(k), "c%d_n",  i); stats[i].count = prefs.getUInt(k,  0);
        snprintf(k, sizeof(k), "c%d_s",  i); stats[i].sum   = prefs.getFloat(k, 0.0f);
        snprintf(k, sizeof(k), "c%d_mx", i); stats[i].max_c = prefs.getFloat(k, 0.0f);
        snprintf(k, sizeof(k), "c%d_mn", i); stats[i].min_c = prefs.getFloat(k, 9999.0f);
    }
    prefs.end();
}

void nvs_save_settings(float threshold, bool use_mph, int club,
                       bool blue_theme, int layout)
{
    prefs.begin("openscope", false);
    prefs.putFloat("thresh", threshold);
    prefs.putBool("mph",     use_mph);
    prefs.putUInt("club",    (uint32_t)club);
    prefs.putBool("blue",    blue_theme);
    prefs.putUInt("layout",  (uint32_t)layout);
    prefs.end();
}

void nvs_save_stats(int idx, const ClubStats& s)
{
    prefs.begin("openscope", false);
    char k[10];
    snprintf(k, sizeof(k), "c%d_n",  idx); prefs.putUInt(k,  s.count);
    snprintf(k, sizeof(k), "c%d_s",  idx); prefs.putFloat(k, s.sum);
    snprintf(k, sizeof(k), "c%d_mx", idx); prefs.putFloat(k, s.max_c);
    snprintf(k, sizeof(k), "c%d_mn", idx); prefs.putFloat(k, s.min_c);
    prefs.end();
}

void record_carry(int club_idx, float carry, ClubStats* stats)
{
    ClubStats& s = stats[club_idx];
    s.count++;
    s.sum += carry;
    if (carry > s.max_c) s.max_c = carry;
    if (carry < s.min_c) s.min_c = carry;
    nvs_save_stats(club_idx, s);
}

void reset_stats(int idx, ClubStats* stats)
{
    stats[idx] = { 0, 0.0f, 0.0f, 9999.0f };
    nvs_save_stats(idx, stats[idx]);
    Serial.printf("[NVS] Stats reset: club %d\n", idx);
}

// ─── Shot history ─────────────────────────────────────────────────────────────
// The whole log is one blob keyed by record size, so a future ShotRecord layout
// change silently discards an old-format log instead of misreading it.

static void save_history(const ShotRecord* shots, int count)
{
    prefs.begin("openscope", false);
    prefs.putUInt("hist_n", (uint32_t)count);
    if (count > 0) prefs.putBytes("hist", shots, count * sizeof(ShotRecord));
    prefs.end();
}

void nvs_load_history(ShotRecord* shots, int& count)
{
    prefs.begin("openscope", false);
    count = (int)prefs.getUInt("hist_n", 0);
    if (count < 0 || count > HISTORY_MAX) count = 0;
    if (count > 0) {
        size_t want = count * sizeof(ShotRecord);
        size_t got  = prefs.getBytes("hist", shots, want);
        if (got != want) count = 0;
    }
    prefs.end();
}

void record_shot(const ShotRecord& rec, ShotRecord* shots, int& count)
{
    if (count >= HISTORY_MAX) {
        memmove(shots, shots + 1, (HISTORY_MAX - 1) * sizeof(ShotRecord));
        count = HISTORY_MAX - 1;
    }
    shots[count++] = rec;
    save_history(shots, count);
}

void clear_history(ShotRecord* shots, int& count)
{
    (void)shots;
    count = 0;
    prefs.begin("openscope", false);
    prefs.remove("hist");
    prefs.putUInt("hist_n", 0);
    prefs.end();
    Serial.println("[NVS] Shot history cleared");
}

// ─── Touch calibration ────────────────────────────────────────────────────────

bool nvs_load_touch_cal(uint16_t cal[5])
{
    prefs.begin("openscope", false);
    bool   ok  = prefs.getBool("tcal_ok", false);
    size_t got = ok ? prefs.getBytes("tcal", cal, 5 * sizeof(uint16_t)) : 0;
    prefs.end();
    return ok && got == 5 * sizeof(uint16_t);
}

void nvs_save_touch_cal(const uint16_t cal[5])
{
    prefs.begin("openscope", false);
    prefs.putBytes("tcal", cal, 5 * sizeof(uint16_t));
    prefs.putBool("tcal_ok", true);
    prefs.end();
    Serial.println("[NVS] Touch calibration saved");
}
