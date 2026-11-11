/* SPDX-License-Identifier: MIT */
#include "mf.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float db_gain(float db) {
  return db > (float)-59.9 ? (float)pow(10, (float)(db * (float).05)) : 0;
}
static float quantized(float a) {
  int q = a == 0
              ? -32768
              : (int)fmax(-32768,
                          fmin(32767,
                               (double)(float)((float)log10(a) * 20.0f) * 256));
  float db = fmaxf(-240, fminf(0, q / 256.0f));
  return (float)pow(10, (float)(db * (float).05));
}
J *factory_state(void) {
  J *s = json_object_new_object(), *mixes = json_object_new_array();
  put(s, "version", json_object_new_int(2));
  put(s, "mixes", mixes);
  for (int m = 0; m < 3; m++) {
    J *mix = json_object_new_object(), *strips = json_object_new_array();
    put(mix, "master_db", json_object_new_double(0));
    put(mix, "master_mute", json_object_new_boolean(false));
    put(mix, "dim", json_object_new_boolean(false));
    put(mix, "strips", strips);
    json_object_array_add(mixes, mix);
    for (int i = 0; i < 6; i++) {
      J *strip = json_object_new_object(), *pans = json_object_new_array();
      put(strip, "level_db", json_object_new_double(0));
      put(strip, "mute", json_object_new_boolean(i != m + 2));
      put(strip, "solo", json_object_new_boolean(false));
      put(strip, "pans", pans);
      json_object_array_add(pans, json_object_new_double(i < 2 ? .5 : 0));
      if (i >= 2)
        json_object_array_add(pans, json_object_new_double(1));
      json_object_array_add(strips, strip);
    }
  }
  return s;
}
int matrix(J *s, float out[6][10]) {
  int version = json_object_get_int(get(s, "version"));
  if (!fields(s, "version,mixes") || !integer(get(s, "version")) ||
      (version != 1 && version != 2))
    return fail("Expected mixer version 1 or 2");
  J *mixes = get(s, "mixes");
  if (!json_object_is_type(mixes, json_type_array) ||
      json_object_array_length(mixes) != 3)
    return fail("Expected three mixes");
  memset(out, 0, 60 * sizeof(float));
  for (int m = 0; m < 3; m++) {
    J *mix = json_object_array_get_idx(mixes, m), *strips = get(mix, "strips");
    if (!fields(mix, version == 1 ? "master_db,strips"
                                  : "master_db,master_mute,dim,strips") ||
        !numeric(get(mix, "master_db"), -60, 0) ||
        (version == 2 &&
         (!json_object_is_type(get(mix, "master_mute"), json_type_boolean) ||
          !json_object_is_type(get(mix, "dim"), json_type_boolean))) ||
        !json_object_is_type(strips, json_type_array) ||
        json_object_array_length(strips) != 6)
      return fail("Invalid mix");
    float master =
        db_gain((float)json_object_get_double(get(mix, "master_db")));
    if (json_object_get_boolean(get(mix, "master_mute")))
      master = 0;
    if (json_object_get_boolean(get(mix, "dim")))
      master *= .1f;
    bool any_solo = false;
    for (int i = 0; i < 6; i++)
      any_solo |= json_object_get_boolean(
          get(json_object_array_get_idx(strips, i), "solo"));
    int col = 0;
    for (int i = 0; i < 6; i++) {
      J *strip = json_object_array_get_idx(strips, i),
        *pans = get(strip, "pans");
      int count = i < 2 ? 1 : 2;
      if (!fields(strip, version == 1 ? "level_db,mute,pans"
                                      : "level_db,mute,solo,pans") ||
          !numeric(get(strip, "level_db"), -60, 0) ||
          !json_object_is_type(get(strip, "mute"), json_type_boolean) ||
          (version == 2 &&
           !json_object_is_type(get(strip, "solo"), json_type_boolean)) ||
          !json_object_is_type(pans, json_type_array) ||
          (int)json_object_array_length(pans) != count)
        return fail("Invalid strip");
      float gain =
          db_gain((float)json_object_get_double(get(strip, "level_db")));
      bool muted = json_object_get_boolean(get(strip, "mute")) ||
                   (any_solo && !json_object_get_boolean(get(strip, "solo")));
      for (int c = 0; c < count; c++, col++) {
        J *p = json_object_array_get_idx(pans, c);
        if (!numeric(p, 0, 1))
          return fail("Invalid pan");
        float pan = (float)json_object_get_double(p),
              angle = (float)(pan * (3.14159265358979323846 / 2));
        float l = pan == 1 ? 0 : (float)((float)cos(angle) * gain),
              r = pan == 0 ? 0 : (float)((float)sin(angle) * gain);
        out[2 * m][col] = muted ? 0 : (float)(quantized(l) * master);
        out[2 * m + 1][col] = muted ? 0 : (float)(quantized(r) * master);
      }
    }
  }
  return 0;
}
float normalized_db(float x) {
  float scale = (float)(1.0f / (float)((float)exp(-2.0f) - 1.0f));
  float unit = (float)((float)((float)exp((float)(x * -2.0f)) - 1.0f) * scale);
  return (float)((float)(unit * 60.0f) - 60.0f);
}
float db_normalized(float x) {
  float scale = (float)(1.0f / (float)((float)exp(-2.0f) - 1.0f));
  float unit = (float)((float)(x + 60.0f) / 60.0f);
  float v = (float)((float)log((float)((float)(unit / scale) + 1.0f)) / -2.0f);
  return fmaxf(0, fminf(1, v));
}
J *default_refs(void) {
  J *a = json_object_new_array();
  for (int m = 0; m < 3; m++) {
    J *r = json_object_new_array();
    for (int i = 0; i < 7; i++)
      json_object_array_add(r, json_object_new_double(0));
    json_object_array_add(a, r);
  }
  return a;
}
int validate_refs(J *a) {
  if (!json_object_is_type(a, json_type_array) ||
      json_object_array_length(a) != 3)
    return fail("Expected three reference rows");
  for (int m = 0; m < 3; m++) {
    J *r = json_object_array_get_idx(a, m);
    if (!json_object_is_type(r, json_type_array) ||
        json_object_array_length(r) != 7)
      return fail("Expected seven reference values");
    for (int i = 0; i < 7; i++) {
      J *v = json_object_array_get_idx(r, i);
      if (v && !numeric(v, 0, 1))
        return fail("Invalid reference");
    }
  }
  return 0;
}
static const int ref_marks[] = {0, -6, -12, -18, -24, -30, -36, -42, -60};
static const uint32_t ref_bits[] = {0,          0x3e295810, 0x3ea371b9,
                                    0x3ef42f40, 0x3f1c8dc1, 0x3f3c0fbb,
                                    0x3f54ad47, 0x3f67635e, 0x3f7f03f3};
