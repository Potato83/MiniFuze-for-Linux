/* SPDX-License-Identifier: MIT */
#include "desktop_assets.h"
#include <gtk/gtk.h>
#include <json-c/json.h>
#include <libsoup/soup.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct json_object J;
typedef struct {
  char *path;
  J *body;
  guint revision;
  GFile *export_file;
} Job;
typedef struct {
  GtkApplication *app;
  GtkWidget *window, *root, *workspace, *status, *message, *dirty, *mix_name,
      *reconnect;
  GtkWidget *tabs[4], *pages, *link, *device, *route, *firmware;
  GtkWidget *hardware[7], *save, *load, *export_button;
  GtkWidget *strips[7], *names[7], *gain[7], *gain_text[7], *mute[6];
  GtkWidget *pan[6][2], *refs[7], *ref_reset[7], *meters[7], *peaks[7],
      *visible[3];
  GtkWidget *input_meters[5], *input_levels[5];
  GtkWidget *prefs, *rate, *quantum, *remember, *audio_info;
  GtkWidget *audio_apply, *audio_reset, *meters_apply;
  GtkWidget *theme;
  GtkCssProvider *css;
  GtkWidget *orange, *clip, *peak;
  SoupSession *http;
  GQueue jobs;
  J *state, *draft;
  char *url, *ready_file, *device_key;
  char token[65];
  guint timer, debounce, revision, sent_revision;
  int mix, page;
  gboolean rendering, busy, connected, closing, start_attempted, replacing;
  gboolean dark;
  gboolean ref_pending[3][7];
  int ref_values[3][7];
} Ui;
typedef struct {
  Ui *ui;
  SoupMessage *message;
  Job *job;
  int kind;
} Request;
static const int marks[] = {0, -6, -12, -18, -24, -30, -36, -42, -60};
static const int rates[] = {0, 44100, 48000, 88200, 96000, 176400, 192000};
static const int quanta[] = {0, 32, 64, 128, 256, 512, 1024, 2048};
static const char *strip_names[] = {"Input 1",     "Input 2",     "Main 1/2",
                                    "Virtual 3/4", "Virtual 5/6", "Virtual 7/8",
                                    "Общий выход"};
static const char *switches[] = {"inst1",
                                 "inst2",
                                 "direct_mono",
                                 "phantom48v",
                                 "loopback",
                                 "loopback_to_usb12",
                                 "loopback_to_monitor"};
static const char *clip_values[] = {"infinite", "1.5s", "3s"};
static const char *peak_values[] = {"none", "infinite", "1s", "2s",
                                    "3s",   "4s",       "5s"};
