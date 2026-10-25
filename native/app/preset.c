/* SPDX-License-Identifier: MIT */
#include "mf.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static J *parameters(J *p) {
  if (!fields(p, "version,parameters") || !integer(get(p, "version")) ||
      json_object_get_int(get(p, "version")) != 1 ||
      !json_object_is_type(get(p, "parameters"), json_type_object)) {
    fail("Invalid preset document");
    return NULL;
  }
  return get(p, "parameters");
}
J *parse_preset(const char *text) {
  if (strlen(text) > 32768) {
    fail("Preset exceeds 32 KiB");
    return NULL;
  }
  if (!strncmp(text, "\xef\xbb\xbf", 3))
    text += 3;
  char *s = strdup(text);
  if (!s)
    return NULL;
  size_t n = 0;
  for (size_t i = 0; s[i]; i++) {
    if (s[i] == '\r' && s[i + 1] == '\n')
      continue;
    s[n++] = s[i];
  }
  s[n] = 0;
  if (n && s[n - 1] == '\n')
    s[--n] = 0;
  char *cursor = s, *line = strsep(&cursor, "\n");
  J *p = NULL, *params = NULL;
  if (!line || strcmp(line, "PresetVersion: 1")) {
    fail("Expected PresetVersion: 1");
    goto done;
  }
  p = json_object_new_object();
  params = json_object_new_object();
  put(p, "version", json_object_new_int(1));
  put(p, "parameters", params);
  while (cursor) {
    char *name = strsep(&cursor, "\n"), *value = strsep(&cursor, "\n");
    J *old = NULL;
    if (!name || !*name || !value || strchr(name, '\r') ||
        strchr(value, '\r') || json_object_object_get_ex(params, name, &old)) {
      fail("Malformed or duplicate preset parameter");
      json_object_put(p);
      p = NULL;
      break;
    }
    put(params, name, json_object_new_string(value));
  }
done:
  free(s);
  return p;
}
char *preset_text(J *p) {
  J *params = parameters(p);
  if (!params)
    return NULL;
  char *buf = NULL;
  size_t n = 0;
  FILE *f = open_memstream(&buf, &n);
  if (!f)
    return NULL;
  fprintf(f, "PresetVersion: 1\r\n");
  json_object_object_foreach(params, name, value) {
    if (!*name || !json_object_is_type(value, json_type_string) ||
        strpbrk(name, "\r\n") || strpbrk(str(value), "\r\n")) {
      fail("Preset values must be single-line strings");
      fclose(f);
      free(buf);
      return NULL;
    }
    fprintf(f, "%s\r\n%s\r\n", name, str(value));
  }
  fclose(f);
  if (n > 32768) {
    free(buf);
    fail("Preset exceeds 32 KiB");
    return NULL;
  }
  return buf;
}
static int norm(J *p, const char *key, float *value) {
  J *v = get(p, key);
  if (!v || !json_object_is_type(v, json_type_string))
    return fail("Missing preset value: %s", key);
  char *end = NULL;
  errno = 0;
  double d = strtod(str(v), &end);
  while (end && (*end == ' ' || *end == '\t'))
    end++;
  if (end == str(v) || !end || *end || errno == ERANGE || !isfinite(d) ||
      d < 0 || d > 1)
    return fail("Invalid normalized preset value: %s", key);
  *value = (float)d;
  return 0;
}
static int boolean(J *p, const char *key, bool *value) {
  float f;
  if (norm(p, key, &f))
    return -1;
  if (f != 0 && f != 1)
    return fail("Expected boolean: %s", key);
  *value = f != 0;
  return 0;
}
static void source(int i, const char **kind, int *index) {
  *kind = i < 2 ? "Input" : i == 2 ? "USBInput" : "VirtualInput";
  *index = i < 2 ? i : i == 2 ? 0 : i - 3;
}
static void key(char *buf, size_t n, const char *field, int mix,
                int source_index, int side) {
  const char *kind;
  int idx;
  source(source_index, &kind, &idx);
  if (side >= 0)
    snprintf(buf, n, "SLS_%s_%s_%d_%d_%c", kind, field, mix, idx,
             side ? 'R' : 'L');
  else
    snprintf(buf, n, "SLS_%s_%s_%d_%d", kind, field, mix, idx);
}
static void number_string(J *p, const char *k, float v) {
  char s[64];
  snprintf(s, sizeof s, "%.9g", (double)v);
  put(p, k, json_object_new_string(s));
}
static void ref_key(char *buf, size_t n, int m, int i) {
  if (i == 6)
    snprintf(buf, n, "SLS_Output_PeakRef_%d", m);
  else
    key(buf, n, "PeakRef", m, i, -1);
}
J *preset_controls(J *preset) {
  J *p = preset ? parameters(preset) : NULL;
  if (preset && !p)
    return NULL;
  J *o = json_object_new_object(), *links = json_object_new_array(),
    *visible = json_object_new_array();
  put(o, "links", links);
  put(o, "virtual_visible", visible);
  for (int m = 0; m < 3; m++) {
    char k[96];
    bool linked = false;
    if (p) {
      snprintf(k, sizeof k, "SLS_Input_Link_%d_0", m);
      if (boolean(p, k, &linked))
        goto bad;
      float a, b;
      key(k, sizeof k, "PeakRef", m, 0, -1);
      if (norm(p, k, &a))
        goto bad;
      key(k, sizeof k, "PeakRef", m, 1, -1);
      if (norm(p, k, &b))
        goto bad;
      if (linked && a != b) {
        fail("Linked PeakRef values differ");
        goto bad;
      }
    }
    json_object_array_add(links, json_object_new_boolean(linked));
    J *row = json_object_new_array();
    json_object_array_add(visible, row);
    for (int i = 0; i < 3; i++) {
      bool show = true;
      if (p) {
        snprintf(k, sizeof k, "SLS_VirtualInput_Show_%d_%d", m, i);
        if (boolean(p, k, &show))
          goto bad;
      }
      json_object_array_add(row, json_object_new_boolean(show));
    }
  }
  return o;
bad:
  json_object_put(o);
  return NULL;
}
int linked_valid(J *state, J *preset) {
  float w[6][10];
  if (matrix(state, w))
    return -1;
  J *c = preset_controls(preset);
  if (!c)
    return -1;
  J *links = get(c, "links");
  for (int m = 0; m < 3; m++)
    if (json_object_get_boolean(json_object_array_get_idx(links, m))) {
      J *strips =
            get(json_object_array_get_idx(get(state, "mixes"), m), "strips"),
        *a = json_object_array_get_idx(strips, 0),
        *b = json_object_array_get_idx(strips, 1);
      if (json_object_get_double(get(a, "level_db")) !=
              json_object_get_double(get(b, "level_db")) ||
          json_object_get_boolean(get(a, "mute")) !=
              json_object_get_boolean(get(b, "mute"))) {
        json_object_put(c);
        return fail("Linked inputs must have the same level and mute");
      }
    }
  json_object_put(c);
  return 0;
}
static int topology(J *p) {
  const char *kinds[] = {"Input", "USBInput", "VirtualInput"},
             *types[] = {"Level", "Mute", "Pan"};
  json_object_object_foreach(p, name, value) {
    (void)value;
    for (int k = 0; k < 3; k++)
      for (int t = 0; t < 3; t++) {
        char prefix[80];
        snprintf(prefix, sizeof prefix, "SLS_%s_%s_", kinds[k], types[t]);
        size_t n = strlen(prefix);
        if (strncmp(name, prefix, n))
          continue;
        char *end;
        errno = 0;
        long m = strtol(name + n, &end, 10);
        if (end == name + n || *end != '_')
          continue;
        char *start = end + 1;
        long i = strtol(start, &end, 10);
        if (end == start || (*end && strcmp(end, "_L") && strcmp(end, "_R")))
          continue;
        if (errno || m < 0 || m >= 3 || i < 0 || i >= (k == 0 ? 2 : 3))
          return fail("Unsupported preset topology: %s", name);
      }
  }
  return 0;
}
J *import_mixer(J *preset) {
  J *p = parameters(preset);
  if (!p || topology(p))
    return NULL;
  J *c = preset_controls(preset);
  if (!c)
    return NULL;
  json_object_put(c);
  J *s = factory_state();
  for (int m = 0; m < 3; m++) {
    char k[96];
    float v;
    snprintf(k, sizeof k, "SLS_Output_Level_%d", m);
    if (norm(p, k, &v))
      goto bad;
    J *mix = json_object_array_get_idx(get(s, "mixes"), m);
    put(mix, "master_db", json_object_new_double(normalized_db(v)));
    for (int i = 0; i < 6; i++) {
      J *strip = json_object_array_get_idx(get(mix, "strips"), i);
      key(k, sizeof k, "Level", m, i, -1);
      if (norm(p, k, &v))
        goto bad;
      put(strip, "level_db", json_object_new_double(normalized_db(v)));
      bool mute = false;
      key(k, sizeof k, "Mute", m, i, -1);
      if (boolean(p, k, &mute))
        goto bad;
      put(strip, "mute", json_object_new_boolean(mute));
      J *pans = get(strip, "pans");
      for (int ch = 0; ch < (i < 2 ? 1 : 2); ch++) {
        key(k, sizeof k, "Pan", m, i, i < 2 ? -1 : ch);
        if (norm(p, k, &v))
          goto bad;
        json_object_array_put_idx(pans, ch, json_object_new_double(v));
      }
    }
  }
  if (linked_valid(s, preset))
    goto bad;
  return s;
bad:
  json_object_put(s);
  return NULL;
}
J *export_mixer(J *state, J *preset) {
  if (linked_valid(state, preset))
    return NULL;
  J *check = import_mixer(preset);
  if (!check)
    return NULL;
  json_object_put(check);
  J *o = copy(preset), *p = parameters(o);
  for (int m = 0; m < 3; m++) {
    char k[96];
    J *mix = json_object_array_get_idx(get(state, "mixes"), m);
    snprintf(k, sizeof k, "SLS_Output_Level_%d", m);
    number_string(
        p, k,
        db_normalized((float)json_object_get_double(get(mix, "master_db"))));
    for (int i = 0; i < 6; i++) {
      J *strip = json_object_array_get_idx(get(mix, "strips"), i);
      key(k, sizeof k, "Level", m, i, -1);
      number_string(
          p, k,
          db_normalized((float)json_object_get_double(get(strip, "level_db"))));
      key(k, sizeof k, "Mute", m, i, -1);
      put(p, k,
          json_object_new_string(
              json_object_get_boolean(get(strip, "mute")) ? "1" : "0"));
      for (int ch = 0; ch < (i < 2 ? 1 : 2); ch++) {
        key(k, sizeof k, "Pan", m, i, i < 2 ? -1 : ch);
        number_string(p, k,
                      (float)json_object_get_double(
                          json_object_array_get_idx(get(strip, "pans"), ch)));
      }
    }
  }
  return o;
}
J *preset_refs(J *preset) {
  if (!preset)
    return default_refs();
  J *c = preset_controls(preset);
  if (!c)
    return NULL;
  json_object_put(c);
  J *p = parameters(preset), *a = json_object_new_array();
  for (int m = 0; m < 3; m++) {
    J *r = json_object_new_array();
    json_object_array_add(a, r);
    for (int i = 0; i < 7; i++) {
      char k[96];
      float v;
      ref_key(k, sizeof k, m, i);
      if (norm(p, k, &v)) {
        if (i < 2) {
          json_object_put(a);
          return NULL;
        }
        json_object_array_add(r, NULL);
      } else
        json_object_array_add(r, json_object_new_double(v));
    }
  }
  return a;
}
J *linked_edit(J *state, J *previous, J *preset) {
  float w[6][10];
  if (matrix(state, w))
    return NULL;
  J *s = copy(state), *c = preset_controls(preset);
  if (!c) {
    json_object_put(s);
    return NULL;
  }
  for (int m = 0; m < 3; m++)
    if (json_object_get_boolean(
            json_object_array_get_idx(get(c, "links"), m))) {
      J *pair = get(json_object_array_get_idx(get(s, "mixes"), m), "strips"),
        *old =
            get(json_object_array_get_idx(get(previous, "mixes"), m), "strips");
      const char *keys[] = {"level_db", "mute"};
      for (int k = 0; k < 2; k++) {
        bool changed[2];
        for (int i = 0; i < 2; i++)
          changed[i] = !json_object_equal(
              get(json_object_array_get_idx(pair, i), keys[k]),
              get(json_object_array_get_idx(old, i), keys[k]));
        if (changed[0] != changed[1]) {
          int i = changed[0] ? 0 : 1;
          put(json_object_array_get_idx(pair, 1 - i), keys[k],
              copy(get(json_object_array_get_idx(pair, i), keys[k])));
        }
      }
    }
  json_object_put(c);
  if (linked_valid(s, preset)) {
    json_object_put(s);
    return NULL;
  }
  return s;
}
int change_link(J **state, J **preset, int m, bool enabled) {
  if (m < 0 || m >= 3 || !*preset)
    return fail("Import .mfp before changing Link");
  if (linked_valid(*state, *preset))
    return -1;
  J *s = copy(*state), *p = copy(*preset), *params = parameters(p),
    *pair = get(json_object_array_get_idx(get(s, "mixes"), m), "strips");
  char k[96];
  if (enabled) {
    float level =
        fminf(json_object_get_double(
                  get(json_object_array_get_idx(pair, 0), "level_db")),
              json_object_get_double(
                  get(json_object_array_get_idx(pair, 1), "level_db")));
    bool mute = json_object_get_boolean(
                    get(json_object_array_get_idx(pair, 0), "mute")) &&
                json_object_get_boolean(
                    get(json_object_array_get_idx(pair, 1), "mute"));
    float a, b;
    key(k, sizeof k, "PeakRef", m, 0, -1);
    if (norm(params, k, &a))
      goto bad;
    key(k, sizeof k, "PeakRef", m, 1, -1);
    if (norm(params, k, &b))
      goto bad;
    for (int i = 0; i < 2; i++) {
      J *v = json_object_array_get_idx(pair, i);
      put(v, "level_db", json_object_new_double(level));
      put(v, "mute", json_object_new_boolean(mute));
      key(k, sizeof k, "Level", m, i, -1);
      number_string(params, k, db_normalized(level));
      key(k, sizeof k, "Mute", m, i, -1);
      put(params, k, json_object_new_string(mute ? "1" : "0"));
      key(k, sizeof k, "PeakRef", m, i, -1);
      number_string(params, k, fminf(a, b));
    }
  }
  snprintf(k, sizeof k, "SLS_Input_Link_%d_0", m);
  put(params, k, json_object_new_string(enabled ? "1" : "0"));
  if (linked_valid(s, p))
    goto bad;
  json_object_put(*state);
  json_object_put(*preset);
  *state = s;
  *preset = p;
  return 0;
bad:
  json_object_put(s);
  json_object_put(p);
  return -1;
}
int change_virtual(J **state, J **preset, int m, int i, bool visible) {
  if (m < 0 || m >= 3 || i < 0 || i >= 3 || !*preset)
    return fail("Invalid virtual channel or missing preset");
  if (linked_valid(*state, *preset))
    return -1;
  J *s = copy(*state), *p = copy(*preset), *params = parameters(p);
  char k[96];
  snprintf(k, sizeof k, "SLS_VirtualInput_Show_%d_%d", m, i);
  put(params, k, json_object_new_string(visible ? "1" : "0"));
  if (!visible) {
    J *v = json_object_array_get_idx(
        get(json_object_array_get_idx(get(s, "mixes"), m), "strips"), i + 3);
    put(v, "level_db", json_object_new_double(0));
    put(v, "mute", json_object_new_boolean(true));
    J *pans = json_object_new_array();
    json_object_array_add(pans, json_object_new_double(0));
    json_object_array_add(pans, json_object_new_double(1));
    put(v, "pans", pans);
    key(k, sizeof k, "Level", m, i + 3, -1);
    put(params, k, json_object_new_string("1"));
    key(k, sizeof k, "Mute", m, i + 3, -1);
    put(params, k, json_object_new_string("1"));
    for (int ch = 0; ch < 2; ch++) {
      key(k, sizeof k, "Pan", m, i + 3, ch);
      put(params, k, json_object_new_string(ch ? "1" : "0"));
    }
  }
  if (linked_valid(s, p)) {
    json_object_put(s);
    json_object_put(p);
    return -1;
  }
  json_object_put(*state);
  json_object_put(*preset);
  *state = s;
  *preset = p;
  return 0;
}
int change_ref(J *preset, int m, int i, float v) {
  int db;
  if (m < 0 || m >= 3 || i < 0 || i >= 7 || !isfinite(v) || !ref_db(v, &db))
    return fail("Invalid PeakRef mark");
  J *p = parameters(preset), *c = preset_controls(preset);
  if (!p || !c)
    return -1;
  char k[96];
  ref_key(k, sizeof k, m, i);
  number_string(p, k, v);
  if (i < 2 &&
      json_object_get_boolean(json_object_array_get_idx(get(c, "links"), m))) {
    ref_key(k, sizeof k, m, 1 - i);
    number_string(p, k, v);
  }
  json_object_put(c);
  return 0;
}
int rename_field(J *preset, const char *kind, int i, const char *name) {
  static const char *inputs[] = {
      "SLS_Input_Name_0",        "SLS_Input_Name_1",
      "SLS_USBInput_Name_0",     "SLS_VirtualInput_Name_0",
      "SLS_VirtualInput_Name_1", "SLS_VirtualInput_Name_2"};
  bool input = !strcmp(kind, "input");
  if ((!input && strcmp(kind, "mix")) || i < 0 || i >= (input ? 6 : 3))
    return fail("Invalid name field");
  size_t count = 0;
  const unsigned char *p = (const unsigned char *)name;
  while (*p) {
    uint32_t cp;
    unsigned n;
    if (*p < 0x80) {
      cp = *p;
      n = 1;
    } else if (*p >= 0xc2 && *p <= 0xdf) {
      cp = *p & 31;
      n = 2;
    } else if (*p >= 0xe0 && *p <= 0xef) {
      cp = *p & 15;
      n = 3;
    } else if (*p >= 0xf0 && *p <= 0xf4) {
      cp = *p & 7;
      n = 4;
    } else
      return fail("Invalid UTF-8 name");
    for (unsigned j = 1; j < n; j++) {
      if (!p[j] || (p[j] & 0xc0) != 0x80)
        return fail("Invalid UTF-8 name");
      cp = (cp << 6) | (p[j] & 63);
    }
    if ((n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000) || cp > 0x10ffff ||
        (cp >= 0xd800 && cp <= 0xdfff) || cp < 32 || (cp >= 127 && cp <= 159) ||
        cp == 0x2028 || cp == 0x2029 || ++count > 128)
      return fail("Name must be one line, at most 128 characters");
    p += n;
  }
  J *params = parameters(preset);
  if (!params)
    return -1;
  char k[64];
  snprintf(k, sizeof k, "SLS_Mix_Name_%d", i);
  put(params, input ? inputs[i] : k, json_object_new_string(name));
  return 0;
}
