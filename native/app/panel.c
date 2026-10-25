/* SPDX-License-Identifier: MIT */
#include "mf.h"
#include "web_assets.h"
#include <arpa/inet.h>
#include <microhttpd.h>
#include <openssl/crypto.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/random.h>
#include <unistd.h>

static volatile sig_atomic_t stopped;
static void interrupt(int sig) {
  (void)sig;
  stopped = 1;
}
typedef struct {
  char *body;
  size_t length;
  bool oversized;
} Request;
static enum MHD_Result respond(struct MHD_Connection *c, unsigned status,
                               const char *body, size_t n, const char *type) {
  struct MHD_Response *r =
      MHD_create_response_from_buffer(n, (void *)body, MHD_RESPMEM_MUST_COPY);
  if (!r)
    return MHD_NO;
  MHD_add_response_header(r, "Content-Type", type);
  MHD_add_response_header(r, "Cache-Control", "no-store");
  MHD_add_response_header(r, "X-Content-Type-Options", "nosniff");
  MHD_add_response_header(
      r, "Content-Security-Policy",
      "default-src 'self'; frame-ancestors 'none'; base-uri 'none'");
  enum MHD_Result rc = MHD_queue_response(c, status, r);
  MHD_destroy_response(r);
  return rc;
}
static enum MHD_Result json_response(struct MHD_Connection *c, unsigned status,
                                     J *o) {
  const char *s = json_object_to_json_string_ext(o, JSON_C_TO_STRING_PLAIN);
  enum MHD_Result rc =
      respond(c, status, s, strlen(s), "application/json; charset=utf-8");
  json_object_put(o);
  return rc;
}
static enum MHD_Result error_response(struct MHD_Connection *c, unsigned status,
                                      const char *message) {
  J *o = json_object_new_object();
  put(o, "error", json_object_new_string(message));
  return json_response(c, status, o);
}
static void completed(void *cls, struct MHD_Connection *c, void **con_cls,
                      enum MHD_RequestTerminationCode code) {
  (void)cls;
  (void)c;
  (void)code;
  Request *r = *con_cls;
  if (r) {
    free(r->body);
    free(r);
  }
  *con_cls = NULL;
}
static enum MHD_Result handle(void *userdata, struct MHD_Connection *c,
                              const char *url, const char *method,
                              const char *version, const char *upload,
                              size_t *upload_size, void **con_cls) {
  (void)version;
  App *a = userdata;
  Request *r = *con_cls;
  if (!r) {
    r = calloc(1, sizeof *r);
    if (!r)
      return MHD_NO;
    r->body = malloc(65537);
    if (!r->body) {
      free(r);
      return MHD_NO;
    }
    r->body[0] = 0;
    *con_cls = r;
    return MHD_YES;
  }
  if (*upload_size) {
    if (*upload_size > 65536 - r->length)
      r->oversized = true;
    else if (!r->oversized) {
      memcpy(r->body + r->length, upload, *upload_size);
      r->length += *upload_size;
      r->body[r->length] = 0;
    }
    *upload_size = 0;
    return MHD_YES;
  }
  char host[80], origin[100];
  snprintf(host, sizeof host, "127.0.0.1:%d", a->http_port);
  snprintf(origin, sizeof origin, "http://%s", host);
  const char *actual = MHD_lookup_connection_value(c, MHD_HEADER_KIND, "Host");
  if (!actual || strcmp(actual, host))
    return error_response(c, 421, "Invalid host");
  if (!strcmp(method, "GET")) {
    if (!strcmp(url, "/api/state")) {
      pthread_mutex_lock(&a->lock);
      J *view = app_view(a);
      pthread_mutex_unlock(&a->lock);
      if (!view)
        return error_response(c, 500, mf_error);
      return json_response(c, 200, view);
    }
    const char *file = NULL, *type = NULL;
    if (!strcmp(url, "/")) {
      file = "index.html";
      type = "text/html; charset=utf-8";
    } else if (!strcmp(url, "/app.js")) {
      file = "app.js";
      type = "text/javascript; charset=utf-8";
    } else if (!strcmp(url, "/style.css")) {
      file = "style.css";
      type = "text/css; charset=utf-8";
    } else
      return error_response(c, 404, "Not found");
    char path[PATH_MAX];
    if (snprintf(path, sizeof path, "%s/%s", a->web_dir, file) >=
        (int)sizeof path)
      return error_response(c, 500, "Asset path too long");
    char *s = NULL;
    if (*a->web_dir)
      s = read_file(path, 262144);
    else {
      const unsigned char *bytes = !strcmp(url, "/")         ? web_index_html
                                   : !strcmp(url, "/app.js") ? web_app_js
                                                             : web_style_css;
      size_t size = !strcmp(url, "/")         ? sizeof web_index_html
                    : !strcmp(url, "/app.js") ? sizeof web_app_js
                                              : sizeof web_style_css;
      s = malloc(size);
      if (s)
        memcpy(s, bytes, size);
    }
    if (!s)
      return error_response(c, 500, mf_error);
    char *result = s;
    if (!strcmp(url, "/")) {
      char *marker = strstr(s, "__CSRF_TOKEN__");
      if (!marker) {
        free(s);
        return error_response(c, 500, "Missing panel request token marker");
      }
      size_t before = (size_t)(marker - s), after = strlen(marker + 14);
      result = malloc(before + 64 + after + 1);
      if (!result) {
        free(s);
        return MHD_NO;
      }
      memcpy(result, s, before);
      memcpy(result + before, a->csrf, 64);
      memcpy(result + before + 64, marker + 14, after + 1);
      free(s);
    }
    enum MHD_Result rc = respond(c, 200, result, strlen(result), type);
    free(result);
    return rc;
  }
  if (strcmp(method, "POST"))
    return error_response(c, 405, "Method not allowed");
  const char *client_origin =
                 MHD_lookup_connection_value(c, MHD_HEADER_KIND, "Origin"),
             *token = MHD_lookup_connection_value(c, MHD_HEADER_KIND,
                                                  "X-CSRF-Token"),
             *type = MHD_lookup_connection_value(c, MHD_HEADER_KIND,
                                                 "Content-Type");
  if ((client_origin && strcmp(client_origin, origin)) || !token ||
      strlen(token) != 64 || CRYPTO_memcmp(token, a->csrf, 64))
    return error_response(c, 403, "Origin or request token rejected");
  if (r->oversized || !type || strncasecmp(type, "application/json", 16) ||
      (type[16] && type[16] != ';'))
    return error_response(c, 400, "Invalid JSON request size or type");
  J *body = parse_json(r->body, r->length);
  if (!body || !json_object_is_type(body, json_type_object)) {
    if (body)
      json_object_put(body);
    return error_response(c, 400, "Expected a JSON object");
  }
  pthread_mutex_lock(&a->lock);
  J *result = app_change(a, url, body);
  pthread_mutex_unlock(&a->lock);
  json_object_put(body);
  if (!result)
    return error_response(
        c, !strcmp(mf_error, "Unknown API endpoint") ? 404 : 400, mf_error);
  return json_response(c, 200, result);
}
int panel_serve(App *a, int port, const char *ready_path) {
  char ready[PATH_MAX] = "";
  if (*ready_path) {
    if (full_path(ready, ready_path))
      return -1;
    char audio[PATH_MAX];
    if (snprintf(audio, sizeof audio, "%s.audio.json", a->state_path) >=
        (int)sizeof audio)
      return fail("Path too long");
    if (!strcmp(ready, a->state_path) || !strcmp(ready, a->session_path) ||
        !strcmp(ready, a->meter_path) || !strcmp(ready, audio))
      return fail("Ready file must be separate from state files");
  }
  unsigned char random[32];
  size_t offset = 0;
  while (offset < sizeof random) {
    ssize_t n = getrandom(random + offset, sizeof random - offset, 0);
    if (n <= 0)
      return fail("Cannot generate panel request token");
    offset += (size_t)n;
  }
  for (int i = 0; i < 32; i++)
    snprintf(a->csrf + 2 * i, 3, "%02x", random[i]);
  struct sockaddr_in address = {.sin_family = AF_INET,
                                .sin_port = htons((uint16_t)port),
                                .sin_addr = {.s_addr = htonl(INADDR_LOOPBACK)}};
  struct MHD_Daemon *server = MHD_start_daemon(
      MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_THREAD_PER_CONNECTION,
      (uint16_t)port, NULL, NULL, handle, a, MHD_OPTION_SOCK_ADDR, &address,
      MHD_OPTION_NOTIFY_COMPLETED, completed, a, MHD_OPTION_CONNECTION_LIMIT,
      (unsigned int)16, MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int)30,
      MHD_OPTION_END);
  if (!server)
    return fail("Cannot start local HTTP server");
  const union MHD_DaemonInfo *info =
      MHD_get_daemon_info(server, MHD_DAEMON_INFO_BIND_PORT);
  a->http_port = info ? info->port : port;
  if (!a->http_port) {
    MHD_stop_daemon(server);
    return fail("Cannot determine panel HTTP port");
  }
  J *r = json_object_new_object();
  char url[100];
  snprintf(url, sizeof url, "http://127.0.0.1:%d", a->http_port);
  put(r, "url", json_object_new_string(url));
  put(r, "pid", json_object_new_int64(getpid()));
  put(r, "prefix", json_object_new_string(a->graph.prefix));
  put(r, "state_path", json_object_new_string(a->state_path));
  put(r, "hardware_port", NULL);
  put(r, "monitor_route", json_object_new_string("raw"));
  put(r, "bridge_backend", json_object_new_string("streams"));
  put(r, "implementation", json_object_new_string("c"));
  if (*ready && save_json(ready, r)) {
    json_object_put(r);
    MHD_stop_daemon(server);
    return -1;
  }
  puts(json_object_to_json_string_ext(r, JSON_C_TO_STRING_PLAIN));
  fflush(stdout);
  json_object_put(r);
  struct sigaction action = {.sa_handler = interrupt}, old_int, old_term;
  sigemptyset(&action.sa_mask);
  stopped = 0;
  sigaction(SIGINT, &action, &old_int);
  sigaction(SIGTERM, &action, &old_term);
  int rc = 0;
  while (!stopped) {
    if (!child_alive(a->graph.pid)) {
      rc = fail("Audio graph exited; restart the panel");
      break;
    }
    pause_ms(100);
  }
  MHD_stop_daemon(server);
  sigaction(SIGINT, &old_int, NULL);
  sigaction(SIGTERM, &old_term, NULL);
  return rc;
}
