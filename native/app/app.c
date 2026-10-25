/* SPDX-License-Identifier: MIT */
#include "mf.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void replace(J **slot, J *v) {
  if (*slot)
    json_object_put(*slot);
  *slot = v;
}
static J *error_json(const char *message) {
  J *o = json_object_new_object();
  put(o, "ok", json_object_new_boolean(false));
  put(o, "error", json_object_new_string(message));
  return o;
}
static int path_copy(char *dest, size_t size, const char *s) {
  if (strlen(s) >= size)
    return fail("Path is too long");
  memcpy(dest, s, strlen(s) + 1);
  return 0;
}
int full_path(char *dest, const char *path) {
  if (strlen(path) >= PATH_MAX)
    return fail("Path too long");
  char temp[PATH_MAX], parent[PATH_MAX];
  snprintf(temp, sizeof temp, "%s", path);
  char *slash = strrchr(temp, '/');
  const char *name = path;
  if (slash) {
    *slash = 0;
    name = strrchr(path, '/') + 1;
    if (!realpath(*temp ? temp : "/", parent))
      return fail("State parent directory is unavailable");
  } else if (!getcwd(parent, sizeof parent))
    return fail("Cannot resolve working directory");
  if (!*name || strchr(name, '/') || !strcmp(name, ".") || !strcmp(name, ".."))
    return fail("Invalid state filename");
  if (snprintf(dest, PATH_MAX, "%s%s%s", parent,
               !strcmp(parent, "/") ? "" : "/", name) >= PATH_MAX)
    return fail("Path too long");
  return 0;
}
static bool usb_port(const char *port) {
  regex_t r;
  if (regcomp(&r, "^[0-9]+-[0-9]+(\\.[0-9]+)*$", REG_EXTENDED))
    return false;
  bool ok = !regexec(&r, port, 0, NULL, 0);
  regfree(&r);
  return ok;
}
static int route_load(App *a, J *route) {
  if (!fields(route, "uid,port"))
    return fail("Invalid session route");
  J *uid = get(route, "uid"), *port = get(route, "port");
  if ((uid && (!json_object_is_type(uid, json_type_string) || !*str(uid) ||
               strlen(str(uid)) > 512 ||
               strlen(str(uid)) != (size_t)json_object_get_string_len(uid))) ||
      (port &&
       (!json_object_is_type(port, json_type_string) || !usb_port(str(port)) ||
        strlen(str(port)) >= sizeof a->route_port)))
    return fail("Invalid device identity or USB port");
  snprintf(a->route_uid, sizeof a->route_uid, "%s", str(uid));
  snprintf(a->route_port, sizeof a->route_port, "%s", str(port));
  return 0;
}
static J *route_view(App *a) {
  J *o = json_object_new_object();
  put(o, "uid", *a->route_uid ? json_object_new_string(a->route_uid) : NULL);
  put(o, "port", *a->route_port ? json_object_new_string(a->route_port) : NULL);
  return o;
}
static void refs_install(App *a, J *refs) {
  for (int m = 0; m < 3; m++)
    for (int i = 0; i < 7; i++)
      if (!a->refs ||
          !json_object_equal(
              json_object_array_get_idx(json_object_array_get_idx(a->refs, m),
                                        i),
              json_object_array_get_idx(json_object_array_get_idx(refs, m), i)))
        a->exceeded[m][i] = false;
  replace(&a->refs, copy(refs));
}
int app_persist(App *a) {
  if (!*a->session_path)
    return 0;
  J *doc = document(a->graph.state, a->preset, a->refs);
  if (!doc)
    return -1;
  char *hash = file_hash(a->state_path);
  if (!hash) {
    json_object_put(doc);
    return -1;
  }
  J *session = json_object_new_object();
  put(session, "version", json_object_new_int(1));
  put(session, "state_path", json_object_new_string(a->state_path));
  put(session, "saved_sha256", *hash ? json_object_new_string(hash) : NULL);
  put(session, "document", doc);
  put(session, "route", route_view(a));
  free(hash);
  int rc = save_json(a->session_path, session);
  json_object_put(session);
  replace(&a->session_error, rc ? json_object_new_string(mf_error) : NULL);
  return rc;
}
static int restore_session(App *a, J **state, J **preset, J **refs) {
  if (!*a->session_path || access(a->session_path, F_OK))
    return 0;
  J *session = read_json(a->session_path, 131072);
  if (!session)
    return -1;
  if (!fields(session, "version,state_path,saved_sha256,document,route") ||
      !integer(get(session, "version")) ||
      json_object_get_int(get(session, "version")) != 1) {
    json_object_put(session);
    return fail("Invalid working session; retained unchanged");
  }
  J *s = NULL, *p = NULL, *r = NULL;
  if (unpack_document(get(session, "document"), &s, &p, &r)) {
    json_object_put(session);
    return -1;
  }
  char *hash = file_hash(a->state_path);
  bool same = hash && !strcmp(str(get(session, "state_path")), a->state_path) &&
              !strcmp(str(get(session, "saved_sha256")), hash);
  free(hash);
  int rc = 0;
  if (same) {
    rc = route_load(a, get(session, "route"));
    if (!rc) {
      replace(state, s);
      replace(preset, p);
      replace(refs, r);
      s = p = r = NULL;
    }
  } else {
    App probe = {0};
    rc = route_load(&probe, get(session, "route"));
  }
  if (s)
    json_object_put(s);
  if (p)
    json_object_put(p);
  if (r)
    json_object_put(r);
  json_object_put(session);
  return rc;
}
static void *meter_reader(void *userdata) {
  App *a = userdata;
  unsigned char buf[65536 + 64];
  size_t pending = 0;
  float peaks[16] = {0};
  for (;;) {
    ssize_t n = read(a->meter_fd, buf + pending, 65536 - pending);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      break;
    size_t total = pending + (size_t)n, usable = total / 64 * 64;
    for (size_t off = 0; off < usable; off += 4) {
      float v;
      memcpy(&v, buf + off, 4);
      if (isfinite(v))
        peaks[(off / 4) % 16] = fmaxf(peaks[(off / 4) % 16], fabsf(v));
    }
    pending = total - usable;
    memmove(buf, buf + usable, pending);
    if (!usable)
      continue;
    /* Control/readback can hold the state lock for seconds. Keep draining
     * pw-cat's audio pipe during that time, otherwise its writer blocks and
     * can stall the graph whose rate/buffer we are trying to verify. Retain
     * interval maxima until publication is possible. */
    int locked = pthread_mutex_trylock(&a->lock);
    if (locked == EBUSY)
      continue;
    if (locked)
      break;
    if (a->stopping) {
      pthread_mutex_unlock(&a->lock);
      break;
    }
    memcpy(a->levels, peaks, sizeof peaks);
    a->updated = monotonic();
    ballistics_update(&a->meters, peaks, a->updated);
    for (int m = 0; m < 3; m++)
      for (int i = 0; i < 7; i++) {
        static const int indices[] = {6, 7, 8, 10, 12, 14};
        int index = i == 6 ? 2 * m : indices[i], db;
        J *ref =
            json_object_array_get_idx(json_object_array_get_idx(a->refs, m), i);
        if (ref && ref_db((float)json_object_get_double(ref), &db) &&
            peaks[index] > 0 && peaks[index] >= pow(10, db / 20.0))
          a->exceeded[m][i] = true;
      }
    pthread_mutex_unlock(&a->lock);
    memset(peaks, 0, sizeof peaks);
  }
  return NULL;
}
static int reconcile(App *a, J *objects) {
  J *devices = hardware_devices(objects);
  if (!devices)
    return -1;
  replace(&a->devices, devices);
  bool present = false;
  if (a->graph.hardware) {
    for (size_t i = 0; i < json_object_array_length(devices); i++) {
      J *d = json_object_array_get_idx(devices, i);
      if (json_object_get_int64(get(d, "id")) ==
              json_object_get_int64(get(a->graph.hardware, "id")) &&
          json_object_get_int64(get(d, "serial")) ==
              json_object_get_int64(get(a->graph.hardware, "serial")))
        present = true;
    }
    if (!present || !graph_links_alive(&a->graph, objects)) {
      if (graph_disconnect(&a->graph))
        return -1;
      replace(&a->hardware_levels, NULL);
      a->hardware_updated = 0;
      ballistics_reset(&a->hardware_meters);
    }
  }
  if ((*a->route_uid || *a->route_port) && !a->graph.hardware) {
    J *selected = NULL;
    int n = 0;
    for (size_t i = 0; i < json_object_array_length(devices); i++) {
      J *d = json_object_array_get_idx(devices, i);
      bool matches = *a->route_uid
                         ? !strcmp(str(get(d, "uid")), a->route_uid)
                         : !strcmp(str(get(d, "port")), a->route_port);
      if (matches) {
        selected = d;
        n++;
      }
    }
    if (n > 1)
      return fail("Selected MiniFuse is ambiguous");
    if (n == 1) {
      if (graph_connect(&a->graph, str(get(selected, "port"))))
        return -1;
      snprintf(a->route_port, sizeof a->route_port, "%s",
               str(get(a->graph.hardware, "port")));
      snprintf(a->route_uid, sizeof a->route_uid, "%s",
               str(get(a->graph.hardware, "uid")));
      if (app_persist(a))
        return -1;
    }
  }
  return 0;
}
static void *poller(void *userdata) {
  App *a = userdata;
  double next_status = 0;
  for (;;) {
    pthread_mutex_lock(&a->lock);
    if (a->stopping) {
      pthread_mutex_unlock(&a->lock);
      break;
    }
    double now = monotonic();
    if (now >= next_status) {
      J *device = *a->ctl_path ? device_status(a->ctl_path) : NULL;
      replace(&a->device,
              device ? device
                     : error_json(*mf_error ? mf_error
                                            : "USB control unavailable"));
      J *objects = pw_snapshot();
      if (objects) {
        int rc = reconcile(a, objects);
        json_object_put(objects);
        replace(&a->route_error, rc ? json_object_new_string(mf_error) : NULL);
      }
      audio_refresh(&a->audio, a->graph.hardware);
      next_status = monotonic() + 1;
    }
    if (a->graph.hardware) {
      J *levels =
          device_meters(a->ctl_path, str(get(a->graph.hardware, "port")));
      if (levels) {
        float peaks[4];
        for (int i = 0; i < 4; i++)
          peaks[i] =
              fmaxf(0, (float)json_object_get_double(json_object_array_get_idx(
                           get(levels, "levels"), i)));
        bool reset = !a->hardware_levels ||
                     strcmp(str(get(levels, "port")),
                            str(get(a->hardware_levels, "port"))) ||
                     now - a->hardware_updated >= .75;
        if (reset)
          ballistics_reset(&a->hardware_meters);
        replace(&a->hardware_levels, levels);
        a->hardware_updated = monotonic();
        ballistics_update(&a->hardware_meters, peaks, a->hardware_updated);
      } else {
        replace(&a->hardware_levels, NULL);
        a->hardware_updated = 0;
      }
    }
    pthread_mutex_unlock(&a->lock);
    pause_ms(100);
  }
  return NULL;
}
int app_init(App *a, const char *state_path, const char *session_path,
             const char *prefix, const char *uid, const char *port,
             const char *ctl, const char *web) {
  memset(a, 0, sizeof *a);
  a->graph.lock_fd = a->graph.log_fd = a->meter_fd = -1;
  if (pthread_mutex_init(&a->lock, NULL))
    return fail("Cannot initialize application lock");
  J *state = NULL, *preset = NULL, *refs = NULL, *prefs = NULL;
  if (full_path(a->state_path, state_path) ||
      (*session_path && full_path(a->session_path, session_path)) ||
      path_copy(a->ctl_path, sizeof a->ctl_path, ctl) ||
      path_copy(a->web_dir, sizeof a->web_dir, web) || strlen(uid) > 512 ||
      strlen(port) >= sizeof a->route_port || (*port && !usb_port(port))) {
    fail("Invalid application paths or device selection");
    goto bad;
  }
  if (snprintf(a->meter_path, sizeof a->meter_path, "%s.meters.json",
               a->state_path) >= (int)sizeof a->meter_path)
    goto bad;
  char audio_path[PATH_MAX];
  if (snprintf(audio_path, sizeof audio_path, "%s.audio.json", a->state_path) >=
      (int)sizeof audio_path)
    goto bad;
  if (*session_path && (!strcmp(a->state_path, a->session_path) ||
                        !strcmp(a->meter_path, a->session_path) ||
                        !strcmp(audio_path, a->session_path))) {
    fail("Working session must use a separate file");
    goto bad;
  }
  snprintf(a->route_uid, sizeof a->route_uid, "%s", uid);
  snprintf(a->route_port, sizeof a->route_port, "%s", port);
  if (!access(a->state_path, F_OK)) {
    J *d = read_json(a->state_path, 65536);
    if (!d)
      goto bad;
    int rc = unpack_document(d, &state, &preset, &refs);
    json_object_put(d);
    if (rc)
      goto bad;
  } else {
    state = factory_state();
    refs = default_refs();
  }
  a->saved = copy(state);
  a->saved_preset = copy(preset);
  a->saved_refs = copy(refs);
  if (restore_session(a, &state, &preset, &refs))
    goto bad;
  a->preset = preset;
  preset = NULL;
  a->refs = refs;
  refs = NULL;
  if (!access(a->meter_path, F_OK)) {
    prefs = read_json(a->meter_path, 4096);
    if (!prefs || validate_preferences(prefs))
      goto bad;
  } else
    prefs = default_preferences();
  ballistics_configure(&a->meters, prefs, 16);
  ballistics_configure(&a->hardware_meters, prefs, 4);
  json_object_put(prefs);
  prefs = NULL;
  audio_init(&a->audio, audio_path);
  if (graph_start(&a->graph, state, prefix))
    goto bad;
  json_object_put(state);
  state = NULL;
  a->devices = json_object_new_array();
  a->device = error_json("Waiting for MiniFuse");
  char target[180];
  snprintf(target, sizeof target, "%s_output", prefix);
  char *argv[] = {"pw-cat",
                  "--record",
                  "--raw",
                  "--target",
                  target,
                  "--channels",
                  "16",
                  "--channel-map",
                  "AUX0,AUX1,AUX2,AUX3,AUX4,AUX5,AUX6,AUX7,AUX8,AUX9,AUX10,"
                  "AUX11,AUX12,AUX13,AUX14,AUX15",
                  "--rate",
                  "48000",
                  "--format",
                  "f32",
                  "-",
                  NULL};
  if (spawn_child(argv, &a->meter_fd, a->graph.log_fd, &a->meter_pid))
    goto bad;
  a->persist_ready = true;
  if (app_persist(a))
    goto bad;
  int e = pthread_create(&a->meter_thread, NULL, meter_reader, a);
  if (e) {
    fail("Cannot start meter reader");
    goto bad;
  }
  a->meter_started = true;
  e = pthread_create(&a->poller, NULL, poller, a);
  if (e) {
    fail("Cannot start device observer");
    goto bad;
  }
  a->poller_started = true;
  return 0;
bad:
  if (state)
    json_object_put(state);
  if (preset)
    json_object_put(preset);
  if (refs)
    json_object_put(refs);
  if (prefs)
    json_object_put(prefs);
  a->persist_ready = false;
  app_close(a);
  return -1;
}
static J *bool_rows(bool values[3][7]) {
  J *a = json_object_new_array();
  for (int m = 0; m < 3; m++) {
    J *r = json_object_new_array();
    for (int i = 0; i < 7; i++)
      json_object_array_add(r, json_object_new_boolean(values[m][i]));
    json_object_array_add(a, r);
  }
  return a;
}
J *app_view(App *a) {
  double now = monotonic();
  J *o = json_object_new_object();
  put(o, "mixer", copy(a->graph.state));
  put(o, "dirty",
      json_object_new_boolean(!json_object_equal(a->graph.state, a->saved) ||
                              !json_object_equal(a->preset, a->saved_preset) ||
                              !json_object_equal(a->refs, a->saved_refs)));
  put(o, "has_saved", json_object_new_boolean(!access(a->state_path, F_OK)));
  J *pc = preset_controls(a->preset);
  if (!pc) {
    json_object_put(o);
    return NULL;
  }
  put(pc, "has_template", json_object_new_boolean(a->preset != NULL));
  J *input_names = json_object_new_array(),
    *mix_names = json_object_new_array();
  static const char *names[] = {
      "SLS_Input_Name_0",        "SLS_Input_Name_1",
      "SLS_USBInput_Name_0",     "SLS_VirtualInput_Name_0",
      "SLS_VirtualInput_Name_1", "SLS_VirtualInput_Name_2"};
  J *params = get(a->preset, "parameters");
  for (int i = 0; i < 6; i++)
    json_object_array_add(input_names,
                          json_object_new_string(str(get(params, names[i]))));
  for (int i = 0; i < 3; i++) {
    char k[64];
    snprintf(k, sizeof k, "SLS_Mix_Name_%d", i);
    json_object_array_add(mix_names,
                          json_object_new_string(str(get(params, k))));
  }
  put(pc, "input_names", input_names);
  put(pc, "mix_names", mix_names);
  put(o, "preset", pc);
  bool active = a->updated > 0 && now - a->updated < .75;
  float zero[16] = {0};
  if (!active)
    ballistics_update(&a->meters, zero, now);
  else
    ballistics_advance(&a->meters, now);
  J *meters = ballistics_view(&a->meters, true);
  put(meters, "active", json_object_new_boolean(active));
  J *levels = active ? json_object_new_array() : NULL;
  if (active)
    for (int i = 0; i < 16; i++)
      json_object_array_add(levels, json_object_new_double(a->levels[i]));
  put(meters, "levels", levels);
  put(meters, "process_alive",
      json_object_new_boolean(child_alive(a->meter_pid)));
  put(meters, "reference_exceeded", bool_rows(a->exceeded));
  put(o, "meters", meters);
  bool hwactive = a->hardware_levels && a->graph.hardware &&
                  now - a->hardware_updated < .75;
  ballistics_advance(&a->hardware_meters, now);
  J *hm = ballistics_view(&a->hardware_meters, hwactive);
  put(hm, "source", json_object_new_string("device-c2"));
  J *channels = json_object_new_array();
  const char *cn[] = {"input1", "input2", "loopback_left", "loopback_right"};
  for (int i = 0; i < 4; i++)
    json_object_array_add(channels, json_object_new_string(cn[i]));
  put(hm, "channels", channels);
  put(hm, "active", json_object_new_boolean(hwactive));
  put(hm, "supported",
      a->hardware_levels ? json_object_new_boolean(true) : NULL);
  put(hm, "status",
      json_object_new_string(hwactive            ? "active"
                             : a->graph.hardware ? "waiting"
                                                 : "waiting"));
  put(hm, "error", NULL);
  put(hm, "port",
      a->hardware_levels ? copy(get(a->hardware_levels, "port")) : NULL);
  put(hm, "age_seconds",
      a->hardware_updated ? json_object_new_double(now - a->hardware_updated)
                          : NULL);
  put(hm, "levels", hwactive ? copy(get(a->hardware_levels, "levels")) : NULL);
  put(hm, "raw_s32",
      hwactive ? copy(get(a->hardware_levels, "raw_s32")) : NULL);
  put(o, "hardware_meters", hm);
  put(o, "device", copy(a->device));
  put(o, "audio", copy(a->audio.status));
  J *ref = json_object_new_object(), *dbs = json_object_new_array(),
    *marks = json_object_new_array();
  put(ref, "normalized", copy(a->refs));
  for (int m = 0; m < 3; m++) {
    J *row = json_object_new_array();
    for (int i = 0; i < 7; i++) {
      int db;
      J *v =
          json_object_array_get_idx(json_object_array_get_idx(a->refs, m), i);
      json_object_array_add(row,
                            v && ref_db((float)json_object_get_double(v), &db)
                                ? json_object_new_int(db)
                                : NULL);
    }
    json_object_array_add(dbs, row);
  }
  const int dbmarks[] = {0, -6, -12, -18, -24, -30, -36, -42, -60};
  for (int i = 0; i < 9; i++)
    json_object_array_add(marks, json_object_new_int(dbmarks[i]));
  put(ref, "db", dbs);
  put(ref, "marks", marks);
  put(ref, "exceeded", bool_rows(a->exceeded));
  put(o, "peakref", ref);
  put(o, "devices", copy(a->devices));
  put(o, "hardware", copy(a->graph.hardware));
  J *session = json_object_new_object();
  put(session, "enabled", json_object_new_boolean(*a->session_path));
  put(session, "error", copy(a->session_error));
  put(o, "session", session);
  put(o, "route_selected",
      json_object_new_boolean(*a->route_uid || *a->route_port));
  put(o, "route_port",
      *a->route_port ? json_object_new_string(a->route_port) : NULL);
  put(o, "route_error", copy(a->route_error));
  put(o, "graph_alive", json_object_new_boolean(child_alive(a->graph.pid)));
  const char *ends[] = {"main",       "virtual34", "virtual56", "virtual78",
                        "loopback78", "mix34",     "mix56",     "inputs12"},
             *labels[] = {"MiniFuse Main 1/2",          "MiniFuse Virtual 3/4",
                          "MiniFuse Virtual 5/6",       "MiniFuse Virtual 7/8",
                          "MiniFuse Loopback Main 7/8", "MiniFuse Mix 3/4",
                          "MiniFuse Mix 5/6",           "MiniFuse Input 1/2"};
  J *play = json_object_new_array(), *rec = json_object_new_array();
  for (int i = 0; i < 8; i++) {
    J *e = json_object_new_object();
    char n[180];
    snprintf(n, sizeof n, "%s_%s", a->graph.prefix, ends[i]);
    put(e, "name", json_object_new_string(n));
    put(e, "label", json_object_new_string(labels[i]));
    json_object_array_add(i < 4 ? play : rec, e);
  }
  put(o, "playback", play);
  put(o, "recording", rec);
  return o;
}
static int replace_mix(App *a, J *state, J *preset, J *refs) {
  J *d = document(state, preset, refs);
  if (!d)
    return -1;
  json_object_put(d);
  if (graph_apply(&a->graph, state))
    return -1;
  replace(&a->preset, copy(preset));
  refs_install(a, refs);
  return 0;
}
J *app_change(App *a, const char *path, J *body) {
  if (a->stopping) {
    fail("Panel is stopping");
    return NULL;
  }
  int rc = 0;
  if (!strcmp(path, "/api/mixer")) {
    J *s = linked_edit(body, a->graph.state, a->preset);
    if (!s)
      return NULL;
    rc = graph_apply(&a->graph, s);
    json_object_put(s);
  } else if (!strcmp(path, "/api/link") || !strcmp(path, "/api/virtual")) {
    bool link = !strcmp(path, "/api/link");
    J *v = get(body, link ? "enabled" : "visible");
    if (!fields(body, link ? "mix,enabled" : "mix,index,visible") ||
        !integer(get(body, "mix")) ||
        !json_object_is_type(v, json_type_boolean) ||
        (!link && !integer(get(body, "index")))) {
      fail("Invalid channel presentation request");
      return NULL;
    }
    J *s = copy(a->graph.state), *p = copy(a->preset);
    rc = link ? change_link(&s, &p, json_object_get_int(get(body, "mix")),
                            json_object_get_boolean(v))
              : change_virtual(&s, &p, json_object_get_int(get(body, "mix")),
                               json_object_get_int(get(body, "index")),
                               json_object_get_boolean(v));
    J *r = !rc ? preset_refs(p) : NULL;
    if (!rc)
      rc = r ? replace_mix(a, s, p, r) : -1;
    if (s)
      json_object_put(s);
    if (p)
      json_object_put(p);
    if (r)
      json_object_put(r);
  } else if (!strcmp(path, "/api/save")) {
    J *d = document(a->graph.state, a->preset, a->refs);
    if (!d)
      return NULL;
    rc = save_json(a->state_path, d);
    json_object_put(d);
    if (!rc) {
      replace(&a->saved, copy(a->graph.state));
      replace(&a->saved_preset, copy(a->preset));
      replace(&a->saved_refs, copy(a->refs));
    }
  } else if (!strcmp(path, "/api/load")) {
    J *d = read_json(a->state_path, 65536), *s = NULL, *p = NULL, *r = NULL;
    if (!d)
      return NULL;
    rc = unpack_document(d, &s, &p, &r);
    json_object_put(d);
    if (!rc)
      rc = replace_mix(a, s, p, r);
    if (!rc) {
      replace(&a->saved, copy(s));
      replace(&a->saved_preset, copy(p));
      replace(&a->saved_refs, copy(r));
    }
    if (s)
      json_object_put(s);
    if (p)
      json_object_put(p);
    if (r)
      json_object_put(r);
  } else if (!strcmp(path, "/api/import-mfp")) {
    if (!fields(body, "text") ||
        !json_object_is_type(get(body, "text"), json_type_string) ||
        strlen(str(get(body, "text"))) !=
            (size_t)json_object_get_string_len(get(body, "text"))) {
      fail("Expected preset text");
      return NULL;
    }
    J *p = parse_preset(str(get(body, "text")));
    if (!p)
      return NULL;
    J *s = import_mixer(p), *r = s ? preset_refs(p) : NULL;
    rc = s && r ? replace_mix(a, s, p, r) : -1;
    if (s)
      json_object_put(s);
    if (r)
      json_object_put(r);
    json_object_put(p);
  } else if (!strcmp(path, "/api/export-mfp")) {
    if (!fields(body, "") || !a->preset) {
      fail("Import a .mfp template before export");
      return NULL;
    }
    J *p = export_mixer(a->graph.state, a->preset);
    if (!p)
      return NULL;
    char *text = preset_text(p);
    json_object_put(p);
    if (!text)
      return NULL;
    J *o = json_object_new_object();
    put(o, "text", json_object_new_string(text));
    free(text);
    return o;
  } else if (!strcmp(path, "/api/rename")) {
    if (!fields(body, "kind,index,name") || !integer(get(body, "index")) ||
        !json_object_is_type(get(body, "name"), json_type_string)) {
      fail("Invalid name request");
      return NULL;
    }
    J *p = copy(a->preset);
    rc = rename_field(p, str(get(body, "kind")),
                      json_object_get_int(get(body, "index")),
                      str(get(body, "name")));
    if (!rc) {
      replace(&a->preset, p);
      p = NULL;
    }
    if (p)
      json_object_put(p);
  } else if (!strcmp(path, "/api/factory")) {
    J *s = factory_state(), *r = default_refs();
    rc = replace_mix(a, s, NULL, r);
    json_object_put(s);
    json_object_put(r);
  } else if (!strcmp(path, "/api/peakref") ||
             !strcmp(path, "/api/reset-peakref")) {
    bool reset = !strcmp(path, "/api/reset-peakref");
    J *mj = get(body, "mix"), *sj = get(body, "source");
    if (!fields(body, reset ? "mix,source" : "mix,source,db") || !integer(mj) ||
        !integer(sj) || json_object_get_int64(mj) < 0 ||
        json_object_get_int64(mj) >= 3 || json_object_get_int64(sj) < 0 ||
        json_object_get_int64(sj) >= 7) {
      fail("Invalid reference index");
      return NULL;
    }
    int m = json_object_get_int(mj), i = json_object_get_int(sj);
    if (!reset) {
      J *db = get(body, "db");
      float v = integer(db) ? ref_normalized(json_object_get_int(db)) : NAN;
      if (!isfinite(v)) {
        fail("Invalid reference mark");
        return NULL;
      }
      J *r = copy(a->refs), *p = copy(a->preset);
      json_object_array_put_idx(json_object_array_get_idx(r, m), i,
                                json_object_new_double(v));
      if (p) {
        rc = change_ref(p, m, i, v);
        if (!rc) {
          json_object_put(r);
          r = preset_refs(p);
          if (!r)
            rc = -1;
        }
      }
      if (!rc) {
        J *d = document(a->graph.state, p, r);
        if (d) {
          json_object_put(d);
          replace(&a->preset, p);
          p = NULL;
          refs_install(a, r);
        } else
          rc = -1;
      }
      if (p)
        json_object_put(p);
      if (r)
        json_object_put(r);
    }
    if (!rc) {
      a->exceeded[m][i] = false;
      J *c = preset_controls(a->preset);
      if (c) {
        if (i < 2 && json_object_get_boolean(
                         json_object_array_get_idx(get(c, "links"), m)))
          a->exceeded[m][1 - i] = false;
        json_object_put(c);
      }
    }
  } else if (!strcmp(path, "/api/reset-meters")) {
    ballistics_reset(&a->meters);
    ballistics_reset(&a->hardware_meters);
    memset(a->exceeded, 0, sizeof a->exceeded);
  } else if (!strcmp(path, "/api/meter-preferences")) {
    rc = validate_preferences(body);
    if (!rc)
      rc = save_json(a->meter_path, body);
    if (!rc) {
      ballistics_configure(&a->meters, body, 16);
      ballistics_configure(&a->hardware_meters, body, 4);
    }
  } else if (!strcmp(path, "/api/audio-settings"))
    rc = audio_apply(&a->audio, a->graph.hardware, body);
  else if (!strcmp(path, "/api/audio-reset")) {
    if (!fields(body, "")) {
      fail("Expected empty reset request");
      return NULL;
    }
    rc = audio_reset(&a->audio);
  } else if (!strcmp(path, "/api/routes")) {
    if (!fields(body, "enabled,port") ||
        !json_object_is_type(get(body, "enabled"), json_type_boolean)) {
      fail("Invalid route request");
      return NULL;
    }
    if (json_object_get_boolean(get(body, "enabled"))) {
      if (!usb_port(str(get(body, "port")))) {
        fail("Invalid USB port");
        return NULL;
      }
      rc = graph_connect(&a->graph, str(get(body, "port")));
      if (!rc) {
        snprintf(a->route_port, sizeof a->route_port, "%s",
                 str(get(a->graph.hardware, "port")));
        snprintf(a->route_uid, sizeof a->route_uid, "%s",
                 str(get(a->graph.hardware, "uid")));
      }
    } else {
      rc = graph_disconnect(&a->graph);
      if (!rc)
        a->route_port[0] = a->route_uid[0] = 0;
    }
    if (!rc)
      replace(&a->route_error, NULL);
  } else if (!strcmp(path, "/api/inst") || !strcmp(path, "/api/mono") ||
             !strcmp(path, "/api/phantom") ||
             !strcmp(path, "/api/hardware-routing")) {
    bool inst = !strcmp(path, "/api/inst"),
         routing = !strcmp(path, "/api/hardware-routing");
    if (!fields(body, inst      ? "channel,enabled"
                      : routing ? "route,enabled"
                                : "enabled") ||
        !json_object_is_type(get(body, "enabled"), json_type_boolean)) {
      fail("Invalid USB control request");
      return NULL;
    }
    int selector = !strcmp(path, "/api/mono") ? 0x500 : 0x400;
    if (inst) {
      J *ch = get(body, "channel");
      if (!integer(ch) ||
          (json_object_get_int64(ch) != 1 && json_object_get_int64(ch) != 2)) {
        fail("Invalid INST channel");
        return NULL;
      }
      selector = json_object_get_int(ch) - 1;
    } else if (routing) {
      const char *r = str(get(body, "route"));
      selector = !strcmp(r, "loopback")  ? 0x1300
                 : !strcmp(r, "usb12")   ? 0x1400
                 : !strcmp(r, "monitor") ? 0x1500
                                         : -1;
      if (selector < 0) {
        fail("Invalid hardware route");
        return NULL;
      }
    }
    J *device = device_status(a->ctl_path);
    if (!device)
      return NULL;
    if (a->graph.hardware && strcmp(str(get(get(device, "identity"), "port")),
                                    str(get(a->graph.hardware, "port")))) {
      json_object_put(device);
      fail("Selected audio and USB devices differ");
      return NULL;
    }
    json_object_put(device);
    rc = device_set(a->ctl_path, selector,
                    json_object_get_boolean(get(body, "enabled")));
    if (!rc) {
      device = device_status(a->ctl_path);
      if (device)
        replace(&a->device, device);
      else
        rc = -1;
    }
  } else {
    fail("Unknown API endpoint");
    return NULL;
  }
  if (rc)
    return NULL;
  if (app_persist(a))
    return NULL;
  return app_view(a);
}
void app_close(App *a) {
  pthread_mutex_lock(&a->lock);
  a->stopping = true;
  if (a->persist_ready)
    app_persist(a);
  pthread_mutex_unlock(&a->lock);
  if (a->poller_started)
    pthread_join(a->poller, NULL);
  stop_child(&a->meter_pid);
  if (a->meter_started)
    pthread_join(a->meter_thread, NULL);
  if (a->meter_fd >= 0)
    close(a->meter_fd);
  audio_close(&a->audio);
  graph_close(&a->graph);
  J **owned[] = {&a->saved,
                 &a->preset,
                 &a->saved_preset,
                 &a->refs,
                 &a->saved_refs,
                 &a->devices,
                 &a->device,
                 &a->route_error,
                 &a->session_error,
                 &a->hardware_levels,
                 &a->meters.preferences,
                 &a->hardware_meters.preferences};
  for (size_t i = 0; i < sizeof owned / sizeof *owned; i++)
    replace(owned[i], NULL);
  pthread_mutex_destroy(&a->lock);
}
