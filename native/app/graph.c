/* SPDX-License-Identifier: MIT */
#include "mf.h"
#include <ctype.h>
#include <fcntl.h>
#include <math.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *play[] = {"main", "virtual34", "virtual56", "virtual78"};
static const char *record[] = {"loopback78", "mix34", "mix56", "inputs12"};
static const char *labels[] = {
    "MiniFuse Main 1/2",          "MiniFuse Virtual 3/4",
    "MiniFuse Virtual 5/6",       "MiniFuse Virtual 7/8",
    "MiniFuse Loopback Main 7/8", "MiniFuse Mix 3/4",
    "MiniFuse Mix 5/6",           "MiniFuse Input 1/2"};
static J *object(void) { return json_object_new_object(); }
static J *array(void) { return json_object_new_array(); }
static void text(J *o, const char *k, const char *v) {
  put(o, k, json_object_new_string(v));
}
static void num(J *o, const char *k, int v) {
  put(o, k, json_object_new_int(v));
}
static void flag(J *o, const char *k, bool v) {
  put(o, k, json_object_new_boolean(v));
}
static J *channels(int start, int count) {
  J *a = array();
  for (int i = 0; i < count; i++) {
    char s[32];
    snprintf(s, sizeof s, "AUX%d", start + i);
    json_object_array_add(a, json_object_new_string(s));
  }
  return a;
}
static J *common(void) {
  J *p = object();
  flag(p, "node.autoconnect", false);
  flag(p, "node.dont-reconnect", true);
  flag(p, "stream.dont-remix", true);
  flag(p, "node.virtual", true);
  num(p, "priority.session", 1);
  num(p, "audio.channels", 2);
  return p;
}
static J *module(const char *name, J *args) {
  J *m = object();
  text(m, "name", name);
  if (args)
    put(m, "args", args);
  return m;
}
static void graph_node(J *nodes, const char *name, const char *label,
                       J *control) {
  J *n = object();
  text(n, "name", name);
  text(n, "type", "builtin");
  text(n, "label", label);
  if (control)
    put(n, "control", control);
  json_object_array_add(nodes, n);
}
static void graph_link(J *links, const char *output, const char *input) {
  J *l = object();
  text(l, "output", output);
  text(l, "input", input);
  json_object_array_add(links, l);
}
J *graph_config(J *state, const char *prefix) {
  float weights[6][10];
  if (matrix(state, weights))
    return NULL;
  J *cfg = object(), *props = object(), *libs = object(), *mods = array();
  put(cfg, "context.properties", props);
  num(props, "log.level", 2);
  put(cfg, "context.spa-libs", libs);
  text(libs, "audio.convert.*", "audioconvert/libspa-audioconvert");
  text(libs, "support.*", "support/libspa-support");
  put(cfg, "context.modules", mods);
  J *rt = module("libpipewire-module-rt", object());
  num(get(rt, "args"), "rt.prio", 83);
  J *flags = array();
  json_object_array_add(flags, json_object_new_string("ifexists"));
  json_object_array_add(flags, json_object_new_string("nofail"));
  put(rt, "flags", flags);
  json_object_array_add(mods, rt);
  const char *names[] = {"libpipewire-module-protocol-native",
                         "libpipewire-module-client-node",
                         "libpipewire-module-adapter"};
  for (int i = 0; i < 3; i++)
    json_object_array_add(mods, module(names[i], NULL));
  J *args = object(), *fg = object(), *nodes = array(), *links = array(),
    *inputs = array(), *outputs = array();
  text(args, "node.description", "MiniFuse SLS matrix (C)");
  text(args, "media.name", "MiniFuse SLS matrix (C)");
  put(args, "filter.graph", fg);
  put(fg, "nodes", nodes);
  put(fg, "links", links);
  put(fg, "inputs", inputs);
  put(fg, "outputs", outputs);
  for (int i = 0; i < 10; i++) {
    char n[32], p[32];
    snprintf(n, sizeof n, "in%d", i);
    graph_node(nodes, n, "copy", NULL);
    snprintf(p, sizeof p, "in%d:In", i);
    json_object_array_add(inputs, json_object_new_string(p));
  }
  for (int r = 0; r < 6; r++) {
    for (int g = 0; g < 2; g++) {
      char name[32];
      snprintf(name, sizeof name, "m%d%c", r, g ? 'b' : 'a');
      J *control = object();
      int start = g ? 8 : 0, count = g ? 2 : 8;
      for (int c = 0; c < count; c++) {
        char key[32], src[64], dst[64];
        snprintf(key, sizeof key, "Gain %d", c + 1);
        put(control, key, json_object_new_double(weights[r][start + c]));
        snprintf(src, sizeof src, "in%d:Out", start + c);
        snprintf(dst, sizeof dst, "%s:In %d", name, c + 1);
        graph_link(links, src, dst);
      }
      graph_node(nodes, name, "mixer", control);
    }
    char name[32], dst[64], src[64];
    snprintf(name, sizeof name, "out%d", r);
    J *control = object();
    num(control, "Gain 1", 1);
    num(control, "Gain 2", 1);
    graph_node(nodes, name, "mixer", control);
    for (int g = 0; g < 2; g++) {
      snprintf(src, sizeof src, "m%d%c:Out", r, g ? 'b' : 'a');
      snprintf(dst, sizeof dst, "out%d:In %d", r, g + 1);
      graph_link(links, src, dst);
    }
    snprintf(src, sizeof src, "out%d:Out", r);
    json_object_array_add(outputs, json_object_new_string(src));
  }
  for (int i = 0; i < 10; i++) {
    char n[32], src[32], dst[32];
    snprintf(n, sizeof n, "tap%d", i);
    graph_node(nodes, n, "copy", NULL);
    snprintf(src, sizeof src, "in%d:Out", i);
    snprintf(dst, sizeof dst, "tap%d:In", i);
    graph_link(links, src, dst);
    snprintf(src, sizeof src, "tap%d:Out", i);
    json_object_array_add(outputs, json_object_new_string(src));
  }
  J *cp = common(), *pp = common();
  char name[160];
  snprintf(name, sizeof name, "%s_input", prefix);
  text(cp, "node.name", name);
  text(cp, "media.class", "Stream/Input/Audio");
  num(cp, "audio.channels", 10);
  put(cp, "audio.position", channels(0, 10));
  flag(cp, "node.hidden", true);
  flag(cp, "pulse.ignore", true);
  snprintf(name, sizeof name, "%s_output", prefix);
  text(pp, "node.name", name);
  text(pp, "media.class", "Stream/Output/Audio");
  num(pp, "audio.channels", 16);
  put(pp, "audio.position", channels(0, 16));
  flag(pp, "node.hidden", true);
  flag(pp, "pulse.ignore", true);
  put(args, "capture.props", cp);
  put(args, "playback.props", pp);
  json_object_array_add(mods, module("libpipewire-module-filter-chain", args));
  for (int direction = 0; direction < 2; direction++)
    for (int i = 0; i < 4; i++) {
      const char *suffix = direction ? record[i] : play[i];
      J *a = object(), *public = common(), *private = common(),
        *stereo = array();
      json_object_array_add(stereo, json_object_new_string("FL"));
      json_object_array_add(stereo, json_object_new_string("FR"));
      put(public, "audio.position", stereo);
      snprintf(name, sizeof name, "%s_%s", prefix, suffix);
      text(public, "node.name", name);
      text(public, "media.class", direction ? "Audio/Source" : "Audio/Sink");
      snprintf(name, sizeof name, "%s_%s_%s", prefix,
               direction ? "receive" : "send", suffix);
      text(private, "node.name", name);
      put(private, "audio.position",
          channels(direction ? (i == 3 ? 0 : 2 * i) : 2 + 2 * i, 2));
      flag(private, "node.hidden", true);
      flag(private, "pulse.ignore", true);
      text(a, "node.description", labels[(direction ? 4 : 0) + i]);
      put(a, direction ? "capture.props" : "playback.props", private);
      put(a, direction ? "playback.props" : "capture.props", public);
      json_object_array_add(mods, module("libpipewire-module-loopback", a));
    }
  return cfg;
}
static void spa(FILE *f, J *v) {
  if (json_object_is_type(v, json_type_object)) {
    fputs("{ ", f);
    json_object_object_foreach(v, k, value) {
      J *key = json_object_new_string(k);
      fputs(json_object_to_json_string_ext(key, JSON_C_TO_STRING_PLAIN), f);
      json_object_put(key);
      fputs(" = ", f);
      spa(f, value);
      fputc(' ', f);
    }
    fputc('}', f);
  } else if (json_object_is_type(v, json_type_array)) {
    fputs("[ ", f);
    for (size_t i = 0; i < json_object_array_length(v); i++) {
      spa(f, json_object_array_get_idx(v, i));
      fputc(' ', f);
    }
    fputc(']', f);
  } else
    fputs(json_object_to_json_string_ext(v, JSON_C_TO_STRING_PLAIN), f);
}
static char *config_text(J *cfg) {
  char *s = NULL;
  size_t n = 0;
  FILE *f = open_memstream(&s, &n);
  if (!f)
    return NULL;
  json_object_object_foreach(cfg, k, v) {
    fprintf(f, "%s = ", k);
    spa(f, v);
    fputc('\n', f);
  }
  fclose(f);
  return s;
}
static J *props(J *o) { return get(get(o, "info"), "props"); }
static bool type(J *o, const char *t) {
  return !strcmp(str(get(o, "type")), t);
}
static J *by_id(J *objects, int64_t id, const char *t) {
  for (size_t i = 0; i < json_object_array_length(objects); i++) {
    J *o = json_object_array_get_idx(objects, i);
    if (json_object_get_int64(get(o, "id")) == id && type(o, t))
      return o;
  }
  return NULL;
}
static int port_id(J *objects, int64_t node, const char *direction,
                   const char *channel) {
  int result = -1, count = 0;
  for (size_t i = 0; i < json_object_array_length(objects); i++) {
    J *o = json_object_array_get_idx(objects, i), *p = props(o);
    if (type(o, "PipeWire:Interface:Port") &&
        json_object_get_int64(get(p, "node.id")) == node &&
        !strcmp(str(get(get(o, "info"), "direction")), direction) &&
        !strcmp(str(get(p, "audio.channel")), channel)) {
      result = json_object_get_int(get(o, "id"));
      count++;
    }
  }
  if (count != 1)
    return fail("Expected one %s %s port on node %lld", direction, channel,
                (long long)node);
  return result;
}
static int link_ports(int output, int input) {
  char a[32], b[32];
  snprintf(a, sizeof a, "%d", output);
  snprintf(b, sizeof b, "%d", input);
  char *argv[] = {"pw-link", a, b, NULL};
  char *s = run(argv, 10000);
  if (!s)
    return -1;
  free(s);
  return 0;
}
static int64_t node(Graph *g, const char *suffix) {
  char n[256];
  snprintf(n, sizeof n, "%s_%s", g->prefix, suffix);
  return json_object_get_int64(get(g->nodes, n));
}
static int verify_core(Graph *g, J *objects) {
  if (!child_alive(g->pid))
    return fail("Mixer process exited");
  char name[180];
  snprintf(name, sizeof name, "%s_input", g->prefix);
  J *o = by_id(objects, node(g, "input"), "PipeWire:Interface:Node");
  if (!o || json_object_get_int64(get(props(o), "object.serial")) !=
                json_object_get_int64(get(g->serials, name)))
    return fail("Mixer identity changed");
  return 0;
}
static int internal(Graph *g) {
  double deadline = monotonic() + 5;
  int pairs[14][2];
  for (;;) {
    J *objects = pw_snapshot();
    if (!objects)
      return -1;
    if (verify_core(g, objects)) {
      json_object_put(objects);
      return -1;
    }
    bool valid = true;
    int index = 0;
    for (int group = 0; group < 2; group++)
      for (int i = 0; i < (group ? 3 : 4); i++)
        for (int ch = 0; ch < 2; ch++) {
          char suffix[80], channel[32];
          snprintf(suffix, sizeof suffix, "%s_%s", group ? "receive" : "send",
                   group ? record[i] : play[i]);
          snprintf(channel, sizeof channel, "AUX%d",
                   group ? 2 * i + ch : 2 + 2 * i + ch);
          pairs[index][0] = port_id(objects, node(g, group ? "output" : suffix),
                                    "output", channel);
          pairs[index][1] = port_id(objects, node(g, group ? suffix : "input"),
                                    "input", channel);
          if (pairs[index][0] < 0 || pairs[index][1] < 0)
            valid = false;
          index++;
        }
    json_object_put(objects);
    if (valid)
      break;
    if (monotonic() > deadline)
      return -1;
    pause_ms(50);
  }
  for (int i = 0; i < 14; i++)
    if (link_ports(pairs[i][0], pairs[i][1]))
      return -1;
  return 0;
}
int graph_start(Graph *g, J *state, const char *prefix) {
  memset(g, 0, sizeof *g);
  g->lock_fd = g->log_fd = -1;
  if (!*prefix || strlen(prefix) > 100)
    return fail("Invalid graph prefix");
  for (const char *p = prefix; *p; p++)
    if (!isalnum((unsigned char)*p) || ((unsigned char)*p >= 128)) {
      if (*p != '_')
        return fail(
            "Graph prefix must contain ASCII letters, digits or underscore");
    }
  snprintf(g->prefix, sizeof g->prefix, "%s", prefix);
  char path[PATH_MAX];
  snprintf(path, sizeof path, "/tmp/minifuze-%u-%s.lock", (unsigned)getuid(),
           prefix);
  g->lock_fd = open(path, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (g->lock_fd < 0 || flock(g->lock_fd, LOCK_EX | LOCK_NB))
    goto bad;
  J *objects = pw_snapshot();
  if (!objects)
    goto bad;
  char start[110];
  snprintf(start, sizeof start, "%s_", prefix);
  for (size_t i = 0; i < json_object_array_length(objects); i++)
    if (!strncmp(
            str(get(props(json_object_array_get_idx(objects, i)), "node.name")),
            start, strlen(start))) {
      json_object_put(objects);
      fail("Graph prefix is already in use");
      goto bad;
    }
  json_object_put(objects);
  snprintf(g->folder, sizeof g->folder, "/tmp/minifuze-c-%u-XXXXXX",
           (unsigned)getuid());
  if (!mkdtemp(g->folder)) {
    fail("Cannot create private graph directory");
    goto bad;
  }
  snprintf(path, sizeof path, "%s/session.conf", g->folder);
  snprintf(g->log_path, sizeof g->log_path, "%s/pipewire.log", g->folder);
  g->log_fd = open(g->log_path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (g->log_fd < 0)
    goto bad;
  J *cfg = graph_config(state, prefix);
  if (!cfg)
    goto bad;
  char *s = config_text(cfg);
  json_object_put(cfg);
  if (!s)
    goto bad;
  int rc = atomic_write(path, s, strlen(s));
  free(s);
  if (rc)
    goto bad;
  char *argv[] = {"pipewire", "-c", path, NULL};
  if (spawn_child(argv, NULL, g->log_fd, &g->pid))
    goto bad;
  g->nodes = object();
  g->serials = object();
  g->links = array();
  g->state = copy(state);
  const char *suffixes[] = {
      "main",           "virtual34",      "virtual56",
      "virtual78",      "loopback78",     "mix34",
      "mix56",          "inputs12",       "input",
      "output",         "send_main",      "send_virtual34",
      "send_virtual56", "send_virtual78", "receive_loopback78",
      "receive_mix34",  "receive_mix56",  "receive_inputs12"};
  for (int attempt = 0; attempt < 100; attempt++) {
    if (!child_alive(g->pid)) {
      fail("PipeWire graph exited; inspect %s", g->log_path);
      goto bad;
    }
    objects = pw_snapshot();
    if (!objects)
      goto bad;
    int found = 0;
    for (size_t n = 0; n < sizeof suffixes / sizeof *suffixes; n++) {
      char name[180];
      snprintf(name, sizeof name, "%s_%s", prefix, suffixes[n]);
      int matches = 0;
      for (size_t i = 0; i < json_object_array_length(objects); i++) {
        J *o = json_object_array_get_idx(objects, i), *p = props(o);
        if (type(o, "PipeWire:Interface:Node") &&
            !strcmp(str(get(p, "node.name")), name)) {
          J *client = by_id(objects, json_object_get_int64(get(p, "client.id")),
                            "PipeWire:Interface:Client"),
            *pid = get(props(client), "pipewire.sec.pid");
          if (!pid)
            pid = get(props(client), "application.process.id");
          if (json_object_get_int64(pid) != g->pid) {
            json_object_put(objects);
            fail("Foreign graph node owner");
            goto bad;
          }
          put(g->nodes, name, copy(get(o, "id")));
          put(g->serials, name, copy(get(p, "object.serial")));
          matches++;
        }
      }
      if (matches == 1)
        found++;
      else if (matches > 1) {
        json_object_put(objects);
        fail("Duplicate graph node");
        goto bad;
      }
    }
    json_object_put(objects);
    if (found == 18) {
      if (internal(g))
        goto bad;
      return 0;
    }
    pause_ms(50);
  }
  fail("Graph endpoints did not appear");
bad:
  graph_close(g);
  return -1;
}
static int controls(Graph *g, float w[6][10]) {
  J *objects = pw_snapshot();
  if (!objects)
    return -1;
  if (verify_core(g, objects)) {
    json_object_put(objects);
    return -1;
  }
  json_object_put(objects);
  char *parameter = NULL;
  size_t parameter_size = 0;
  FILE *parameter_file = open_memstream(&parameter, &parameter_size);
  if (!parameter_file)
    return fail("Cannot serialize PipeWire controls");
  fputs("{ params = [", parameter_file);
  for (int r = 0; r < 6; r++)
    for (int c = 0; c < 10; c++) {
      char name[64];
      snprintf(name, sizeof name, "m%d%c:Gain %d", r, c < 8 ? 'a' : 'b',
               c < 8 ? c + 1 : c - 7);
      fprintf(parameter_file, " \"%s\" %.9g", name, (double)w[r][c]);
    }
  fputs(" ] }", parameter_file);
  if (fclose(parameter_file)) {
    free(parameter);
    return fail("Cannot serialize PipeWire controls");
  }
  char id[32];
  snprintf(id, sizeof id, "%lld", (long long)node(g, "input"));
  char *argv[] = {
      "pw-cli",
      "set-param",
      id,
      "Props",
      parameter,
      NULL};
  char *s = run(argv, 10000);
  free(parameter);
  if (!s)
    return -1;
  free(s);
  char mismatch[160] = "";
  for (int attempt = 0; attempt < 20; attempt++) {
    objects = pw_snapshot();
    if (!objects)
      return -1;
    if (verify_core(g, objects)) {
      json_object_put(objects);
      return -1;
    }
    J *o = by_id(objects, node(g, "input"), "PipeWire:Interface:Node"),
      *list = get(get(get(o, "info"), "params"), "Props"), *values = object();
    for (size_t i = 0; i < json_object_array_length(list); i++) {
      J *pairs = get(json_object_array_get_idx(list, i), "params");
      for (size_t j = 0; j + 1 < json_object_array_length(pairs); j += 2)
        put(values, str(json_object_array_get_idx(pairs, j)),
            copy(json_object_array_get_idx(pairs, j + 1)));
    }
    bool ok = true;
    mismatch[0] = 0;
    for (int r = 0; r < 6; r++)
      for (int c = 0; c < 10; c++) {
        char name[64];
        snprintf(name, sizeof name, "m%d%c:Gain %d", r, c < 8 ? 'a' : 'b',
                 c < 8 ? c + 1 : c - 7);
        J *value = get(values, name);
        if (!value || fabs(json_object_get_double(value) - w[r][c]) > 1e-6) {
          ok = false;
          if (!*mismatch)
            snprintf(mismatch, sizeof mismatch, "%s expected %.9g got %s", name,
                     (double)w[r][c],
                     value ? json_object_to_json_string_ext(
                                 value, JSON_C_TO_STRING_PLAIN)
                           : "<missing>");
        }
      }
    json_object_put(values);
    json_object_put(objects);
    if (ok)
      return 0;
    pause_ms(25);
  }
  return fail("Mixer coefficient readback mismatch: %s", mismatch);
}
int graph_apply(Graph *g, J *state) {
  float candidate[6][10], original[6][10];
  if (matrix(state, candidate) || matrix(g->state, original))
    return -1;
  if (controls(g, candidate)) {
    char first[1024];
    snprintf(first, sizeof first, "%s", mf_error);
    if (controls(g, original))
      return fail("Update failed: %s; rollback failed", first);
    return fail("%s; previous mix restored", first);
  }
  json_object_put(g->state);
  g->state = copy(state);
  return 0;
}
J *hardware_devices(J *objects) {
  J *list = array();
  regex_t rx;
  if (regcomp(&rx, "/([0-9]+-[0-9]+(\\.[0-9]+)*):", REG_EXTENDED)) {
    json_object_put(list);
    return NULL;
  }
  for (size_t i = 0; i < json_object_array_length(objects); i++) {
    J *o = json_object_array_get_idx(objects, i), *p = props(o);
    if (!type(o, "PipeWire:Interface:Device"))
      continue;
    if (strtoul(json_object_get_string(get(p, "device.vendor.id"))
                    ? json_object_get_string(get(p, "device.vendor.id"))
                    : "",
                NULL, 0) != 0x1c75 ||
        strtoul(json_object_get_string(get(p, "device.product.id"))
                    ? json_object_get_string(get(p, "device.product.id"))
                    : "",
                NULL, 0) != 0xaf90)
      continue;
    const char *path = str(get(p, "device.sysfs.path"));
    regmatch_t matches[3];
    if (regexec(&rx, path, 3, matches, 0))
      continue;
    J *d = object();
    put(d, "id", copy(get(o, "id")));
    put(d, "serial", copy(get(p, "object.serial")));
    put(d, "port",
        json_object_new_string_len(path + matches[1].rm_so,
                                   (int)(matches[1].rm_eo - matches[1].rm_so)));
    text(d, "name",
         *str(get(p, "device.description")) ? str(get(p, "device.description"))
                                            : "MiniFuse 2");
    if (*str(get(p, "device.serial")))
      put(d, "uid", copy(get(p, "device.serial")));
    json_object_array_add(list, d);
  }
  regfree(&rx);
  return list;
}
static J *audio_node(J *objects, int64_t device, const char *stream) {
  J *result = NULL;
  int count = 0;
  for (size_t i = 0; i < json_object_array_length(objects); i++) {
    J *o = json_object_array_get_idx(objects, i), *p = props(o);
    if (!type(o, "PipeWire:Interface:Node") || !get(p, "device.id") ||
        json_object_get_int64(get(p, "device.id")) != device ||
        strcmp(str(get(p, "api.alsa.pcm.stream")), stream) ||
        json_object_get_int(get(p, "audio.channels")) != 4)
      continue;
    J *position = NULL;
    if (json_object_get_boolean(get(p, "api.alsa.split.parent")))
      position = channels(0, 4);
    else if (!strcmp(str(get(p, "device.api")), "alsa") &&
             !strncmp(str(get(p, "device.profile.name")),
                      "Direct: Direct:", 15)) {
      J *ports[4] = {0};
      bool valid = true;
      for (size_t j = 0; j < json_object_array_length(objects); j++) {
        J *port = json_object_array_get_idx(objects, j), *pp = props(port);
        if (type(port, "PipeWire:Interface:Port") &&
            json_object_get_int64(get(pp, "node.id")) ==
                json_object_get_int64(get(o, "id")) &&
            !strcmp(str(get(get(port, "info"), "direction")),
                    !strcmp(stream, "playback") ? "input" : "output")) {
          int index = json_object_get_int(get(pp, "port.id"));
          if (index < 0 || index > 3 || ports[index])
            valid = false;
          else
            ports[index] = port;
        }
      }
      position = array();
      char joined[128] = "";
      for (int j = 0; j < 4; j++) {
        if (!ports[j]) {
          valid = false;
          break;
        }
        const char *c = str(get(props(ports[j]), "audio.channel"));
        if (strlen(joined) + strlen(c) + 2 >= sizeof joined) {
          valid = false;
          break;
        }
        strcat(joined, c);
        strcat(joined, ",");
        json_object_array_add(position, json_object_new_string(c));
      }
      if (!valid ||
          (strcmp(joined, "FL,FR,FC,LFE,") && strcmp(joined, "FL,FR,RL,RR,") &&
           strcmp(joined, "AUX0,AUX1,AUX2,AUX3,"))) {
        json_object_put(position);
        position = NULL;
      }
    }
    if (position) {
      if (result)
        json_object_put(result);
      result = object();
      put(result, "id", copy(get(o, "id")));
      put(result, "channels", position);
      count++;
    }
  }
  if (count != 1) {
    if (result)
      json_object_put(result);
    fail("Four-channel ALSA %s node unavailable or ambiguous", stream);
    return NULL;
  }
  return result;
}
int graph_disconnect(Graph *g) {
  if (g->links && json_object_array_length(g->links)) {
    J *objects = pw_snapshot();
    if (!objects)
      return -1;
    for (size_t i = 0; i < json_object_array_length(g->links); i++) {
      J *expected = json_object_array_get_idx(g->links, i),
        *link = by_id(objects, json_object_get_int64(get(expected, "id")),
                      "PipeWire:Interface:Link");
      if (link && json_object_get_int64(get(props(link), "object.serial")) ==
                      json_object_get_int64(get(expected, "serial"))) {
        char id[32];
        snprintf(id, sizeof id, "%lld",
                 (long long)json_object_get_int64(get(expected, "id")));
        char *argv[] = {"pw-link", "-d", id, NULL};
        char *s = run(argv, 10000);
        if (!s) {
          json_object_put(objects);
          return -1;
        }
        free(s);
      }
    }
    json_object_put(objects);
  }
  if (g->links)
    json_object_put(g->links);
  g->links = array();
  if (g->hardware)
    json_object_put(g->hardware);
  g->hardware = NULL;
  return 0;
}
bool graph_links_alive(Graph *g, J *objects) {
  if (!g->hardware || !g->links || json_object_array_length(g->links) != 6)
    return false;
  for (size_t i = 0; i < json_object_array_length(g->links); i++) {
    J *v = json_object_array_get_idx(g->links, i),
      *o = by_id(objects, json_object_get_int64(get(v, "id")),
                 "PipeWire:Interface:Link");
    if (!o || json_object_get_int64(get(props(o), "object.serial")) !=
                  json_object_get_int64(get(v, "serial")))
      return false;
  }
  return true;
}
int graph_connect(Graph *g, const char *port) {
  if (graph_disconnect(g))
    return -1;
  J *objects = pw_snapshot();
  if (!objects)
    return -1;
  if (verify_core(g, objects)) {
    json_object_put(objects);
    return -1;
  }
  J *devices = hardware_devices(objects), *device = NULL, *capture = NULL,
    *playback = NULL;
  int found = 0;
  for (size_t i = 0; i < json_object_array_length(devices); i++) {
    J *d = json_object_array_get_idx(devices, i);
    if (!strcmp(str(get(d, "port")), port)) {
      device = d;
      found++;
    }
  }
  if (found != 1) {
    fail("MiniFuse not found or ambiguous at %s", port);
    goto bad;
  }
  capture =
      audio_node(objects, json_object_get_int64(get(device, "id")), "capture");
  playback =
      audio_node(objects, json_object_get_int64(get(device, "id")), "playback");
  if (!capture || !playback)
    goto bad;
  int pairs[6][2], n = 0;
  for (int ch = 0; ch < 2; ch++) {
    const char *source =
                   str(json_object_array_get_idx(get(capture, "channels"), ch)),
               *dest = str(
                   json_object_array_get_idx(get(playback, "channels"), ch));
    char c[32];
    snprintf(c, sizeof c, "AUX%d", ch);
    int src = port_id(objects, json_object_get_int64(get(capture, "id")),
                      "output", source);
    pairs[n][0] = src;
    pairs[n++][1] = port_id(objects, node(g, "input"), "input", c);
    pairs[n][0] = src;
    pairs[n++][1] = port_id(objects, node(g, "receive_inputs12"), "input", c);
    pairs[n][0] = port_id(objects, node(g, "output"), "output", c);
    pairs[n++][1] = port_id(objects, json_object_get_int64(get(playback, "id")),
                            "input", dest);
  }
  for (int i = 0; i < 6; i++)
    if (pairs[i][0] < 0 || pairs[i][1] < 0)
      goto bad;
  for (int i = 0; i < 6; i++) {
    if (link_ports(pairs[i][0], pairs[i][1]))
      goto bad;
    J *after = pw_snapshot();
    if (!after)
      goto bad;
    int count = 0;
    for (size_t j = 0; j < json_object_array_length(after); j++) {
      J *o = json_object_array_get_idx(after, j), *info = get(o, "info");
      if (type(o, "PipeWire:Interface:Link") &&
          json_object_get_int(get(info, "output-port-id")) == pairs[i][0] &&
          json_object_get_int(get(info, "input-port-id")) == pairs[i][1]) {
        J *l = object();
        put(l, "id", copy(get(o, "id")));
        put(l, "serial", copy(get(props(o), "object.serial")));
        json_object_array_add(g->links, l);
        count++;
      }
    }
    json_object_put(after);
    if (count != 1) {
      fail("New hardware link is ambiguous");
      goto bad;
    }
  }
  g->hardware = copy(device);
  flag(g->hardware, "monitor", true);
  flag(g->hardware, "capture", true);
  json_object_put(capture);
  json_object_put(playback);
  json_object_put(devices);
  json_object_put(objects);
  return 0;
bad:
  if (capture)
    json_object_put(capture);
  if (playback)
    json_object_put(playback);
  if (devices)
    json_object_put(devices);
  json_object_put(objects);
  {
    char error[1024];
    snprintf(error, sizeof error, "%s", mf_error);
    graph_disconnect(g);
    snprintf(mf_error, sizeof mf_error, "%s", error);
  }
  return -1;
}
void graph_close(Graph *g) {
  stop_child(&g->pid);
  if (g->state)
    json_object_put(g->state);
  if (g->hardware)
    json_object_put(g->hardware);
  if (g->nodes)
    json_object_put(g->nodes);
  if (g->serials)
    json_object_put(g->serials);
  if (g->links)
    json_object_put(g->links);
  g->state = g->hardware = g->nodes = g->serials = g->links = NULL;
  if (g->log_fd >= 0) {
    close(g->log_fd);
    g->log_fd = -1;
  }
  if (*g->folder) {
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/session.conf", g->folder);
    unlink(p);
    unlink(g->log_path);
    rmdir(g->folder);
    g->folder[0] = 0;
  }
  if (g->lock_fd >= 0) {
    close(g->lock_fd);
    g->lock_fd = -1;
  }
}