static void render(Ui *u);
static void dispatch(Ui *u);
static void flush_mixer(Ui *u);
static void settings(GtkButton *button, gpointer data);
static void job_free(Job *j) {
  if (!j)
    return;
  g_free(j->path);
  if (j->body)
    json_object_put(j->body);
  g_clear_object(&j->export_file);
  g_free(j);
}
static J *get(J *o, const char *key) {
  J *v = NULL;
  if (o)
    json_object_object_get_ex(o, key, &v);
  return v;
}
static J *idx(J *a, int i) {
  return a && i >= 0 ? json_object_array_get_idx(a, (size_t)i) : NULL;
}
static size_t jlen(J *o) {
  return o && json_object_is_type(o, json_type_array)
             ? json_object_array_length(o)
             : 0;
}
static const char *str(J *o) {
  return o && json_object_is_type(o, json_type_string)
             ? json_object_get_string(o)
             : "";
}
static J *jcopy(J *o) {
  J *v = NULL;
  if (o)
    json_object_deep_copy(o, &v, NULL);
  return v;
}
static void put(J *o, const char *key, J *v) {
  json_object_object_add(o, key, v);
}
static void number(J *o, const char *key, double n) {
  put(o, key, json_object_new_double(n));
}
static void integer(J *o, const char *key, int n) {
  put(o, key, json_object_new_int(n));
}
static void boolean(J *o, const char *key, gboolean b) {
  put(o, key, json_object_new_boolean(b));
}
static void text(J *o, const char *key, const char *s) {
  put(o, key, json_object_new_string(s));
}
static J *body(void) { return json_object_new_object(); }
static double db(double v) { return v > 0 ? 20 * log10(v) : -120; }
static double val(J *o) { return json_object_get_double(o); }
static gboolean yes(J *o) { return json_object_get_boolean(o); }
static int widget_id(gpointer w, const char *key) {
  return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), key));
}
static void tag(GtkWidget *w, const char *key, int n) {
  g_object_set_data(G_OBJECT(w), key, GINT_TO_POINTER(n));
}
static GtkWidget *box(GtkOrientation orientation, int gap) {
  return gtk_box_new(orientation, gap);
}
static void add(GtkWidget *b, GtkWidget *w) { gtk_box_append(GTK_BOX(b), w); }
static GtkWidget *label(const char *s, const char *css) {
  GtkWidget *w = gtk_label_new(s);
  if (css)
    gtk_widget_add_css_class(w, css);
  return w;
}
static void notice(Ui *u, const char *s, gboolean error) {
  if (!u->window)
    return;
  gtk_label_set_text(GTK_LABEL(u->message), s);
  if (error)
    gtk_widget_add_css_class(u->message, "error");
  else
    gtk_widget_remove_css_class(u->message, "error");
}
static GtkWidget *button(const char *s, GCallback callback, Ui *u) {
  GtkWidget *w = gtk_button_new_with_label(s);
  gtk_widget_add_css_class(w, "control");
  g_signal_connect(w, "clicked", callback, u);
  return w;
}
static GtkWidget *dropdown(const char *const *items) {
  return items ? gtk_drop_down_new_from_strings(items)
               : gtk_drop_down_new(NULL, NULL);
}
static char *theme_path(void) {
  return g_build_filename(g_get_user_config_dir(), "minifuze2", "desktop.ini", NULL);
}
static void load_theme(Ui *u) {
  char *path = theme_path();
  GKeyFile *file = g_key_file_new();
  if (g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, NULL))
    u->dark = g_key_file_get_boolean(file, "Appearance", "dark", NULL);
  g_key_file_unref(file);
  g_free(path);
}
static void apply_theme(Ui *u) {
  GtkSettings *prefs = gtk_settings_get_default();
  GParamSpec *scheme = g_object_class_find_property(
      G_OBJECT_GET_CLASS(prefs), "gtk-interface-color-scheme");
  if (scheme) {
    GEnumClass *values = g_type_class_ref(G_PARAM_SPEC_VALUE_TYPE(scheme));
    GEnumValue *value = g_enum_get_value_by_nick(values, u->dark ? "dark" : "light");
    if (value)
      g_object_set(prefs, "gtk-interface-color-scheme", value->value, NULL);
    g_type_class_unref(values);
  } else
    g_object_set(prefs, "gtk-application-prefer-dark-theme", u->dark, NULL);
  char *css = g_strconcat((const char *)desktop_css,
                          u->dark ? (const char *)desktop_dark_css : "", NULL);
  gtk_css_provider_load_from_string(u->css, css);
  g_free(css);
  for (int i = 0; i < 7; i++)
    if (u->meters[i])
      gtk_widget_queue_draw(u->meters[i]);
  for (int i = 0; i < 5; i++)
    if (u->input_meters[i])
      gtk_widget_queue_draw(u->input_meters[i]);
}
static void theme_changed(GObject *unused, GParamSpec *spec, gpointer data) {
  (void)unused;
  (void)spec;
  Ui *u = data;
  u->dark = gtk_drop_down_get_selected(GTK_DROP_DOWN(u->theme)) == 1;
  apply_theme(u);
  char *path = theme_path(), *directory = g_path_get_dirname(path);
  GKeyFile *file = g_key_file_new();
  g_key_file_load_from_file(file, path, G_KEY_FILE_KEEP_COMMENTS, NULL);
  g_key_file_set_boolean(file, "Appearance", "dark", u->dark);
  gsize length = 0;
  char *contents = g_key_file_to_data(file, &length, NULL);
  GError *error = NULL;
  if (g_mkdir_with_parents(directory, 0700) != 0)
    notice(u, "Не удалось создать папку настроек темы", TRUE);
  else if (!g_file_set_contents_full(path, contents, (gssize)length,
                                      G_FILE_SET_CONTENTS_CONSISTENT |
                                          G_FILE_SET_CONTENTS_DURABLE,
                                      0600, &error)) {
    notice(u, error->message, TRUE);
    g_clear_error(&error);
  }
  g_free(contents);
  g_key_file_unref(file);
  g_free(directory);
  g_free(path);
}
static void clear_jobs(Ui *u) {
  memset(u->ref_pending, 0, sizeof u->ref_pending);
  while (!g_queue_is_empty(&u->jobs))
    job_free(g_queue_pop_head(&u->jobs));
  if (u->debounce) {
    g_source_remove(u->debounce);
    u->debounce = 0;
  }
  if (u->draft) {
    json_object_put(u->draft);
    u->draft = NULL;
  }
}
static gboolean valid_url(const char *url) {
  GError *error = NULL;
  GUri *uri = g_uri_parse(url, G_URI_FLAGS_NONE, &error);
  gboolean ok = uri && !g_strcmp0(g_uri_get_scheme(uri), "http") &&
                !g_strcmp0(g_uri_get_host(uri), "127.0.0.1") &&
                g_uri_get_port(uri) > 0 && g_uri_get_port(uri) <= 65535 &&
                !g_uri_get_userinfo(uri) && !g_uri_get_query(uri) &&
                !g_uri_get_fragment(uri) &&
                (!*g_uri_get_path(uri) || !strcmp(g_uri_get_path(uri), "/"));
  if (uri)
    g_uri_unref(uri);
  g_clear_error(&error);
  return ok;
}
static void refresh_url(Ui *u) {
  if (!u->ready_file)
    return;
  char *s = NULL;
  gsize n = 0;
  if (g_file_get_contents(u->ready_file, &s, &n, NULL) && n < 4096) {
    J *j = json_tokener_parse(s);
    const char *url = str(get(j, "url"));
    if (valid_url(url) && g_strcmp0(url, u->url)) {
      g_free(u->url);
      u->url = g_strdup(url);
      u->token[0] = 0;
    }
    if (j)
      json_object_put(j);
  }
  g_free(s);
}
static void start_service(GtkButton *unused, gpointer data) {
  (void)unused;
  Ui *u = data;
  u->start_attempted = TRUE;
  GError *error = NULL;
  GSubprocess *p = g_subprocess_new(
      G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
      &error, "systemctl", "--user", "start", "minifuze2.service", NULL);
  if (p) {
    g_object_unref(p);
    notice(u, "Ожидаем аудиослужбу…", FALSE);
  } else {
    notice(u, error->message, TRUE);
    g_clear_error(&error);
  }
}
static void queue(Ui *u, const char *path, J *b, GFile *file, guint revision) {
  if (!strcmp(path, "/api/mixer")) {
    for (GList *l = u->jobs.head; l; l = l->next) {
      Job *old = l->data;
      if (!strcmp(old->path, path)) {
        json_object_put(old->body);
        old->body = b;
        old->revision = revision;
        return;
      }
    }
  }
  Job *j = g_new0(Job, 1);
  j->path = g_strdup(path);
  j->body = b;
  j->revision = revision;
  if (file)
    j->export_file = g_object_ref(file);
  g_queue_push_tail(&u->jobs, j);
}
static void command(Ui *u, const char *path, J *b) {
  flush_mixer(u);
  if (!strcmp(path, "/api/load") || !strcmp(path, "/api/factory") ||
      !strcmp(path, "/api/import-mfp"))
    u->replacing = TRUE;
  queue(u, path, b, NULL, 0);
  if (u->replacing)
    render(u);
  dispatch(u);
}
static gboolean delayed_mix(gpointer data) {
  Ui *u = data;
  u->debounce = 0;
  flush_mixer(u);
  dispatch(u);
  return G_SOURCE_REMOVE;
}
static void flush_mixer(Ui *u) {
  if (u->debounce) {
    g_source_remove(u->debounce);
    u->debounce = 0;
  }
  if (u->draft && u->revision != u->sent_revision) {
    queue(u, "/api/mixer", jcopy(u->draft), NULL, u->revision);
    u->sent_revision = u->revision;
  }
}
static J *edit_mix(Ui *u) {
  if (!u->connected || !u->state)
    return NULL;
  if (!u->draft)
    u->draft = jcopy(get(u->state, "mixer"));
  return idx(get(u->draft, "mixes"), u->mix);
}
static void edited(Ui *u) {
  u->revision++;
  if (u->debounce)
    g_source_remove(u->debounce);
  u->debounce = g_timeout_add(160, delayed_mix, u);
  render(u);
}
static void gain_changed(GtkRange *range, gpointer data) {
  Ui *u = data;
  if (u->rendering)
    return;
  J *m = edit_mix(u);
  if (!m)
    return;
  int source = widget_id(range, "source"), channel = widget_id(range, "pan");
  double v = gtk_range_get_value(range);
  if (source == 6)
    number(m, "master_db", v);
  else {
    J *s = idx(get(m, "strips"), source);
    if (channel)
      json_object_array_put_idx(get(s, "pans"), channel - 1,
                                json_object_new_double(v));
    else
      number(s, "level_db", v);
  }
  edited(u);
}
static void mute_changed(GtkToggleButton *b, gpointer data) {
  Ui *u = data;
  if (u->rendering)
    return;
  J *m = edit_mix(u);
  if (!m)
    return;
  boolean(idx(get(m, "strips"), widget_id(b, "source")), "mute",
          gtk_toggle_button_get_active(b));
  edited(u);
  flush_mixer(u);
  dispatch(u);
}
static void hardware_changed(GtkToggleButton *b, gpointer data) {
  Ui *u = data;
  if (u->rendering || !u->connected)
    return;
  int i = widget_id(b, "switch");
  J *j = body();
  boolean(j, "enabled", gtk_toggle_button_get_active(b));
  const char *path = i < 2    ? "/api/inst"
                     : i == 2 ? "/api/mono"
                     : i == 3 ? "/api/phantom"
                              : "/api/hardware-routing";
  if (i < 2)
    integer(j, "channel", i + 1);
  if (i >= 4)
    text(j, "route", i == 4 ? "loopback" : i == 5 ? "usb12" : "monitor");
  command(u, path, j);
}
static void link_changed(GtkCheckButton *b, gpointer data) {
  Ui *u = data;
  if (u->rendering)
    return;
  J *j = body();
  integer(j, "mix", u->mix);
  boolean(j, "enabled", gtk_check_button_get_active(b));
  command(u, "/api/link", j);
}
static void visible_changed(GtkCheckButton *b, gpointer data) {
  Ui *u = data;
  if (u->rendering)
    return;
  J *j = body();
  integer(j, "mix", u->mix);
  integer(j, "index", widget_id(b, "index"));
  boolean(j, "visible", gtk_check_button_get_active(b));
  command(u, "/api/virtual", j);
}
static void name_changed(GtkEntry *entry, gpointer data) {
  Ui *u = data;
  if (u->rendering || !u->connected)
    return;
  J *j = body();
  int i = widget_id(entry, "source");
  text(j, "kind", i == 6 ? "mix" : "input");
  integer(j, "index", i == 6 ? u->mix : i);
  text(j, "name", gtk_editable_get_text(GTK_EDITABLE(entry)));
  command(u, "/api/rename", j);
}
static void ref_changed(GObject *obj, GParamSpec *pspec, gpointer data) {
  (void)pspec;
  Ui *u = data;
  if (u->rendering)
    return;
  guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(obj));
  if (selected >= G_N_ELEMENTS(marks))
    return;
  J *j = body();
  integer(j, "mix", u->mix);
  int source = widget_id(obj, "source");
  u->ref_pending[u->mix][source] = TRUE;
  u->ref_values[u->mix][source] = marks[selected];
  integer(j, "source", source);
  integer(j, "db", marks[selected]);
  command(u, "/api/peakref", j);
}
static void reset_ref(GtkButton *b, gpointer data) {
  Ui *u = data;
  J *j = body();
  integer(j, "mix", u->mix);
  integer(j, "source", widget_id(b, "source"));
  command(u, "/api/reset-peakref", j);
}
static void simple(GtkButton *b, gpointer data) {
  Ui *u = data;
  const char *path = g_object_get_data(G_OBJECT(b), "path");
  command(u, path, body());
}
static GtkWidget *simple_button(Ui *u, const char *s, const char *path) {
  GtkWidget *w = button(s, G_CALLBACK(simple), u);
  g_object_set_data(G_OBJECT(w), "path", (gpointer)path);
  return w;
}
static void route_changed(GtkButton *unused, gpointer data) {
  (void)unused;
  Ui *u = data;
  gboolean connected = yes(get(u->state, "route_selected"));
  J *j = body();
  boolean(j, "enabled", !connected);
  if (!connected) {
    guint i = gtk_drop_down_get_selected(GTK_DROP_DOWN(u->device));
    text(j, "port", str(get(idx(get(u->state, "devices"), (int)i), "port")));
  } else
    put(j, "port", NULL);
  command(u, "/api/routes", j);
}
static void tab_changed(GtkToggleButton *b, gpointer data) {
  Ui *u = data;
  if (u->rendering || !gtk_toggle_button_get_active(b))
    return;
  flush_mixer(u);
  u->page = widget_id(b, "page");
  if (u->page)
    u->mix = u->page - 1;
  gtk_stack_set_visible_child_name(GTK_STACK(u->pages),
                                   u->page ? "mixer" : "inputs");
  render(u);
  dispatch(u);
}
static void meters_draw(GtkDrawingArea *area, cairo_t *cr, int width,
                        int height, gpointer data) {
  Ui *u = data;
  int source = widget_id(area, "source"), input = widget_id(area, "input");
  gboolean physical = input && source < 2;
  J *m = get(u->state, physical ? "hardware_meters" : "meters");
  int offset = physical      ? source
               : input       ? source < 2 ? 6 + source : 8 + (source - 2) * 2
               : source == 6 ? 2 * u->mix
               : source < 2  ? 6 + source
                             : 8 + (source - 2) * 2;
  int channels = physical || (!input && source < 2) ? 1 : 2;
  J *levels = get(m, "display_levels"), *peaks = get(m, "peaks"),
    *clips = get(m, "clips");
  gboolean active = u->connected && yes(get(m, "active"));
  double w = channels == 1 ? 15 : 10, gap = 5,
         left = width / 2.0 - (channels * w + (channels - 1) * gap) / 2;
  double top = 10, bottom = height - 12, h = bottom - top;
  cairo_set_font_size(cr, 10);
  for (int tick = 0; tick <= 60; tick += 12) {
    double y = top + h * tick / 60.0;
    cairo_set_source_rgb(cr, u->dark ? .30 : .73, u->dark ? .38 : .78,
                          u->dark ? .43 : .8);
    cairo_set_line_width(cr, .5);
    cairo_move_to(cr, left - 5, y);
    cairo_line_to(cr, left + channels * (w + gap), y);
    cairo_stroke(cr);
    cairo_set_source_rgb(cr, u->dark ? .68 : .4, u->dark ? .76 : .47,
                          u->dark ? .81 : .5);
    char s[32];
    snprintf(s, sizeof s, "%d", -tick);
    cairo_move_to(cr, 2, y + 3);
    cairo_show_text(cr, s);
  }
  double orange = val(get(get(m, "preferences"), "orange_db"));
  for (int ch = 0; ch < channels; ch++) {
    double x = left + ch * (w + gap),
           v = active ? val(idx(levels, offset + ch)) : 0;
    double fraction = CLAMP((db(v) + 60) / 60, 0, 1);
    cairo_set_source_rgb(cr, u->dark ? .04 : .13, u->dark ? .08 : .2,
                          u->dark ? .10 : .24);
    cairo_rectangle(cr, x, top, w, h);
    cairo_fill(cr);
    if (yes(idx(clips, offset + ch)))
      cairo_set_source_rgb(cr, .76, .3, .26);
    else if (db(v) >= orange)
      cairo_set_source_rgb(cr, .76, .48, .09);
    else
      cairo_set_source_rgb(cr, .086, .53, .35);
    cairo_rectangle(cr, x, bottom - h * fraction, w, h * fraction);
    cairo_fill(cr);
    double peak =
        active ? CLAMP((db(val(idx(peaks, offset + ch))) + 60) / 60, 0, 1) : 0;
    cairo_set_source_rgb(cr, .1, .55, .7);
    cairo_rectangle(cr, x, bottom - h * peak, w, 2);
    cairo_fill(cr);
  }
  if (!input && u->state) {
    J *ref = idx(idx(get(get(u->state, "peakref"), "db"), u->mix), source);
    if (ref) {
      double y = bottom - h * (val(ref) + 60) / 60;
      cairo_set_source_rgb(cr, .05, .49, .6);
      cairo_set_line_width(cr, 1.5);
      cairo_move_to(cr, left - 5, y);
      cairo_line_to(cr, left + channels * (w + gap), y);
      cairo_stroke(cr);
    }
  }
}
static GtkWidget *meter(Ui *u, int source, gboolean input) {
  GtkWidget *w = gtk_drawing_area_new();
  tag(w, "source", source);
  tag(w, "input", input);
  gtk_widget_set_size_request(w, 68, 208);
  gtk_widget_set_vexpand(w, TRUE);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(w), meters_draw, u, NULL);
  return w;
}
static void entry_update(GtkWidget *w, const char *s) {
  if (!gtk_widget_has_focus(w) &&
      strcmp(gtk_editable_get_text(GTK_EDITABLE(w)), s))
    gtk_editable_set_text(GTK_EDITABLE(w), s);
}
static void render(Ui *u) {
  if (!u->window)
    return;
  u->rendering = TRUE;
  J *s = u->state, *preset = get(s, "preset"), *device = get(s, "device"),
    *audio = get(s, "audio");
  gboolean online = u->connected && s,
           template = online && yes(get(preset, "has_template"));
  gtk_widget_set_sensitive(u->workspace, online && !u->replacing);
  gtk_widget_set_sensitive(u->save, online && !u->replacing);
  gtk_widget_set_sensitive(u->load,
                           online && !u->replacing && yes(get(s, "has_saved")));
  gtk_widget_set_sensitive(u->export_button, template);
  gtk_label_set_text(GTK_LABEL(u->dirty), u->draft || yes(get(s, "dirty"))
                                              ? "Есть несохранённые изменения"
                                          : yes(get(s, "has_saved"))
                                              ? "Микс сохранён"
                                              : "Заводской микс");
  char status[256];
  if (online && get(s, "hardware"))
    snprintf(status, sizeof status, "MiniFuse подключена · %.0f Гц",
             val(get(audio, "actual_rate")));
  else
    snprintf(status, sizeof status, "%s",
             online ? yes(get(s, "route_selected"))
                          ? "Ожидаем подключения MiniFuse"
                          : "Выбери MiniFuse слева"
                    : "Аудиослужба недоступна");
  gtk_label_set_text(GTK_LABEL(u->status), status);
  gtk_widget_set_visible(u->reconnect, !online);
  gtk_widget_set_sensitive(u->link, template);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(u->link),
                              yes(idx(get(preset, "links"), u->mix)));
  gtk_widget_set_sensitive(u->mix_name, template);
  entry_update(u->mix_name, str(idx(get(preset, "mix_names"), u->mix)));
  J *mix = idx(get(u->draft ? u->draft : get(s, "mixer"), "mixes"), u->mix);
  for (int i = 0; i < 7; i++) {
    gboolean visible =
        i < 3 || i == 6 || !template ||
        yes(idx(idx(get(preset, "virtual_visible"), u->mix), i - 3));
    gtk_widget_set_visible(u->strips[i], visible);
    if (i < 6) {
      J *strip = idx(get(mix, "strips"), i);
      double level = val(get(strip, "level_db"));
      gtk_range_set_value(GTK_RANGE(u->gain[i]), level);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(u->mute[i]),
                                   yes(get(strip, "mute")));
      gtk_button_set_label(GTK_BUTTON(u->mute[i]),
                           yes(get(strip, "mute")) ? "Mute ✓" : "Mute");
      for (int ch = 0; ch < (i < 2 ? 1 : 2); ch++)
        gtk_range_set_value(GTK_RANGE(u->pan[i][ch]),
                            val(idx(get(strip, "pans"), ch)));
      gtk_widget_set_sensitive(u->names[i], template);
      entry_update(u->names[i], str(idx(get(preset, "input_names"), i)));
    } else
      gtk_range_set_value(GTK_RANGE(u->gain[i]), val(get(mix, "master_db")));
    double level = gtk_range_get_value(GTK_RANGE(u->gain[i]));
    char value[40];
    if (level <= -59.9)
      snprintf(value, sizeof value, "−∞ dB");
    else
      snprintf(value, sizeof value, "%.1f dB", level);
    gtk_label_set_text(GTK_LABEL(u->gain_text[i]), value);
    J *ref = idx(idx(get(get(s, "peakref"), "db"), u->mix), i);
    guint selected = GTK_INVALID_LIST_POSITION;
    if (u->ref_pending[u->mix][i]) {
      for (guint k = 0; k < G_N_ELEMENTS(marks); k++)
        if (marks[k] == u->ref_values[u->mix][i])
          selected = k;
    } else if (ref)
      for (guint k = 0; k < G_N_ELEMENTS(marks); k++)
        if (marks[k] == json_object_get_int(ref))
          selected = k;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(u->refs[i]), selected);
    gboolean hit = yes(idx(idx(get(get(s, "peakref"), "exceeded"), u->mix), i));
    gtk_widget_set_sensitive(u->ref_reset[i], hit);
    if (hit)
      gtk_widget_add_css_class(u->ref_reset[i], "error");
    else
      gtk_widget_remove_css_class(u->ref_reset[i], "error");
    int offset = i == 6  ? u->mix * 2
                 : i < 2 ? 6 + i
                         : 8 + (i - 2) * 2,
        count = i < 2 ? 1 : 2;
    J *meters = get(s, "meters");
    double peak = val(idx(get(meters, "levels"), offset));
    if (count == 2)
      peak = MAX(peak, val(idx(get(meters, "levels"), offset + 1)));
    if (peak > 0)
      snprintf(value, sizeof value, "%.1f dBFS", db(peak));
    else
      snprintf(value, sizeof value, "−∞ dBFS");
    gtk_label_set_text(GTK_LABEL(u->peaks[i]), value);
    gtk_widget_queue_draw(u->meters[i]);
  }
  for (int i = 0; i < 3; i++) {
    gtk_widget_set_sensitive(u->visible[i], template);
    gtk_check_button_set_active(
        GTK_CHECK_BUTTON(u->visible[i]),
        !template || yes(idx(idx(get(preset, "virtual_visible"), u->mix), i)));
  }
  for (int i = 0; i < 7; i++) {
    gboolean supported = online && yes(get(device, "ok")) &&
                         (i < 2 || yes(get(get(device, "controls"),
                                           i == 2   ? "direct_mono"
                                           : i == 3 ? "phantom48v"
                                                    : "hardware_routing")));
    gtk_widget_set_sensitive(u->hardware[i], supported);
    gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(u->hardware[i]),
        yes(get(get(get(device, "status"), "switches"), switches[i])));
  }
  unsigned fw = 0;
  sscanf(str(get(get(device, "identity"), "bcd_device")), "%x", &fw);
  char firmware[80];
  snprintf(firmware, sizeof firmware, "Прошивка %u.%u.%u", fw >> 8,
           (fw >> 4) & 15, fw & 15);
  gtk_label_set_text(GTK_LABEL(u->firmware), yes(get(device, "ok"))
                                                 ? firmware
                                                 : "USB-управление недоступно");
  J *devices = get(s, "devices");
  const char *key =
      devices ? json_object_to_json_string_ext(devices, JSON_C_TO_STRING_PLAIN)
              : "[]";
  if (g_strcmp0(key, u->device_key)) {
    g_free(u->device_key);
    u->device_key = g_strdup(key);
    GtkStringList *list = gtk_string_list_new(NULL);
    guint selected = 0;
    for (size_t i = 0; i < jlen(devices); i++) {
      char name[128];
      snprintf(name, sizeof name, "MiniFuse 2 · %s",
               str(get(idx(devices, (int)i), "port")));
      gtk_string_list_append(list, name);
      if (!strcmp(str(get(idx(devices, (int)i), "port")),
                  str(get(get(s, "hardware"), "port"))))
        selected = (guint)i;
    }
    if (!jlen(devices))
      gtk_string_list_append(list, "Нет подключённой MiniFuse");
    gtk_drop_down_set_model(GTK_DROP_DOWN(u->device), G_LIST_MODEL(list));
    g_object_unref(list);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(u->device), selected);
  }
  gtk_button_set_label(GTK_BUTTON(u->route), yes(get(s, "route_selected"))
                                                 ? "Отключить от микшера"
                                                 : "Подключить к микшеру");
  gtk_widget_set_sensitive(
      u->route, online && (yes(get(s, "route_selected")) || jlen(devices) > 0));
  for (int i = 0; i < 5; i++) {
    J *m = get(s, i < 2 ? "hardware_meters" : "meters");
    int offset = i < 2 ? i : 8 + (i - 2) * 2;
    double v = val(idx(get(m, "levels"), offset));
    if (i >= 2)
      v = MAX(v, val(idx(get(m, "levels"), offset + 1)));
    char text[48];
    snprintf(text, sizeof text, "%.1f dBFS", db(v));
    gtk_label_set_text(GTK_LABEL(u->input_levels[i]), v > 0 ? text : "−∞ dBFS");
    gtk_widget_queue_draw(u->input_meters[i]);
  }
  if (u->audio_info) {
    char info[256];
    if (!online)
      snprintf(info, sizeof info, "Аудиослужба недоступна. Выбранные значения "
                                   "ещё не применены.");
    else if (val(get(audio, "actual_rate")) <= 0)
      snprintf(info, sizeof info, "Текущая частота недоступна\n%s",
               str(get(audio, "error")));
    else
      snprintf(info, sizeof info, "Текущий режим: %.0f Гц · %s\n%s",
               val(get(audio, "actual_rate")),
               val(get(audio, "requested_quantum")) ? "ручной буфер" : "Auto",
               str(get(audio, "error")));
    gtk_label_set_text(GTK_LABEL(u->audio_info), info);
    gtk_widget_set_sensitive(u->audio_apply, online);
    gtk_widget_set_sensitive(u->audio_reset, online);
    gtk_widget_set_sensitive(u->meters_apply, online);
  }
  u->rendering = FALSE;
}
typedef struct {
  Ui *ui;
  GFile *file;
  char *text;
} Export;
static void exported(GObject *source, GAsyncResult *res, gpointer data) {
  Export *e = data;
  GError *error = NULL;
  if (g_file_replace_contents_finish(G_FILE(source), res, NULL, &error))
    notice(e->ui, "Пресет .mfp сохранён", FALSE);
  else {
    notice(e->ui, error->message, TRUE);
    g_clear_error(&error);
  }
  g_object_unref(e->file);
  g_free(e->text);
  g_free(e);
}
static void save_export(Ui *u, GFile *file, const char *s) {
  Export *e = g_new0(Export, 1);
  e->ui = u;
  e->file = g_object_ref(file);
  e->text = g_strdup(s);
  g_file_replace_contents_async(file, e->text, strlen(e->text), NULL, FALSE,
                                G_FILE_CREATE_PRIVATE, NULL, exported, e);
}
static void request_done(GObject *source, GAsyncResult *res, gpointer data) {
  Request *r = data;
  Ui *u = r->ui;
  GError *error = NULL;
  GBytes *bytes =
      soup_session_send_and_read_finish(SOUP_SESSION(source), res, &error);
  guint code = soup_message_get_status(r->message);
  gsize size = 0;
  const char *raw = bytes ? g_bytes_get_data(bytes, &size) : NULL;
  char *content = raw && size < 1024 * 1024 ? g_strndup(raw, size) : NULL;
  J *j = NULL;
  gboolean ok = content && code >= 200 && code < 300;
  if (ok && r->kind == 0) {
    const char *needle = "name=\"csrf-token\" content=\"";
    char *p = strstr(content, needle);
    ok = FALSE;
    if (p) {
      p += strlen(needle);
      if (strlen(p) >= 65 && p[64] == '"') {
        ok = TRUE;
        for (int i = 0; i < 64; i++)
          if (!g_ascii_isxdigit(p[i]))
            ok = FALSE;
        if (ok) {
          memcpy(u->token, p, 64);
          u->token[64] = 0;
        }
      }
    }
  } else if (content) {
    j = json_tokener_parse(content);
    if (ok) {
      if (r->job && r->job->export_file) {
        const char *s = str(get(j, "text"));
        ok = *s;
        if (ok)
          save_export(u, r->job->export_file, s);
      } else {
        ok = get(j, "mixer") && jlen(get(get(j, "mixer"), "mixes")) == 3;
        if (ok) {
          if (u->state)
            json_object_put(u->state);
          u->state = j;
          j = NULL;
          if (!u->connected)
            notice(u, "Готово · изменения применяются сразу", FALSE);
          u->connected = TRUE;
          if (r->job && !strcmp(r->job->path, "/api/peakref")) {
            int m = json_object_get_int(get(r->job->body, "mix"));
            int i = json_object_get_int(get(r->job->body, "source"));
            if (m >= 0 && m < 3 && i >= 0 && i < 7 &&
                u->ref_values[m][i] ==
                    json_object_get_int(get(r->job->body, "db")))
              u->ref_pending[m][i] = FALSE;
          }
          if (r->job && !strcmp(r->job->path, "/api/mixer") &&
              r->job->revision == u->revision) {
            if (u->draft)
              json_object_put(u->draft);
            u->draft = NULL;
          }
          if (r->job)
            notice(u,
                   !strcmp(r->job->path, "/api/save") ? "Микс сохранён"
                                                      : "Изменения применены",
                   FALSE);
        }
      }
    }
  }
  if (!ok) {
    u->replacing = FALSE;
    const char *why = j       ? str(get(j, "error"))
                      : error ? error->message
                              : "Некорректный ответ аудиослужбы";
    notice(u, *why ? why : "Аудиослужба недоступна", TRUE);
    clear_jobs(u);
    if (error || code == 403 || r->kind == 0) {
      u->token[0] = 0;
      u->connected = FALSE;
    }
    if (error && !u->start_attempted)
      start_service(NULL, u);
  }
  if (j)
    json_object_put(j);
  g_free(content);
  if (bytes)
    g_bytes_unref(bytes);
  g_clear_error(&error);
  if (r->job && (!strcmp(r->job->path, "/api/load") ||
                 !strcmp(r->job->path, "/api/factory") ||
                 !strcmp(r->job->path, "/api/import-mfp")))
    u->replacing = FALSE;
  job_free(r->job);
  g_object_unref(r->message);
  g_free(r);
  u->busy = FALSE;
  render(u);
  if (u->closing && g_queue_is_empty(&u->jobs) && !u->draft) {
    gtk_window_destroy(GTK_WINDOW(u->window));
    u->window = NULL;
    return;
  }
  if (ok && !g_queue_is_empty(&u->jobs))
    dispatch(u);
}
static void dispatch(Ui *u) {
  if (u->busy || !u->window)
    return;
  refresh_url(u);
  Request *r = g_new0(Request, 1);
  r->ui = u;
  const char *path;
  if (!u->token[0]) {
    r->kind = 0;
    path = "/";
  } else if (!g_queue_is_empty(&u->jobs)) {
    r->kind = 2;
    r->job = g_queue_pop_head(&u->jobs);
    path = r->job->path;
  } else {
    r->kind = 1;
    path = "/api/state";
  }
  char *url = g_strconcat(u->url, path, NULL);
  r->message = soup_message_new(r->kind == 2 ? "POST" : "GET", url);
  g_free(url);
  soup_message_set_flags(r->message, SOUP_MESSAGE_NO_REDIRECT);
  if (r->job) {
    SoupMessageHeaders *headers = soup_message_get_request_headers(r->message);
    soup_message_headers_append(headers, "Origin", u->url);
    soup_message_headers_append(headers, "X-CSRF-Token", u->token);
    const char *s =
        json_object_to_json_string_ext(r->job->body, JSON_C_TO_STRING_PLAIN);
    GBytes *b = g_bytes_new(s, strlen(s));
    soup_message_set_request_body_from_bytes(r->message, "application/json", b);
    g_bytes_unref(b);
  }
  u->busy = TRUE;
  soup_session_send_and_read_async(u->http, r->message, G_PRIORITY_DEFAULT,
                                   NULL, request_done, r);
}
static gboolean tick(gpointer data) {
  Ui *u = data;
  if (!u->closing)
    dispatch(u);
  return G_SOURCE_CONTINUE;
}
static void loaded_file(GObject *source, GAsyncResult *res, gpointer data) {
  Ui *u = data;
  char *s = NULL;
  gsize n = 0;
  GError *error = NULL;
  if (g_file_load_contents_finish(G_FILE(source), res, &s, &n, NULL, &error)) {
    if (n > 32768 || memchr(s, 0, n) || !g_utf8_validate(s, (gssize)n, NULL))
      notice(u, "Нужен UTF-8 пресет .mfp размером до 32 КиБ", TRUE);
    else {
      J *j = body();
      text(j, "text", s);
      command(u, "/api/import-mfp", j);
    }
  } else {
    notice(u, error->message, TRUE);
    g_clear_error(&error);
  }
  g_free(s);
}
static void chosen_open(GObject *source, GAsyncResult *res, gpointer data) {
  Ui *u = data;
  GError *error = NULL;
  GFile *f = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), res, &error);
  if (f) {
    g_file_load_contents_async(f, NULL, loaded_file, u);
    g_object_unref(f);
  }
  g_clear_error(&error);
  g_object_unref(source);
}
static void chosen_save(GObject *source, GAsyncResult *res, gpointer data) {
  Ui *u = data;
  GError *error = NULL;
  GFile *f = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), res, &error);
  if (f) {
    flush_mixer(u);
    queue(u, "/api/export-mfp", body(), f, 0);
    g_object_unref(f);
    dispatch(u);
  }
  g_clear_error(&error);
  g_object_unref(source);
}
static void open_preset(GtkButton *unused, gpointer data) {
  (void)unused;
  Ui *u = data;
  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Открыть пресет MiniFuse");
  GtkFileFilter *filter = gtk_file_filter_new();
  gtk_file_filter_set_name(filter, "MiniFuse preset (*.mfp)");
  gtk_file_filter_add_pattern(filter, "*.mfp");
  GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
  g_list_store_append(filters, filter);
  gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
  g_object_unref(filter);
  g_object_unref(filters);
  gtk_file_dialog_open(dialog, GTK_WINDOW(u->window), NULL, chosen_open, u);
}
static void export_preset(GtkButton *unused, gpointer data) {
  (void)unused;
  Ui *u = data;
  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Сохранить пресет MiniFuse");
  gtk_file_dialog_set_initial_name(dialog, "MiniFuse2.mfp");
  gtk_file_dialog_save(dialog, GTK_WINDOW(u->window), NULL, chosen_save, u);
}
static void apply_audio(GtkButton *unused, gpointer data) {
  (void)unused;
  Ui *u = data;
  guint r = gtk_drop_down_get_selected(GTK_DROP_DOWN(u->rate)),
        q = gtk_drop_down_get_selected(GTK_DROP_DOWN(u->quantum));
  if (r >= G_N_ELEMENTS(rates) || q >= G_N_ELEMENTS(quanta))
    return;
  J *j = body();
  integer(j, "rate", rates[r]);
  integer(j, "quantum", quanta[q]);
  boolean(j, "remember",
          gtk_check_button_get_active(GTK_CHECK_BUTTON(u->remember)));
  command(u, "/api/audio-settings", j);
}
static void apply_meters(GtkButton *unused, gpointer data) {
  (void)unused;
  Ui *u = data;
  const int values[] = {-1, -3, -6, -9, -12};
  guint o = gtk_drop_down_get_selected(GTK_DROP_DOWN(u->orange)),
        c = gtk_drop_down_get_selected(GTK_DROP_DOWN(u->clip)),
        p = gtk_drop_down_get_selected(GTK_DROP_DOWN(u->peak));
  if (o >= G_N_ELEMENTS(values) || c >= G_N_ELEMENTS(clip_values) ||
      p >= G_N_ELEMENTS(peak_values))
    return;
  J *j = body();
  integer(j, "version", 1);
  integer(j, "orange_db", values[o]);
  text(j, "clip_reset", clip_values[c]);
  text(j, "peak_reset", peak_values[p]);
  command(u, "/api/meter-preferences", j);
}
static void prefs_destroy(GtkWidget *unused, gpointer data) {
  (void)unused;
  Ui *u = data;
  u->prefs = u->audio_info = u->rate = u->quantum = u->remember = u->orange =
      u->clip = u->peak = NULL;
  u->audio_apply = u->audio_reset = u->meters_apply = NULL;
  u->theme = NULL;
}
static void field(GtkWidget *b, const char *s, GtkWidget *w) {
  GtkWidget *l = label(s, NULL);
  gtk_widget_set_halign(l, GTK_ALIGN_START);
  add(b, l);
  add(b, w);
  gtk_accessible_update_property(GTK_ACCESSIBLE(w),
                                  GTK_ACCESSIBLE_PROPERTY_LABEL, s, -1);
}
static void settings(GtkButton *unused, gpointer data) {
  (void)unused;
  Ui *u = data;
  if (u->prefs) {
    gtk_window_present(GTK_WINDOW(u->prefs));
    return;
  }
  u->prefs = gtk_window_new();
  gtk_window_set_application(GTK_WINDOW(u->prefs), u->app);
  gtk_window_set_title(GTK_WINDOW(u->prefs), "Настройки MiniFuse 2");
  gtk_window_set_default_size(GTK_WINDOW(u->prefs), 460, 620);
  gtk_window_set_transient_for(GTK_WINDOW(u->prefs), GTK_WINDOW(u->window));
  gtk_window_set_destroy_with_parent(GTK_WINDOW(u->prefs), TRUE);
  g_signal_connect(u->prefs, "destroy", G_CALLBACK(prefs_destroy), u);
  GtkWidget *b = box(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(b, "settings-content");
  gtk_widget_set_margin_start(b, 24);
  gtk_widget_set_margin_end(b, 24);
  gtk_widget_set_margin_top(b, 24);
  gtk_widget_set_margin_bottom(b, 24);
  GtkWidget *scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(scroll), FALSE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), b);
  gtk_window_set_child(GTK_WINDOW(u->prefs), scroll);
  add(b, label("Оформление", "section"));
  const char *themes[] = {"Светлая", "Тёмная", NULL};
  u->theme = dropdown(themes);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(u->theme), u->dark ? 1 : 0);
  g_signal_connect(u->theme, "notify::selected", G_CALLBACK(theme_changed), u);
  field(b, "Тема", u->theme);
  add(b, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
  add(b, label("Частота и буфер", "section"));
  u->audio_info = label("", "hint");
  gtk_label_set_wrap(GTK_LABEL(u->audio_info), TRUE);
  add(b, u->audio_info);
  const char *rs[] = {"Автоматически", "44 100 Гц",  "48 000 Гц",  "88 200 Гц",
                      "96 000 Гц",     "176 400 Гц", "192 000 Гц", NULL};
  const char *qs[] = {"Автоматически", "32",   "64",   "128", "256",
                      "512",           "1024", "2048", NULL};
  u->rate = dropdown(rs);
  u->quantum = dropdown(qs);
  field(b, "Частота", u->rate);
  field(b, "Буфер (сэмплы)", u->quantum);
  J *audio = get(u->state, "audio");
  for (guint i = 0; i < G_N_ELEMENTS(rates); i++)
    if (rates[i] == val(get(audio, "requested_rate")))
      gtk_drop_down_set_selected(GTK_DROP_DOWN(u->rate), i);
  for (guint i = 0; i < G_N_ELEMENTS(quanta); i++)
    if (quanta[i] == val(get(audio, "requested_quantum")))
      gtk_drop_down_set_selected(GTK_DROP_DOWN(u->quantum), i);
  u->remember = gtk_check_button_new_with_label("Запомнить для этой карты");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(u->remember), TRUE);
  add(b, u->remember);
  GtkWidget *hint = label(
      "Параметры общие для аудиослужбы. Переключение кратко прерывает звук.",
      "hint");
  gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
  add(b, hint);
  u->audio_apply = button("Применить", G_CALLBACK(apply_audio), u);
  gtk_widget_add_css_class(u->audio_apply, "suggested-action");
  add(b, u->audio_apply);
  u->audio_reset = simple_button(u, "Вернуть Auto", "/api/audio-reset");
  add(b, u->audio_reset);
  add(b, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
  add(b, label("Индикаторы", "section"));
  const char *os[] = {"−1 dBFS", "−3 dBFS",  "−6 dBFS",
                      "−9 dBFS", "−12 dBFS", NULL};
  const char *cs[] = {"До сброса", "1,5 секунды", "3 секунды", NULL};
  const char *ps[] = {"Не показывать", "До сброса", "1 секунда", "2 секунды",
                      "3 секунды",     "4 секунды", "5 секунд",  NULL};
  u->orange = dropdown(os);
  u->clip = dropdown(cs);
  u->peak = dropdown(ps);
  field(b, "Оранжевый уровень", u->orange);
  field(b, "Удержание перегрузки", u->clip);
  field(b, "Удержание пика", u->peak);
  J *prefs = get(get(u->state, "meters"), "preferences");
  const int orange[] = {-1, -3, -6, -9, -12};
  for (guint i = 0; i < G_N_ELEMENTS(orange); i++)
    if (orange[i] == val(get(prefs, "orange_db")))
      gtk_drop_down_set_selected(GTK_DROP_DOWN(u->orange), i);
  for (guint i = 0; i < G_N_ELEMENTS(clip_values); i++)
    if (!strcmp(clip_values[i], str(get(prefs, "clip_reset"))))
      gtk_drop_down_set_selected(GTK_DROP_DOWN(u->clip), i);
  for (guint i = 0; i < G_N_ELEMENTS(peak_values); i++)
    if (!strcmp(peak_values[i], str(get(prefs, "peak_reset"))))
      gtk_drop_down_set_selected(GTK_DROP_DOWN(u->peak), i);
  u->meters_apply = button("Сохранить настройки индикаторов",
                            G_CALLBACK(apply_meters), u);
  add(b, u->meters_apply);
  add(b, label("MiniFuse 2 for Linux · 0.1.0-preview\nНезависимый проект, не "
               "приложение Arturia.",
               "hint"));
  render(u);
  gtk_window_present(GTK_WINDOW(u->prefs));
}
static GtkWidget *make_strip(Ui *u, int i) {
  GtkWidget *b = box(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_add_css_class(b, "strip");
  if (i == 6)
    gtk_widget_add_css_class(b, "master");
  gtk_widget_set_size_request(b, 128, -1);
  gtk_widget_set_hexpand(b, TRUE);
  u->strips[i] = b;
  add(b, label(strip_names[i], "strip-title"));
  if (i < 6) {
    u->names[i] = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(u->names[i]), "Имя канала");
    gtk_widget_set_tooltip_text(u->names[i], "Имя канала · Enter для применения");
    gtk_entry_set_max_length(GTK_ENTRY(u->names[i]), 256);
    gtk_widget_set_size_request(u->names[i], -1, 34);
    gtk_editable_set_width_chars(GTK_EDITABLE(u->names[i]), 8);
    tag(u->names[i], "source", i);
    g_signal_connect(u->names[i], "activate", G_CALLBACK(name_changed), u);
    add(b, u->names[i]);
  } else {
    GtkWidget *alias = label("Стереомикс", "hint");
    gtk_widget_set_size_request(alias, -1, 34);
    add(b, alias);
  }
  GtkWidget *pans = box(GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_size_request(pans, -1, 96);
  add(b, pans);
  if (i < 6) {
    for (int ch = 0; ch < (i < 2 ? 1 : 2); ch++) {
      add(pans, label(i < 2 ? "Панорама"
                      : ch  ? "Правый канал"
                            : "Левый канал",
                      "hint"));
      u->pan[i][ch] =
          gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1, .01);
      gtk_scale_set_draw_value(GTK_SCALE(u->pan[i][ch]), FALSE);
      tag(u->pan[i][ch], "source", i);
      tag(u->pan[i][ch], "pan", ch + 1);
      char accessible[96];
      snprintf(accessible, sizeof accessible, "%s · панорама %s", strip_names[i],
               i < 2 ? "" : ch ? "правого канала" : "левого канала");
      gtk_accessible_update_property(GTK_ACCESSIBLE(u->pan[i][ch]),
                                      GTK_ACCESSIBLE_PROPERTY_LABEL,
                                      accessible, -1);
      g_signal_connect(u->pan[i][ch], "value-changed", G_CALLBACK(gain_changed),
                       u);
      add(pans, u->pan[i][ch]);
    }
  } else
    add(pans, label("После общего\nфейдера", "hint"));
  GtkWidget *faders = box(GTK_ORIENTATION_HORIZONTAL, 3);
  u->meters[i] = meter(u, i, FALSE);
  add(faders, u->meters[i]);
  u->gain[i] = gtk_scale_new_with_range(GTK_ORIENTATION_VERTICAL, -60, 0, .5);
  gtk_range_set_inverted(GTK_RANGE(u->gain[i]), TRUE);
  gtk_scale_set_draw_value(GTK_SCALE(u->gain[i]), FALSE);
  gtk_widget_set_vexpand(u->gain[i], TRUE);
  gtk_widget_set_size_request(u->gain[i], 30, 208);
  tag(u->gain[i], "source", i);
  char accessible[96];
  snprintf(accessible, sizeof accessible, "%s · уровень, dB", strip_names[i]);
  gtk_accessible_update_property(GTK_ACCESSIBLE(u->gain[i]),
                                  GTK_ACCESSIBLE_PROPERTY_LABEL,
                                  accessible, -1);
  g_signal_connect(u->gain[i], "value-changed", G_CALLBACK(gain_changed), u);
  add(faders, u->gain[i]);
  add(b, faders);
  u->gain_text[i] = label("0.0 dB", "gain");
  add(b, u->gain_text[i]);
  u->peaks[i] = label("−∞ dBFS", "hint");
  add(b, u->peaks[i]);
  if (i < 6) {
    u->mute[i] = gtk_toggle_button_new_with_label("Mute");
    gtk_widget_add_css_class(u->mute[i], "control");
    gtk_widget_add_css_class(u->mute[i], "mute");
    snprintf(accessible, sizeof accessible, "%s · Mute", strip_names[i]);
    gtk_accessible_update_property(GTK_ACCESSIBLE(u->mute[i]),
                                    GTK_ACCESSIBLE_PROPERTY_LABEL,
                                    accessible, -1);
    tag(u->mute[i], "source", i);
    g_signal_connect(u->mute[i], "toggled", G_CALLBACK(mute_changed), u);
    add(b, u->mute[i]);
  } else {
    GtkWidget *spacer = label("Выход микса", "hint");
    gtk_widget_set_size_request(spacer, -1, 34);
    add(b, spacer);
  }
  const char *rs[] = {"0 dBFS",   "−6 dBFS",  "−12 dBFS", "−18 dBFS",
                      "−24 dBFS", "−30 dBFS", "−36 dBFS", "−42 dBFS",
                      "−60 dBFS", NULL};
  GtkWidget *refrow = box(GTK_ORIENTATION_HORIZONTAL, 4);
  u->refs[i] = dropdown(rs);
  tag(u->refs[i], "source", i);
  gtk_widget_set_tooltip_text(u->refs[i], "Порог PeakRef");
  snprintf(accessible, sizeof accessible, "%s · порог PeakRef", strip_names[i]);
  gtk_accessible_update_property(GTK_ACCESSIBLE(u->refs[i]),
                                  GTK_ACCESSIBLE_PROPERTY_LABEL,
                                  accessible, -1);
  g_signal_connect(u->refs[i], "notify::selected", G_CALLBACK(ref_changed), u);
  add(refrow, u->refs[i]);
  u->ref_reset[i] = gtk_button_new_from_icon_name("edit-clear-symbolic");
  gtk_widget_add_css_class(u->ref_reset[i], "control");
  tag(u->ref_reset[i], "source", i);
  gtk_widget_set_tooltip_text(u->ref_reset[i], "Сбросить превышение PeakRef");
  snprintf(accessible, sizeof accessible, "%s · сброс PeakRef", strip_names[i]);
  gtk_accessible_update_property(GTK_ACCESSIBLE(u->ref_reset[i]),
                                  GTK_ACCESSIBLE_PROPERTY_LABEL,
                                  accessible, -1);
  g_signal_connect(u->ref_reset[i], "clicked", G_CALLBACK(reset_ref), u);
  add(refrow, u->ref_reset[i]);
  add(b, refrow);
  return b;
}
static gboolean close_window(GtkWindow *window, gpointer data) {
  (void)window;
  Ui *u = data;
  flush_mixer(u);
  if (u->busy || !g_queue_is_empty(&u->jobs)) {
    u->closing = TRUE;
    gtk_widget_set_sensitive(u->root, FALSE);
    notice(u, "Завершаем отправку изменений…", FALSE);
    dispatch(u);
    return TRUE;
  }
  u->window = NULL;
  return FALSE;
}
static void activate(GtkApplication *app, gpointer data) {
  Ui *u = data;
  if (u->window) {
    gtk_window_present(GTK_WINDOW(u->window));
    return;
  }
  u->app = app;
  u->window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(u->window), "MiniFuse 2 for Linux");
  gtk_window_set_default_size(GTK_WINDOW(u->window), 1280, 820);
  gtk_window_set_icon_name(GTK_WINDOW(u->window),
                           "io.github.minifuze2.Control");
  g_signal_connect(u->window, "close-request", G_CALLBACK(close_window), u);
  u->css = gtk_css_provider_new();
  load_theme(u);
  apply_theme(u);
  gtk_style_context_add_provider_for_display(
      gtk_widget_get_display(u->window), GTK_STYLE_PROVIDER(u->css),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  GtkWidget *header = gtk_header_bar_new(),
            *brand = box(GTK_ORIENTATION_VERTICAL, 0);
  gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(header),
                                        ":minimize,maximize,close");
  add(brand, label("MiniFuse 2", "brand"));
  add(brand, label("Панель управления Linux", "subtitle"));
  gtk_header_bar_pack_start(GTK_HEADER_BAR(header), brand);
  u->status = label("Подключение…", "hint");
  gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), u->status);
  u->save = simple_button(u, "Сохранить", "/api/save");
  gtk_widget_add_css_class(u->save, "suggested-action");
  gtk_header_bar_pack_end(GTK_HEADER_BAR(header),
                          button("Настройки", G_CALLBACK(settings), u));
  gtk_header_bar_pack_end(GTK_HEADER_BAR(header), u->save);
  u->load = simple_button(u, "Загрузить", "/api/load");
  gtk_header_bar_pack_end(GTK_HEADER_BAR(header), u->load);
  gtk_window_set_titlebar(GTK_WINDOW(u->window), header);
  u->root = box(GTK_ORIENTATION_VERTICAL, 0);
  gtk_window_set_child(GTK_WINDOW(u->window), u->root);
  GtkWidget *tabs = box(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_add_css_class(tabs, "tabs");
  const char *names[] = {"Входы", "Monitor & Phones", "Loopback Mix 3/4",
                         "Loopback Mix 5/6"};
  for (int i = 0; i < 4; i++) {
    u->tabs[i] = gtk_toggle_button_new_with_label(names[i]);
    gtk_widget_add_css_class(u->tabs[i], "control");
    if (i)
      gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(u->tabs[i]),
                                  GTK_TOGGLE_BUTTON(u->tabs[0]));
    tag(u->tabs[i], "page", i);
    g_signal_connect(u->tabs[i], "toggled", G_CALLBACK(tab_changed), u);
    add(tabs, u->tabs[i]);
  }
  add(u->root, tabs);
  u->workspace = box(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_vexpand(u->workspace, TRUE);
  add(u->root, u->workspace);
  GtkWidget *sidebar_scroll = gtk_scrolled_window_new(),
            *sidebar = box(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_add_css_class(sidebar, "sidebar");
  gtk_widget_set_size_request(sidebar_scroll, 232, -1);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sidebar_scroll),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sidebar_scroll), sidebar);
  add(u->workspace, sidebar_scroll);
  add(sidebar, label("Аудиоинтерфейс", "section"));
  u->device = dropdown(NULL);
  add(sidebar, u->device);
  u->route = button("Подключить к микшеру", G_CALLBACK(route_changed), u);
  add(sidebar, u->route);
  u->firmware = label("", "hint");
  add(sidebar, u->firmware);
  add(sidebar, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
  add(sidebar, label("Аналоговые входы", "section"));
  GtkWidget *inst = box(GTK_ORIENTATION_HORIZONTAL, 8);
  add(sidebar, inst);
  const char *hardware_labels[] = {
      "INST 1",   "INST 2",     "Direct Mono",      "+48V",
      "Loopback", "На USB 1/2", "На Monitor/Phones"};
  GtkWidget *routes = box(GTK_ORIENTATION_VERTICAL, 6);
  for (int i = 0; i < 7; i++) {
    u->hardware[i] = gtk_toggle_button_new_with_label(hardware_labels[i]);
    gtk_widget_add_css_class(u->hardware[i], "control");
    tag(u->hardware[i], "switch", i);
    g_signal_connect(u->hardware[i], "toggled", G_CALLBACK(hardware_changed),
                     u);
    add(i < 2 ? inst : i < 4 ? sidebar : routes, u->hardware[i]);
  }
  gtk_widget_add_css_class(u->hardware[3], "destructive-action");
  GtkWidget *expander = gtk_expander_new("Аппаратный loopback");
  gtk_expander_set_child(GTK_EXPANDER(expander), routes);
  add(sidebar, expander);
  add(sidebar, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
  add(sidebar, label("Пресеты", "section"));
  add(sidebar, button("Открыть .mfp…", G_CALLBACK(open_preset), u));
  u->export_button = button("Экспорт .mfp…", G_CALLBACK(export_preset), u);
  add(sidebar, u->export_button);
  add(sidebar, simple_button(u, "Заводской микс", "/api/factory"));
  add(sidebar, simple_button(u, "Сбросить пики", "/api/reset-meters"));
  GtkWidget *right = box(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_add_css_class(right, "mixer-workspace");
  gtk_widget_set_hexpand(right, TRUE);
  add(u->workspace, right);
  GtkWidget *mixbar = box(GTK_ORIENTATION_VERTICAL, 8),
            *context = box(GTK_ORIENTATION_HORIZONTAL, 12),
            *visibility = box(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_add_css_class(mixbar, "mixbar");
  u->link = gtk_check_button_new_with_label("Связать Input 1/2");
  g_signal_connect(u->link, "toggled", G_CALLBACK(link_changed), u);
  add(context, u->link);
  u->mix_name = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(u->mix_name), "Имя микса");
  gtk_widget_set_tooltip_text(u->mix_name, "Имя микса · Enter для применения");
  gtk_widget_set_hexpand(u->mix_name, TRUE);
  gtk_editable_set_width_chars(GTK_EDITABLE(u->mix_name), 12);
  tag(u->mix_name, "source", 6);
  g_signal_connect(u->mix_name, "activate", G_CALLBACK(name_changed), u);
  add(context, u->mix_name);
  add(mixbar, context);
  add(visibility, label("Показывать", "hint"));
  for (int i = 0; i < 3; i++) {
    char s[64];
    snprintf(s, sizeof s, "Virtual %d/%d", 3 + i * 2, 4 + i * 2);
    u->visible[i] = gtk_check_button_new_with_label(s);
    tag(u->visible[i], "index", i);
    g_signal_connect(u->visible[i], "toggled", G_CALLBACK(visible_changed), u);
    add(visibility, u->visible[i]);
  }
  add(mixbar, visibility);
  add(right, mixbar);
  u->pages = gtk_stack_new();
  gtk_widget_set_vexpand(u->pages, TRUE);
  add(right, u->pages);
  GtkWidget *scroller = gtk_scrolled_window_new(),
            *strips = box(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), strips);
  for (int i = 0; i < 7; i++)
    add(strips, make_strip(u, i));
  gtk_stack_add_named(GTK_STACK(u->pages), scroller, "mixer");
  GtkWidget *inputs = box(GTK_ORIENTATION_HORIZONTAL, 0),
            *input_scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(input_scroll), inputs);
  gtk_stack_add_named(GTK_STACK(u->pages), input_scroll, "inputs");
  for (int i = 0; i < 5; i++) {
    GtkWidget *card = box(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(card, "input-card");
    gtk_widget_set_hexpand(card, TRUE);
    gtk_widget_set_size_request(card, 150, -1);
    add(card,
        label(i < 2 ? strip_names[i] : strip_names[i + 1], "strip-title"));
    add(card, label(i < 2 ? "Аппаратный VU" : "Звук приложения", "hint"));
    u->input_meters[i] = meter(u, i, TRUE);
    add(card, u->input_meters[i]);
    u->input_levels[i] = label("−∞ dBFS", "gain");
    add(card, u->input_levels[i]);
    add(inputs, card);
  }
  GtkWidget *footer = box(GTK_ORIENTATION_HORIZONTAL, 14);
  gtk_widget_add_css_class(footer, "footer");
  u->message = label("Подключение к аудиослужбе…", "hint");
  gtk_label_set_ellipsize(GTK_LABEL(u->message), PANGO_ELLIPSIZE_END);
  gtk_widget_set_hexpand(u->message, TRUE);
  gtk_widget_set_halign(u->message, GTK_ALIGN_START);
  add(footer, u->message);
  u->dirty = label("", "hint");
  add(footer, u->dirty);
  u->reconnect = button("Подключиться снова", G_CALLBACK(start_service), u);
  add(footer, u->reconnect);
  add(u->root, footer);
  u->rendering = TRUE;
  u->page = 1;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(u->tabs[1]), TRUE);
  gtk_stack_set_visible_child_name(GTK_STACK(u->pages), "mixer");
  u->rendering = FALSE;
  u->http = soup_session_new();
  soup_session_set_timeout(u->http, 30);
  soup_session_set_proxy_resolver(u->http, NULL);
  u->timer = g_timeout_add(125, tick, u);
  render(u);
  gtk_window_present(GTK_WINDOW(u->window));
  dispatch(u);
}
int main(int argc, char **argv) {
  Ui u = {0};
  u.url = g_strdup("http://127.0.0.1:39713");
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--help")) {
      puts("MiniFuse 2 for Linux — native C/GTK desktop\nUsage: minifuze2 "
           "[--url http://127.0.0.1:PORT] [--ready-file FILE]\nThe audio "
           "service keeps running after the window closes.");
      g_free(u.url);
      return 0;
    }
    if (!strcmp(argv[i], "--version")) {
      puts("0.1.0-preview");
      g_free(u.url);
      return 0;
    }
    if (i + 1 >= argc) {
      g_printerr("Missing option value\n");
      return 2;
    }
    if (!strcmp(argv[i], "--url")) {
      g_free(u.url);
      u.url = g_strdup(argv[++i]);
    } else if (!strcmp(argv[i], "--ready-file"))
      u.ready_file = g_strdup(argv[++i]);
    else {
      g_printerr("Unknown option: %s\n", argv[i]);
      return 2;
    }
  }
  if (!valid_url(u.url)) {
    g_printerr("Only http://127.0.0.1:PORT is supported\n");
    return 2;
  }
  size_t n = strlen(u.url);
  if (u.url[n - 1] == '/')
    u.url[n - 1] = 0;
  GtkApplication *app = gtk_application_new("io.github.minifuze2.Control",
                                            G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(activate), &u);
  char *args[] = {argv[0], NULL};
  int code = g_application_run(G_APPLICATION(app), 1, args);
  if (u.timer)
    g_source_remove(u.timer);
  if (u.debounce)
    g_source_remove(u.debounce);
  clear_jobs(&u);
  if (u.http) {
    soup_session_abort(u.http);
    g_object_unref(u.http);
  }
  if (u.state)
    json_object_put(u.state);
  g_free(u.url);
  g_free(u.ready_file);
  g_free(u.device_key);
  g_clear_object(&u.css);
  g_object_unref(app);
  return code;
}
