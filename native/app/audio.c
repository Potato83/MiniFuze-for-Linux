/* SPDX-License-Identifier: MIT */
#include "mf.h"
#include <ctype.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const int rates[] = {44100, 48000, 88200, 96000, 176400, 192000};
static const int quanta[] = {32, 64, 128, 256, 512, 1024, 2048};
static const char *keys[] = {"clock.force-quantum", "clock.force-rate"};
static bool member(int value, const int *list, size_t n) {
  for (size_t i = 0; i < n; i++)
    if (value == list[i])
      return true;
  return false;
}
static bool number_text(const char *s) {
  if (!*s)
    return false;
  for (; *s; s++)
    if (!isdigit((unsigned char)*s))
      return false;
  return true;
}
static J *metadata(void) {
  char *argv[] = {"pw-metadata", "-n", "settings", NULL};
  char *s = run(argv, 10000);
  if (!s)
    return NULL;
  J *o = json_object_new_object();
  char *save = NULL;
  for (char *line = strtok_r(s, "\n", &save); line;
       line = strtok_r(NULL, "\n", &save)) {
    char *p = strstr(line, "update: id:0 key:'");
    if (!p)
      continue;
    p += 18;
    char *end = strchr(p, '\'');
    if (!end)
      continue;
    *end = 0;
    char *value = strstr(end + 1, "value:'");
    if (!value)
      continue;
    value += 7;
    char *ve = strchr(value, '\'');
    if (!ve)
      continue;
    *ve = 0;
    char *kind = strstr(ve + 1, "type:'");
    if (!kind)
      continue;
    kind += 6;
    char *ke = strchr(kind, '\'');
    if (!ke)
      continue;
    *ke = 0;
    J *pair = json_object_new_array();
    json_object_array_add(pair, json_object_new_string(value));
    json_object_array_add(pair, json_object_new_string(kind));
    put(o, p, pair);
  }
  free(s);
  const char *required[] = {"clock.force-quantum", "clock.force-rate",
                            "clock.min-quantum", "clock.max-quantum"};
  for (int i = 0; i < 4; i++) {
    J *pair = get(o, required[i]);
    if (!pair || !number_text(str(json_object_array_get_idx(pair, 0)))) {
      json_object_put(o);
      fail("Cannot read PipeWire clock settings");
      return NULL;
    }
  }
  return o;
}
static int setting(J *m, const char *key) {
  return atoi(str(json_object_array_get_idx(get(m, key), 0)));
}
static int write_setting(const char *key, const char *value, const char *kind) {
  if (strcmp(key, keys[0]) && strcmp(key, keys[1]))
    return fail("Unsupported audio setting");
  char *argv[] = {"pw-metadata", "-n",          "settings",   "0",
                  (char *)key,   (char *)value, (char *)kind, NULL};
  char *s = run(argv, 10000);
  if (!s)
    return -1;
  free(s);
  return 0;
}
static int write_number(const char *key, int value) {
  char s[32];
  snprintf(s, sizeof s, "%d", value);
  return write_setting(key, s, "");
}
static char *line_value(char *text, const char *key) {
  size_t n = strlen(key);
  for (char *p = text; *p;) {
    if (!strncmp(p, key, n)) {
      p += n;
      while (*p == ' ' || *p == '\t')
        p++;
      return p;
    }
    p = strchr(p, '\n');
    if (!p)
      break;
    p++;
  }
  return NULL;
}
static J *observation(J *hw) {
  if (!hw || !json_object_get_boolean(get(hw, "monitor")) ||
      !json_object_get_boolean(get(hw, "capture"))) {
    fail("Connect MiniFuse input/output routes first");
    return NULL;
  }
  const char *port = str(get(hw, "port"));
  glob_t paths = {0};
  if (glob("/sys/class/sound/card[0-9]*", 0, NULL, &paths)) {
    fail("No sound cards");
    return NULL;
  }
  char card[64] = "";
  int count = 0;
  for (size_t i = 0; i < paths.gl_pathc; i++) {
    char p[PATH_MAX], resolved[PATH_MAX];
    snprintf(p, sizeof p, "%s/device", paths.gl_pathv[i]);
    if (!realpath(p, resolved))
      continue;
    const char *base = strrchr(resolved, '/');
    if (base && !strncmp(base + 1, port, strlen(port)) &&
        base[1 + strlen(port)] == ':') {
      const char *name = strrchr(paths.gl_pathv[i], '/');
      snprintf(card, sizeof card, "%s", name ? name + 1 : paths.gl_pathv[i]);
      count++;
    }
  }
  globfree(&paths);
  if (count != 1) {
    fail("MiniFuse ALSA card unavailable or ambiguous");
    return NULL;
  }
  J *o = json_object_new_object(), *pcm = json_object_new_object();
  put(o, "pcm", pcm);
  char path[PATH_MAX];
  for (int i = 0; i < 2; i++) {
    snprintf(path, sizeof path, "/proc/asound/%s/pcm0%c/sub0/hw_params", card,
             i ? 'c' : 'p');
    char *params = read_file(path, 8192);
    if (!params)
      goto bad;
    snprintf(path, sizeof path, "/proc/asound/%s/pcm0%c/sub0/status", card,
             i ? 'c' : 'p');
    char *status = read_file(path, 8192);
    if (!status) {
      free(params);
      goto bad;
    }
    char *r = line_value(params, "rate:"), *s = line_value(status, "state:");
    J *p = json_object_new_object();
    put(p, "rate", r ? json_object_new_int(atoi(r)) : NULL);
    char st[64] = "CLOSED";
    if (s)
      sscanf(s, "%63s", st);
    put(p, "state", json_object_new_string(st));
    put(pcm, i ? "capture" : "playback", p);
    free(params);
    free(status);
  }
  snprintf(path, sizeof path, "/proc/asound/%s/stream0", card);
  char *stream = read_file(path, 65536);
  if (!stream)
    goto bad;
  bool seen[2][6] = {{0}};
  char *save = NULL;
  int direction = -1;
  for (char *line = strtok_r(stream, "\n", &save); line;
       line = strtok_r(NULL, "\n", &save)) {
    if (!strncmp(line, "Playback:", 9))
      direction = 0;
    else if (!strncmp(line, "Capture:", 8))
      direction = 1;
    while (*line == ' ' || *line == '\t')
      line++;
    if (direction >= 0 && !strncmp(line, "Rates:", 6)) {
      char *p = line + 6;
      while (*p) {
        if (isdigit((unsigned char)*p)) {
          char *end;
          long n = strtol(p, &end, 10);
          for (int j = 0; j < 6; j++)
            if (n == rates[j])
              seen[direction][j] = true;
          p = end;
        } else
          p++;
      }
    }
  }
  free(stream);
  J *allowed = json_object_new_array();
  for (int j = 0; j < 6; j++)
    if (seen[0][j] && seen[1][j])
      json_object_array_add(allowed, json_object_new_int(rates[j]));
  put(o, "rates", allowed);
  snprintf(path, sizeof path, "/sys/bus/usb/devices/%s/serial", port);
  char *serial = read_file(path, 1024);
  if (!serial)
    goto bad;
  serial[strcspn(serial, "\r\n")] = 0;
  put(o, "usb_serial", json_object_new_string(serial));
  free(serial);
  J *meta = metadata();
  if (!meta)
    goto bad;
  put(o, "metadata", meta);
  return o;
bad:
  json_object_put(o);
  return NULL;
}
static bool other_active(J *hw) {
  J *objects = pw_snapshot();
  if (!objects)
    return true;
  bool result = false;
  for (size_t i = 0; i < json_object_array_length(objects); i++) {
    J *o = json_object_array_get_idx(objects, i), *info = get(o, "info"),
      *p = get(info, "props");
    const char *api = str(get(p, "device.api"));
    if (!strcmp(str(get(o, "type")), "PipeWire:Interface:Node") &&
        !strcmp(str(get(info, "state")), "running") &&
        (!strcmp(api, "alsa") || !strcmp(api, "bluez5")) &&
        get(p, "device.id") &&
        json_object_get_int64(get(p, "device.id")) !=
            json_object_get_int64(get(hw, "id")))
      result = true;
  }
  json_object_put(objects);
  return result;
}
static void publish(Audio *a, J *o) {
  if (a->status)
    json_object_put(a->status);
  a->status = json_object_new_object();
  J *s = a->status, *m = get(o, "metadata"), *pcm = get(o, "pcm"),
    *capture = get(pcm, "capture"), *playback = get(pcm, "playback"),
    *q = json_object_new_array();
  put(s, "available", json_object_new_boolean(true));
  put(s, "error", NULL);
  put(s, "rates", copy(get(o, "rates")));
  for (int i = 0; i < 7; i++)
    if (quanta[i] >= setting(m, "clock.min-quantum") &&
        quanta[i] <= setting(m, "clock.max-quantum"))
      json_object_array_add(q, json_object_new_int(quanta[i]));
  put(s, "quanta", q);
  put(s, "requested_rate", json_object_new_int(setting(m, keys[1])));
  put(s, "requested_quantum", json_object_new_int(setting(m, keys[0])));
  put(s, "actual_rate",
      json_object_equal(get(capture, "rate"), get(playback, "rate"))
          ? copy(get(capture, "rate"))
          : NULL);
  put(s, "pcm", copy(pcm));
  put(s, "remembered", copy(a->preferences));
  if (get(o, "observed"))
    put(s, "observed", copy(get(o, "observed")));
}
static void error_status(Audio *a, const char *message) {
  if (a->status)
    json_object_put(a->status);
  a->status = json_object_new_object();
  put(a->status, "available", json_object_new_boolean(false));
  put(a->status, "error", json_object_new_string(message));
}
static bool request_valid(J *r) {
  return fields(r, "rate,quantum,remember") && integer(get(r, "rate")) &&
         integer(get(r, "quantum")) &&
         json_object_is_type(get(r, "remember"), json_type_boolean) &&
         (json_object_get_int(get(r, "rate")) == 0 ||
          member(json_object_get_int(get(r, "rate")), rates, 6)) &&
         (json_object_get_int(get(r, "quantum")) == 0 ||
          member(json_object_get_int(get(r, "quantum")), quanta, 7));
}
void audio_init(Audio *a, const char *path) {
  memset(a, 0, sizeof *a);
  snprintf(a->path, sizeof a->path, "%s", path);
  a->owned = json_object_new_object();
  error_status(a, "Connect MiniFuse to configure audio");
  if (access(path, F_OK))
    return;
  J *p = read_json(path, 4096);
  if (!p)
    return;
  J *r = json_object_new_object();
  put(r, "rate", copy(get(p, "rate")));
  put(r, "quantum", copy(get(p, "quantum")));
  put(r, "remember", json_object_new_boolean(true));
  if (fields(p, "version,rate,quantum,usb_serial") &&
      integer(get(p, "version")) &&
      json_object_get_int(get(p, "version")) == 1 &&
      *str(get(p, "usb_serial")) && request_valid(r))
    a->preferences = p;
  else {
    json_object_put(p);
    put(a->status, "preference_error",
        json_object_new_string("Invalid saved audio preferences"));
  }
  json_object_put(r);
}
int audio_release(Audio *a) {
  if (!a->owned || !json_object_object_length(a->owned))
    return 0;
  J *current = metadata();
  if (!current)
    return -1;
  for (int i = 0; i < 2; i++) {
    J *ownership = get(a->owned, keys[i]);
    if (!ownership)
      continue;
    J *pair = get(current, keys[i]), *before = get(ownership, "before");
    if (!strcmp(str(json_object_array_get_idx(pair, 0)),
                str(get(ownership, "applied")))) {
      if (write_setting(keys[i], str(json_object_array_get_idx(before, 0)),
                        str(json_object_array_get_idx(before, 1)))) {
        json_object_put(current);
        return -1;
      }
      J *checked = metadata();
      if (!checked) {
        json_object_put(current);
        return -1;
      }
      bool ok = json_object_equal(get(checked, keys[i]), before);
      json_object_put(checked);
      if (!ok) {
        json_object_put(current);
        return fail("Cannot release audio settings");
      }
    }
    json_object_object_del(a->owned, keys[i]);
  }
  json_object_put(current);
  return 0;
}
static J *verify(J *hw, int rate, int quantum, const char *serial) {
  double end = monotonic() + 6;
  do {
    J *o = observation(hw);
    if (!o)
      return NULL;
    J *pcm = get(o, "pcm"), *capture = get(pcm, "capture"),
      *playback = get(pcm, "playback"), *meta = get(o, "metadata");
    int actual = json_object_get_int(get(capture, "rate"));
    bool allowed = false;
    for (size_t i = 0; i < json_object_array_length(get(o, "rates")); i++)
      if (json_object_get_int(json_object_array_get_idx(get(o, "rates"), i)) ==
          actual)
        allowed = true;
    if (strcmp(str(get(o, "usb_serial")), serial)) {
      json_object_put(o);
      fail("USB identity changed");
      return NULL;
    }
    if (!strcmp(str(get(capture, "state")), "RUNNING") &&
        !strcmp(str(get(playback, "state")), "RUNNING") &&
        actual == json_object_get_int(get(playback, "rate")) && allowed &&
        (!rate || actual == rate) && setting(meta, keys[0]) == quantum &&
        setting(meta, keys[1]) == rate) {
      if (!quantum)
        return o;
      char *argv[] = {"pw-top", "-b", "-n", "3", NULL};
      char *s = run(argv, 10000);
      if (s) {
        char *save = NULL;
        bool found = false;
        for (char *line = strtok_r(s, "\n", &save); line;
             line = strtok_r(NULL, "\n", &save)) {
          char state;
          int id, q, r;
          char name[512];
          if (sscanf(line, " %c %d %d %d %*s %*s %*s %*s %*s %511[^\n]", &state,
                     &id, &q, &r, name) == 5 &&
              (state == 'R' || state == 't' || state == 'T') && q == quantum &&
              r == actual && !strchr(name, '+') && !strchr(name, '=')) {
            J *objects = pw_snapshot();
            if (objects) {
              for (size_t j = 0; j < json_object_array_length(objects); j++) {
                J *node = json_object_array_get_idx(objects, j),
                  *p = get(get(node, "info"), "props");
                if (json_object_get_int(get(node, "id")) == id &&
                    get(p, "device.id") &&
                    json_object_get_int64(get(p, "device.id")) ==
                        json_object_get_int64(get(hw, "id")) &&
                    !strcmp(str(get(p, "api.alsa.pcm.stream")), "capture") &&
                    json_object_get_int(get(p, "audio.channels")) == 4)
                  found = true;
              }
              json_object_put(objects);
            }
          }
        }
        free(s);
        if (found) {
          J *check = observation(hw);
          if (check && json_object_equal(get(check, "pcm"), pcm) &&
              !strcmp(str(get(check, "usb_serial")), serial) &&
              setting(get(check, "metadata"), keys[0]) == quantum &&
              setting(get(check, "metadata"), keys[1]) == rate) {
            J *observed = json_object_new_object();
            put(observed, "quantum", json_object_new_int(quantum));
            put(observed, "rate", json_object_new_int(actual));
            put(check, "observed", observed);
            json_object_put(o);
            return check;
          }
          if (check)
            json_object_put(check);
        }
      }
    }
    json_object_put(o);
    pause_ms(100);
  } while (monotonic() < end);
  fail("MiniFuse did not enter the requested rate and buffer");
  return NULL;
}
int audio_apply(Audio *a, J *hw, J *r) {
  if (!request_valid(r))
    return fail("Invalid audio settings request");
  J *before = observation(hw);
  if (!before)
    return -1;
  int rate = json_object_get_int(get(r, "rate")),
      quantum = json_object_get_int(get(r, "quantum"));
  J *meta = get(before, "metadata"), *rs = get(before, "rates");
  bool allowed = !rate;
  for (size_t i = 0; i < json_object_array_length(rs); i++)
    if (json_object_get_int(json_object_array_get_idx(rs, i)) == rate)
      allowed = true;
  if (!allowed || (quantum && (quantum < setting(meta, "clock.min-quantum") ||
                               quantum > setting(meta, "clock.max-quantum")))) {
    json_object_put(before);
    return fail("Unsupported device rate or PipeWire buffer");
  }
  if ((rate || quantum) && other_active(hw)) {
    json_object_put(before);
    return fail(
        "Another audio device is active; use automatic rate and buffer");
  }
  const char *serial = str(get(before, "usb_serial"));
  J *current = NULL;
  int rc = write_number(keys[0], quantum);
  if (!rc)
    rc = write_number(keys[1], rate);
  if (!rc) {
    current = verify(hw, rate, quantum, serial);
    if (!current)
      rc = -1;
  }
  J *preferences = NULL;
  if (!rc && json_object_get_boolean(get(r, "remember"))) {
    preferences = json_object_new_object();
    put(preferences, "version", json_object_new_int(1));
    put(preferences, "rate", json_object_new_int(rate));
    put(preferences, "quantum", json_object_new_int(quantum));
    put(preferences, "usb_serial", copy(get(before, "usb_serial")));
    if (save_json(a->path, preferences))
      rc = -1;
  }
  if (rc) {
    char first[1024];
    snprintf(first, sizeof first, "%s", mf_error);
    bool restored = true;
    for (int i = 1; i >= 0; i--) {
      J *pair = get(meta, keys[i]);
      if (write_setting(keys[i], str(json_object_array_get_idx(pair, 0)),
                        str(json_object_array_get_idx(pair, 1))))
        restored = false;
    }
    if (current)
      json_object_put(current);
    current =
        verify(hw, setting(meta, keys[1]), setting(meta, keys[0]), serial);
    if (!current)
      restored = false;
    else
      publish(a, current);
    if (preferences)
      json_object_put(preferences);
    if (current)
      json_object_put(current);
    json_object_put(before);
    if (!restored)
      error_status(a, "Failed to restore previous audio settings");
    return fail("%s; %s", first,
                restored ? "previous settings restored"
                         : "rollback incomplete");
  }
  if (preferences) {
    if (a->preferences)
      json_object_put(a->preferences);
    a->preferences = preferences;
  }
  J *owned = json_object_new_object();
  if (rate || quantum)
    for (int i = 0; i < 2; i++) {
      J *old = get(a->owned, keys[i]), *entry = json_object_new_object(),
        *pair = get(meta, keys[i]);
      bool retain = old && !strcmp(str(get(old, "applied")),
                                   str(json_object_array_get_idx(pair, 0)));
      put(entry, "before", copy(retain ? get(old, "before") : pair));
      char value[32];
      snprintf(value, sizeof value, "%d", i ? rate : quantum);
      put(entry, "applied", json_object_new_string(value));
      put(owned, keys[i], entry);
    }
  json_object_put(a->owned);
  a->owned = owned;
  snprintf(a->identity, sizeof a->identity, "%lld:%lld:%s",
           (long long)json_object_get_int64(get(hw, "id")),
           (long long)json_object_get_int64(get(hw, "serial")), serial);
  publish(a, current);
  json_object_put(current);
  json_object_put(before);
  return 0;
}
int audio_refresh(Audio *a, J *hw) {
  if (!hw) {
    a->identity[0] = 0;
    if (audio_release(a)) {
      error_status(a, mf_error);
      return -1;
    }
    error_status(a, "Connect MiniFuse to configure audio");
    return 0;
  }
  if (json_object_object_length(a->owned) && other_active(hw) &&
      audio_release(a)) {
    error_status(a, mf_error);
    return -1;
  }
  J *o = observation(hw);
  if (!o) {
    error_status(a, mf_error);
    return -1;
  }
  char identity[1024];
  snprintf(identity, sizeof identity, "%lld:%lld:%s",
           (long long)json_object_get_int64(get(hw, "id")),
           (long long)json_object_get_int64(get(hw, "serial")),
           str(get(o, "usb_serial")));
  if (strcmp(a->identity, identity)) {
    snprintf(a->identity, sizeof a->identity, "%s", identity);
    if (a->preferences && !strcmp(str(get(a->preferences, "usb_serial")),
                                  str(get(o, "usb_serial")))) {
      J *r = json_object_new_object();
      put(r, "rate", copy(get(a->preferences, "rate")));
      put(r, "quantum", copy(get(a->preferences, "quantum")));
      put(r, "remember", json_object_new_boolean(false));
      json_object_put(o);
      int rc = audio_apply(a, hw, r);
      json_object_put(r);
      if (rc)
        error_status(a, mf_error);
      return rc;
    }
  }
  publish(a, o);
  json_object_put(o);
  return 0;
}
int audio_reset(Audio *a) {
  if (write_number(keys[0], 0) || write_number(keys[1], 0))
    return -1;
  J *m = metadata();
  if (!m)
    return -1;
  bool ok = !setting(m, keys[0]) && !setting(m, keys[1]);
  json_object_put(m);
  if (!ok)
    return fail("Cannot restore automatic audio settings");
  json_object_put(a->owned);
  a->owned = json_object_new_object();
  if (a->preferences) {
    put(a->preferences, "rate", json_object_new_int(0));
    put(a->preferences, "quantum", json_object_new_int(0));
    if (save_json(a->path, a->preferences))
      return -1;
  }
  put(a->status, "requested_rate", json_object_new_int(0));
  put(a->status, "requested_quantum", json_object_new_int(0));
  return 0;
}
void audio_close(Audio *a) {
  audio_release(a);
  if (a->preferences)
    json_object_put(a->preferences);
  if (a->status)
    json_object_put(a->status);
  if (a->owned)
    json_object_put(a->owned);
  a->preferences = a->status = a->owned = NULL;
}
