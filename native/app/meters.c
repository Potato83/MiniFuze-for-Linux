/* SPDX-License-Identifier: MIT */
#include "mf.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

float meter_position(float a) {
  if (a <= (float).00031622776)
    return 0;
  static const float limits[] = {.001f,       .0031622776f, .01f,
                                 .031622775f, .1f,          INFINITY};
  static const float slopes[] = {.1f, .05f, .15f, .3f, .4f, .5f},
                     offsets[] = {.35f, .2f, .45f, .75f, .9f, 1};
  float log = (float)log10(a);
  for (int i = 0; i < 6; i++)
    if (a <= limits[i])
      return (float)((float)(log * slopes[i]) + offsets[i]);
  return 0;
}
double position_amplitude(double p) {
  if (p <= 0)
    return 0;
  static const double ceilings[] = {.05, .075, .15, .3, .5, INFINITY},
                      slopes[] = {.1, .05, .15, .3, .4, .5},
                      offsets[] = {.35, .2, .45, .75, .9, 1};
  for (int i = 0; i < 6; i++)
    if (p <= ceilings[i])
      return pow(10, (p - offsets[i]) / slopes[i]);
  return 0;
}
J *default_preferences(void) {
  J *o = json_object_new_object();
  put(o, "version", json_object_new_int(1));
  put(o, "orange_db", json_object_new_int(-6));
  put(o, "clip_reset", json_object_new_string("infinite"));
  put(o, "peak_reset", json_object_new_string("none"));
  return o;
}
static bool one_of(const char *s, const char *const *values, int n) {
  for (int i = 0; i < n; i++)
    if (!strcmp(s, values[i]))
      return true;
  return false;
}
int validate_preferences(J *p) {
  if (!fields(p, "version,orange_db,clip_reset,peak_reset") ||
      !integer(get(p, "version")) ||
      json_object_get_int(get(p, "version")) != 1 ||
      !integer(get(p, "orange_db")))
    return fail("Invalid meter preferences");
  int db = json_object_get_int(get(p, "orange_db"));
  if (db != -1 && db != -3 && db != -6 && db != -9 && db != -12)
    return fail("Invalid orange threshold");
  static const char *clips[] = {"infinite", "1.5s", "3s"},
                    *peaks[] = {"none", "1s", "2s",      "3s",
                                "4s",   "5s", "infinite"};
  if (!one_of(str(get(p, "clip_reset")), clips, 3) ||
      !one_of(str(get(p, "peak_reset")), peaks, 7))
    return fail("Invalid meter reset mode");
  return 0;
}
void ballistics_reset(Ballistics *b) {
  memset(b->bar, 0, sizeof b->bar);
  memset(b->peak, 0, sizeof b->peak);
  memset(b->targets, 0, sizeof b->targets);
  memset(b->counter, 0, sizeof b->counter);
  memset(b->clipped, 0, sizeof b->clipped);
  for (int i = 0; i < 16; i++)
    b->clip_at[i] = NAN;
  b->tick_at = 0;
}
void ballistics_configure(Ballistics *b, J *prefs, int channels) {
  J *p = copy(prefs);
  if (b->preferences)
    json_object_put(b->preferences);
  b->preferences = p;
  b->channels = channels;
  ballistics_reset(b);
}
void ballistics_advance(Ballistics *b, double now) {
  if (!b->tick_at)
    b->tick_at = now;
  long ticks = (long)fmax(0, floor((now - b->tick_at) / .04 + 1e-8));
  const char *peak = str(get(b->preferences, "peak_reset"));
  int hold = (!strcmp(peak, "none") || !strcmp(peak, "infinite"))
                 ? 0
                 : atoi(peak) * 25;
  if (ticks) {
    for (long t = 0; t < (ticks > 256 ? 256 : ticks); t++)
      for (int i = 0; i < b->channels; i++) {
        double previous = b->bar[i], target = b->targets[i];
        b->bar[i] = target;
        if (previous > target) {
          float candidate = (float)(previous - (float).1);
          if (candidate >= target)
            b->bar[i] = fmin(1, candidate);
        }
        if (b->peak[i] <= target) {
          b->peak[i] = target;
          b->counter[i] = hold;
        } else if (strcmp(peak, "infinite")) {
          if (b->counter[i] > 0)
            b->counter[i]--;
          else {
            float candidate = (float)(b->peak[i] - (float).1);
            b->peak[i] = candidate >= target ? fmin(1, candidate) : target;
          }
        }
      }
    b->tick_at += ticks * .04;
  }
  const char *clip = str(get(b->preferences, "clip_reset"));
  if (strcmp(clip, "infinite"))
    for (int i = 0; i < b->channels; i++)
      if (isfinite(b->clip_at[i]) && now - b->clip_at[i] >= atof(clip)) {
        b->clipped[i] = false;
        b->clip_at[i] = NAN;
      }
}
void ballistics_update(Ballistics *b, const float *levels, double now) {
  ballistics_advance(b, now);
  const char *peak = str(get(b->preferences, "peak_reset"));
  int hold = (!strcmp(peak, "none") || !strcmp(peak, "infinite"))
                 ? 0
                 : atoi(peak) * 25;
  for (int i = 0; i < b->channels; i++) {
    double target = meter_position(levels[i]);
    b->targets[i] = target;
    b->bar[i] = fmax(b->bar[i], target);
    if (b->peak[i] <= target) {
      b->peak[i] = target;
      b->counter[i] = hold;
    }
    if (levels[i] >= .999f) {
      b->clipped[i] = true;
      b->clip_at[i] = NAN;
    } else if (b->clipped[i] && !isfinite(b->clip_at[i]))
      b->clip_at[i] = now;
  }
}
J *ballistics_view(Ballistics *b, bool active) {
  const char *keys[] = {"display_positions", "peak_positions", "display_levels",
                        "peaks", "clips"};
  J *o = json_object_new_object();
  for (int j = 0; j < 5; j++) {
    J *a = json_object_new_array();
    for (int i = 0; i < b->channels; i++) {
      J *v = j == 4 ? json_object_new_boolean(active && b->clipped[i])
                    : json_object_new_double(
                          !active  ? 0
                          : j == 0 ? b->bar[i]
                          : j == 1 ? b->peak[i]
                                   : position_amplitude(j == 2 ? b->bar[i]
                                                               : b->peak[i]));
      json_object_array_add(a, v);
    }
    put(o, keys[j], a);
  }
  put(o, "preferences", copy(b->preferences));
  return o;
}
