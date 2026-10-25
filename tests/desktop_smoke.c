/* SPDX-License-Identifier: MIT */
/* Exercise real GTK signals and the real HTTP client against a private engine.
 * The guard below prevents this test from editing a user's physical graph. */
#define main desktop_program_main
#include "../native/desktop/main.c"
#undef main

static Ui test_ui;
static char *report_path, *snapshot_path, *preset_path;
static int stage, ticks;
static gboolean captured;

static gboolean save_snapshot(GtkWidget *widget, GtkWidget *window,
                              const char *path) {
  GdkPaintable *p = gtk_widget_paintable_new(widget);
  GtkSnapshot *s = gtk_snapshot_new();
  int w = gtk_widget_get_width(widget), h = gtk_widget_get_height(widget);
  gdk_paintable_snapshot(p, GDK_SNAPSHOT(s), w, h);
  GskRenderNode *node = gtk_snapshot_free_to_node(s);
  if (!node) {
    g_object_unref(p);
    return FALSE;
  }
  graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, w, h);
  GdkTexture *texture = gsk_renderer_render_texture(
      gtk_native_get_renderer(GTK_NATIVE(window)), node, &bounds);
  g_assert_nonnull(texture);
  g_assert_true(gdk_texture_save_to_png(texture, path));
  g_object_unref(texture);
  gsk_render_node_unref(node);
  g_object_unref(p);
  return TRUE;
}
static gboolean snapshot(Ui *u) {
  captured = save_snapshot(u->root, u->window, snapshot_path);
  return captured;
}
static J *mix(int index) {
  return idx(get(get(test_ui.state, "mixer"), "mixes"), index);
}
static J *strip(int m, int i) { return idx(get(mix(m), "strips"), i); }
static gboolean drive(gpointer unused) {
  (void)unused;
  Ui *u = &test_ui;
  if (++ticks > 600)
    g_error("Desktop integration test timed out at stage %d", stage);
  if ((!u->connected && stage < 16) || u->busy || u->debounce || !g_queue_is_empty(&u->jobs) ||
      ticks < 8)
    return G_SOURCE_CONTINUE;
  g_assert_null(get(u->state, "hardware"));
  g_assert_true(g_str_has_prefix(
      str(get(idx(get(u->state, "playback"), 0), "name")), "mf_desktop_test_"));
  switch (stage++) {
  case 0:
    if (!snapshot(u)) {
      stage--;
      return G_SOURCE_CONTINUE;
    }
    {
      char *path = g_strconcat(snapshot_path, ".window.png", NULL);
      g_assert_true(save_snapshot(u->window, u->window, path));
      g_free(path);
    }
    gtk_range_set_value(GTK_RANGE(u->gain[2]), -9.5);
    J *draft_mix = idx(get(u->draft, "mixes"), u->mix);
    J *state_mix = idx(get(get(u->state, "mixer"), "mixes"), 0);
    g_printerr("[DEBUG-mfci] fader set: mix=%d widget=%.2f draft=%.2f state=%.2f\n",
               u->mix, gtk_range_get_value(GTK_RANGE(u->gain[2])),
               val(get(idx(get(draft_mix, "strips"), 2), "level_db")),
               val(get(idx(get(state_mix, "strips"), 2), "level_db")));
    break;
  case 1:
    g_printerr("[DEBUG-mfci] fader response: mix=%d revision=%u sent=%u busy=%d debounce=%u draft=%p value=%.2f message=%s\n",
               u->mix, u->revision, u->sent_revision, u->busy, u->debounce,
               (void *)u->draft,
               val(get(strip(0, 2), "level_db")),
               gtk_label_get_text(GTK_LABEL(u->message)));
    g_assert_cmpfloat(val(get(strip(0, 2), "level_db")), ==, -9.5);
    gtk_range_set_value(GTK_RANGE(u->pan[2][0]), .25);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(u->mute[2]), TRUE);
    break;
  case 2:
    g_assert_cmpfloat(val(idx(get(strip(0, 2), "pans"), 0)), ==, .25);
    g_assert_true(yes(get(strip(0, 2), "mute")));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(u->tabs[2]), TRUE);
    gtk_range_set_value(GTK_RANGE(u->gain[2]), -6);
    break;
  case 3:
    g_assert_cmpfloat(val(get(strip(0, 2), "level_db")), ==, -9.5);
    g_assert_cmpfloat(val(get(strip(1, 2), "level_db")), ==, -6);
    {
      char *s = NULL;
      gsize n = 0;
      g_assert_true(g_file_get_contents(preset_path, &s, &n, NULL));
      J *j = body();
      text(j, "text", s);
      command(u, "/api/import-mfp", j);
      g_free(s);
    }
    break;
  case 4:
    g_assert_true(yes(get(get(u->state, "preset"), "has_template")));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(u->link), TRUE);
    break;
  case 5:
    g_assert_true(yes(idx(get(get(u->state, "preset"), "links"), 1)));
    gtk_editable_set_text(GTK_EDITABLE(u->names[0]), "Тест гитары");
    g_signal_emit_by_name(u->names[0], "activate");
    gtk_drop_down_set_selected(GTK_DROP_DOWN(u->refs[0]), 2);
    break;
  case 6:
    g_assert_cmpstr(str(idx(get(get(u->state, "preset"), "input_names"), 0)),
                    ==, "Тест гитары");
    g_assert_cmpint(json_object_get_int(
                        idx(idx(get(get(u->state, "peakref"), "db"), 1), 0)),
                    ==, -12);
    g_assert_cmpint(json_object_get_int(
                        idx(idx(get(get(u->state, "peakref"), "db"), 1), 1)),
                    ==, -12);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(u->visible[2]), FALSE);
    break;
  case 7:
    g_assert_false(
        yes(idx(idx(get(get(u->state, "preset"), "virtual_visible"), 1), 2)));
    settings(NULL, u);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(u->orange), 3);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(u->clip), 1);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(u->peak), 2);
    apply_meters(NULL, u);
    break;
  case 8: {
    J *p = get(get(u->state, "meters"), "preferences");
    g_assert_cmpint(json_object_get_int(get(p, "orange_db")), ==, -9);
    g_assert_cmpstr(str(get(p, "clip_reset")), ==, "1.5s");
    g_assert_cmpstr(str(get(p, "peak_reset")), ==, "1s");
    char *path = g_strconcat(snapshot_path, ".settings.png", NULL);
    gboolean saved = save_snapshot(gtk_window_get_child(GTK_WINDOW(u->prefs)),
                                   u->prefs, path);
    g_free(path);
    if (!saved) {
      stage--;
      return G_SOURCE_CONTINUE;
    }
  }
    gtk_window_destroy(GTK_WINDOW(u->prefs));
    g_signal_emit_by_name(u->save, "clicked");
    break;
  case 9:
    g_assert_false(yes(get(u->state, "dirty")));
    gtk_range_set_value(GTK_RANGE(u->gain[2]), -17);
    break;
  case 10:
    g_assert_true(yes(get(u->state, "dirty")));
    g_signal_emit_by_name(u->load, "clicked");
    break;
  case 11:
    g_assert_false(yes(get(u->state, "dirty")));
    {
      char *file = g_strconcat(report_path, ".mfp", NULL);
      GFile *f = g_file_new_for_path(file);
      queue(u, "/api/export-mfp", body(), f, 0);
      g_object_unref(f);
      g_free(file);
      dispatch(u);
    }
    break;
  case 12: {
    char *file = g_strconcat(report_path, ".mfp", NULL);
    if (!g_file_test(file, G_FILE_TEST_EXISTS)) {
      stage--;
      g_free(file);
      return G_SOURCE_CONTINUE;
    }
    g_free(file);
  }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(u->tabs[0]), TRUE);
    break;
  case 13:
    g_assert_cmpstr(gtk_stack_get_visible_child_name(GTK_STACK(u->pages)), ==,
                    "inputs");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(u->tabs[1]), TRUE);
    for (int i = 0; i < 3; i++)
      gtk_check_button_set_active(GTK_CHECK_BUTTON(u->visible[i]), TRUE);
    settings(NULL, u);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(u->theme), 1);
    g_assert_true(u->dark);
    {
      Ui restored = {0};
      load_theme(&restored);
      g_assert_true(restored.dark);
    }
    break;
  case 14: {
    char *path = g_strconcat(snapshot_path, ".dark.png", NULL);
    gboolean ready = save_snapshot(u->window, u->window, path);
    g_free(path);
    if (!ready) {
      stage--;
      return G_SOURCE_CONTINUE;
    }
    path = g_strconcat(snapshot_path, ".dark-settings.png", NULL);
    ready = save_snapshot(u->prefs, u->prefs, path);
    g_free(path);
    if (!ready) {
      stage--;
      return G_SOURCE_CONTINUE;
    }
    gtk_window_destroy(GTK_WINDOW(u->prefs));
    gtk_window_set_default_size(GTK_WINDOW(u->window), 1024, 720);
    break;
  }
  case 15: {
    char *path = g_strconcat(snapshot_path, ".small.png", NULL);
    g_assert_cmpint(gtk_widget_get_width(u->window), <=, 1024);
    gboolean ready = save_snapshot(u->window, u->window, path);
    g_free(path);
    if (!ready) {
      stage--;
      return G_SOURCE_CONTINUE;
    }
    settings(NULL, u);
    g_source_remove(u->timer);
    u->timer = 0;
    u->connected = FALSE;
    render(u);
    g_assert_false(gtk_widget_get_sensitive(u->workspace));
    g_assert_false(gtk_widget_get_sensitive(u->audio_apply));
    g_assert_false(gtk_widget_get_sensitive(u->audio_reset));
    g_assert_false(gtk_widget_get_sensitive(u->meters_apply));
    break;
  }
  default: {
    char *path = g_strconcat(snapshot_path, ".offline.png", NULL);
    gboolean ready = save_snapshot(u->window, u->window, path);
    g_free(path);
    if (!ready) {
      stage--;
      return G_SOURCE_CONTINUE;
    }
    path = g_strconcat(snapshot_path, ".offline-settings.png", NULL);
    ready = save_snapshot(u->prefs, u->prefs, path);
    g_free(path);
    if (!ready) {
      stage--;
      return G_SOURCE_CONTINUE;
    }
    gtk_drop_down_set_selected(GTK_DROP_DOWN(u->theme), 0);
    g_assert_false(u->dark);
    {
      Ui restored = {0};
      load_theme(&restored);
      g_assert_false(restored.dark);
    }
    g_assert_true(captured);
    g_assert_true(
        g_file_set_contents(report_path,
                            "{\"ok\":true,\"stages\":17,\"gtk_signals\":true,"
                            "\"theme_switch_and_persistence\":true,"
                            "\"small_window\":true,\"offline_states\":true,"
                            "\"no_physical_routes\":true}\n",
                            -1, NULL));
    gtk_window_close(GTK_WINDOW(u->window));
    return G_SOURCE_REMOVE;
  }
  }
  return G_SOURCE_CONTINUE;
}
int main(int argc, char **argv) {
  if (argc != 5) {
    g_printerr("Usage: desktop-smoke URL REPORT PNG PRESET\n");
    return 2;
  }
  g_assert_true(valid_url(argv[1]));
  test_ui.url = g_strdup(argv[1]);
  report_path = argv[2];
  snapshot_path = argv[3];
  preset_path = argv[4];
  GtkApplication *app =
      gtk_application_new("io.github.minifuze2.Test", G_APPLICATION_NON_UNIQUE);
  g_signal_connect(app, "activate", G_CALLBACK(activate), &test_ui);
  g_timeout_add(120, drive, NULL);
  char *args[] = {argv[0], NULL};
  int rc = g_application_run(G_APPLICATION(app), 1, args);
  if (test_ui.timer)
    g_source_remove(test_ui.timer);
  clear_jobs(&test_ui);
  if (test_ui.http) {
    soup_session_abort(test_ui.http);
    g_object_unref(test_ui.http);
  }
  if (test_ui.state)
    json_object_put(test_ui.state);
  g_free(test_ui.url);
  g_free(test_ui.device_key);
  g_clear_object(&test_ui.css);
  g_object_unref(app);
  return rc;
}
