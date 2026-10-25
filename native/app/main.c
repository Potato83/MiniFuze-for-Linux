/* SPDX-License-Identifier: MIT */
#include "mf.h"
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>

static int emit(J *value) {
  if (!value)
    return -1;
  puts(json_object_to_json_string_ext(value, JSON_C_TO_STRING_PRETTY));
  json_object_put(value);
  return 0;
}
static int usage(void) {
  puts("MiniFuse 2 for Linux engine\nUsage: minifuze2-engine panel [--state FILE "
       "--session-state FILE --prefix NAME --device-uid UID --port USB --ctl "
       "DEVICE --http-port PORT --ready-file FILE --web-dir DIR]\n       "
       "minifuze2-engine status|meters [--ctl DEVICE]\n       minifuze2-engine switch "
       "SELECTOR on|off [--ctl DEVICE]\n       minifuze2-engine factory|matrix "
       "FILE|preset-import FILE|preset-export STATE TEMPLATE\n       "
       "minifuze2-engine meter-position AMPLITUDE\nNo firmware update or MIDI "
       "transmission is implemented.");
  return 0;
}
int main_cli(int argc, char **argv) {
  if (argc < 2 || !strcmp(argv[1], "--help"))
    return usage();
  const char *command = argv[1];
  if (!strcmp(command, "--version")) {
    puts("0.1.0-preview");
    return 0;
  }
  if (!strcmp(command, "factory"))
    return emit(factory_state());
  if (!strcmp(command, "matrix") && argc == 3) {
    J *d = read_json(argv[2], 65536), *s = NULL, *p = NULL, *r = NULL;
    if (!d)
      return -1;
    if (unpack_document(d, &s, &p, &r)) {
      json_object_put(d);
      return -1;
    }
    float w[6][10];
    int rc = matrix(s, w);
    J *a = json_object_new_array();
    if (!rc)
      for (int i = 0; i < 6; i++) {
        J *row = json_object_new_array();
        for (int j = 0; j < 10; j++)
          json_object_array_add(row, json_object_new_double(w[i][j]));
        json_object_array_add(a, row);
      }
    json_object_put(d);
    json_object_put(s);
    if (p)
      json_object_put(p);
    json_object_put(r);
    if (rc) {
      json_object_put(a);
      return -1;
    }
    return emit(a);
  }
  if (!strcmp(command, "preset-import") && argc == 3) {
    char *text = read_file(argv[2], 32768);
    if (!text)
      return -1;
    J *p = parse_preset(text);
    free(text);
    if (!p)
      return -1;
    J *s = import_mixer(p);
    if (!s) {
      json_object_put(p);
      return -1;
    }
    J *r = preset_refs(p), *d = r ? document(s, p, r) : NULL;
    json_object_put(p);
    json_object_put(s);
    if (r)
      json_object_put(r);
    return emit(d);
  }
  if (!strcmp(command, "preset-export") && argc == 4) {
    J *d = read_json(argv[2], 65536), *s = NULL, *p = NULL, *r = NULL;
    if (!d)
      return -1;
    int rc = unpack_document(d, &s, &p, &r);
    json_object_put(d);
    if (rc)
      return -1;
    char *text = read_file(argv[3], 32768);
    J *template = text ? parse_preset(text) : NULL;
    free(text);
    J *updated = template ? export_mixer(s, template) : NULL;
    char *output = updated ? preset_text(updated) : NULL;
    bool exported = output != NULL;
    if (output)
      fwrite(output, 1, strlen(output), stdout);
    free(output);
    json_object_put(s);
    if (p)
      json_object_put(p);
    json_object_put(r);
    if (template)
      json_object_put(template);
    if (updated)
      json_object_put(updated);
    return exported ? 0 : -1;
  }
  if (!strcmp(command, "meter-position") && argc == 3) {
    char *end;
    float v = strtof(argv[2], &end);
    if (*end || !isfinite(v) || v < 0)
      return fail("Invalid amplitude");
    printf("%.9g\n", (double)meter_position(v));
    return 0;
  }
  if (!strcmp(command, "status") || !strcmp(command, "meters") ||
      !strcmp(command, "switch")) {
    const char *ctl = "/dev/minifuze2ctl";
    bool write = !strcmp(command, "switch");
    int base = write ? 4 : 2;
    if (write && argc < 4)
      return fail("Expected switch SELECTOR on|off");
    if (argc == base + 2 && !strcmp(argv[base], "--ctl"))
      ctl = argv[base + 1];
    else if (argc != base)
      return fail("Unexpected control arguments");
    if (write) {
      char *end;
      long selector = strtol(argv[2], &end, 0);
      if (*end || selector < 0 || selector > 65535 ||
          (strcmp(argv[3], "on") && strcmp(argv[3], "off")))
        return fail("Invalid switch request");
      if (device_set(ctl, (int)selector, !strcmp(argv[3], "on")))
        return -1;
    }
    return emit(!strcmp(command, "meters") ? device_meters(ctl, NULL)
                                           : device_status(ctl));
  }
#ifndef MF_CORE_ONLY
  if (!strcmp(command, "panel")) {
    const char *state = "minifuze-state.json", *session = "",
               *prefix = "minifuze_c", *uid = "", *port = "",
               *ctl = "/dev/minifuze2ctl", *ready = "", *web = "";
    int http = 0;
    for (int i = 2; i < argc; i += 2) {
      if (i + 1 >= argc)
        return fail("Missing option value");
      const char *v = argv[i + 1];
      if (!strcmp(argv[i], "--state"))
        state = v;
      else if (!strcmp(argv[i], "--session-state"))
        session = v;
      else if (!strcmp(argv[i], "--prefix"))
        prefix = v;
      else if (!strcmp(argv[i], "--device-uid"))
        uid = v;
      else if (!strcmp(argv[i], "--port"))
        port = v;
      else if (!strcmp(argv[i], "--ctl"))
        ctl = v;
      else if (!strcmp(argv[i], "--ready-file"))
        ready = v;
      else if (!strcmp(argv[i], "--web-dir"))
        web = v;
      else if (!strcmp(argv[i], "--http-port")) {
        char *end;
        long n = strtol(v, &end, 10);
        if (*end || n < 0 || n > 65535)
          return fail("Invalid HTTP port");
        http = (int)n;
      } else
        return fail("Unknown option: %s", argv[i]);
    }
    App *app = calloc(1, sizeof *app);
    if (!app)
      return fail("Out of memory");
    int rc = app_init(app, state, session, prefix, uid, port, ctl, web);
    if (!rc) {
      rc = panel_serve(app, http, ready);
      app_close(app);
    }
    free(app);
    return rc;
  }
#endif
  return fail("Unknown command or invalid arguments; use --help");
}
int main(int argc, char **argv) {
  if (argc >= 4 && !strcmp(argv[1], "--child-exec")) {
    char *end = NULL;
    long parent = strtol(argv[2], &end, 10);
    if (!end || *end || parent <= 1 || prctl(PR_SET_PDEATHSIG, SIGTERM) ||
        getppid() != parent)
      return 125;
    execvp(argv[3], argv + 3);
    return 127;
  }
  int rc = main_cli(argc, argv);
  if (rc < 0) {
    fprintf(stderr, "MiniFuse: %s\n",
            *mf_error ? mf_error : "Operation failed");
    return 1;
  }
  return rc;
}