float ref_normalized(int db) {
  for (int i = 0; i < 9; i++)
    if (ref_marks[i] == db) {
      float f;
      memcpy(&f, &ref_bits[i], 4);
      return f;
    }
  return NAN;
}
bool ref_db(float v, int *db) {
  for (int i = 0; i < 9; i++)
    if (v == ref_normalized(ref_marks[i])) {
      *db = ref_marks[i];
      return true;
    }
  return false;
}
J *document(J *state, J *preset, J *refs) {
  float weights[6][10];
  if (matrix(state, weights) || validate_refs(refs) ||
      linked_valid(state, preset))
    return NULL;
  if (preset) {
    J *expected = preset_refs(preset);
    if (!expected)
      return NULL;
    bool equal = json_object_equal(expected, refs);
    json_object_put(expected);
    if (!equal) {
      fail("Preset references differ from metadata");
      return NULL;
    }
  }
  J *d = json_object_new_object();
  put(d, "version", json_object_new_int(4));
  put(d, "mixer", copy(state));
  put(d, "mfp_template", preset_is_native(preset) ? NULL : copy(preset));
  put(d, "peakrefs", copy(refs));
  put(d, "presentation", preset_presentation(preset));
  return d;
}
int unpack_document(J *d, J **state, J **preset, J **refs) {
  *state = *preset = *refs = NULL;
  J *s = d, *p = NULL, *r = NULL;
  J *v = get(d, "version");
  int version = integer(v) ? json_object_get_int(v) : 0;
  if ((version == 2 && !fields(d, "version,mixes")) || version == 3 ||
      version == 4) {
    if (!fields(d, version == 2 ? "version,mixer,mfp_template"
                   : version == 3
                       ? "version,mixer,mfp_template,peakrefs"
                       : "version,mixer,mfp_template,peakrefs,presentation"))
      return fail("Invalid document fields");
    s = get(d, "mixer");
    p = get(d, "mfp_template");
    if (version == 2 && !p)
      return fail("Version 2 requires a template");
    if (p) {
      J *check = import_mixer(p);
      if (!check)
        return -1;
      json_object_put(check);
    }
    r = version >= 3 ? copy(get(d, "peakrefs")) : preset_refs(p);
  } else
    r = default_refs();
  float w[6][10];
  if (!r || matrix(s, w) || validate_refs(r) || linked_valid(s, p)) {
    if (r)
      json_object_put(r);
    return -1;
  }
  if (p) {
    J *expected = preset_refs(p);
    if (!expected) {
      json_object_put(r);
      return -1;
    }
    bool eq = json_object_equal(expected, r);
    json_object_put(expected);
    if (!eq) {
      json_object_put(r);
      return fail("Preset references differ from metadata");
    }
  }
  if (version == 4 && !get(d, "presentation")) {
    json_object_put(r);
    return fail("Missing Linux presentation");
  }
  J *native = native_preset(s, r, version == 4 ? get(d, "presentation") : NULL);
  if (!native) {
    json_object_put(r);
    return -1;
  }
  if (version == 4 && p) {
    J *presentation = preset_presentation(p);
    bool same =
        presentation && json_object_equal(presentation, get(d, "presentation"));
    if (presentation)
      json_object_put(presentation);
    if (!same) {
      json_object_put(native);
      json_object_put(r);
      return fail("Windows template and Linux presentation differ");
    }
  }
  *state = copy(s);
  if (json_object_get_int(get(s, "version")) == 1) {
    put(*state, "version", json_object_new_int(2));
    J *mixes = get(*state, "mixes");
    for (int m = 0; m < 3; m++) {
      J *mix = json_object_array_get_idx(mixes, m);
      put(mix, "master_mute", json_object_new_boolean(false));
      put(mix, "dim", json_object_new_boolean(false));
      for (int i = 0; i < 6; i++)
        put(json_object_array_get_idx(get(mix, "strips"), i), "solo",
            json_object_new_boolean(false));
    }
  }
  *preset = p ? copy(p) : json_object_get(native);
  json_object_put(native);
  *refs = r;
  return 0;
}
