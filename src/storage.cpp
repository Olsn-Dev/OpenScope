#include <Arduino.h>
#include <Preferences.h>
#include "storage.h"

static Preferences prefs;

void nvs_load(float& threshold, bool& use_mph, int& club,
              ClubStats* stats, int num_clubs)
{
    prefs.begin("openscope", false);
    threshold = prefs.getFloat("thresh", 80.0f);
    use_mph   = prefs.getBool("mph",    false);
    club      = (int)prefs.getUInt("club", 0);
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

void nvs_save_settings(float threshold, bool use_mph, int club)
{
    prefs.begin("openscope", false);
    prefs.putFloat("thresh", threshold);
    prefs.putBool("mph",     use_mph);
    prefs.putUInt("club",    (uint32_t)club);
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
