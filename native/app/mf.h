/* SPDX-License-Identifier: MIT */
#ifndef MF_APP_H
#define MF_APP_H
#include <json-c/json.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct json_object J;
extern _Thread_local char mf_error[1024];
int fail(const char *fmt, ...);
int full_path(char *dest, const char *path);
J *get(J *o, const char *key);
const char *str(J *o);
J *copy(J *o);
void put(J *o, const char *key, J *value);
bool fields(J *o, const char *keys);
bool integer(J *o);
bool numeric(J *o, double low, double high);
char *read_file(const char *path, size_t limit);
int atomic_write(const char *path, const char *data, size_t length);
J *read_json(const char *path, size_t limit);
J *parse_json(const char *text, size_t length);
int save_json(const char *path, J *value);
char *file_hash(const char *path);
double monotonic(void);
void pause_ms(int ms);
char *run(char *const argv[], int timeout_ms);
J *pw_snapshot(void);
int spawn_child(char *const argv[], int *read_fd, int log_fd, pid_t *pid);
void stop_child(pid_t *pid);
bool child_alive(pid_t pid);

J *factory_state(void);
int matrix(J *state, float weights[6][10]);
float normalized_db(float value);
float db_normalized(float value);
J *parse_preset(const char *text);
char *preset_text(J *preset);
J *import_mixer(J *preset);
J *export_mixer(J *state, J *preset);
J *preset_controls(J *preset);
J *preset_refs(J *preset);
int linked_valid(J *state, J *preset);
J *linked_edit(J *state, J *previous, J *preset);
int change_link(J **state, J **preset, int mix, bool enabled);
int change_virtual(J **state, J **preset, int mix, int index, bool visible);
int change_ref(J *preset, int mix, int source, float normalized);
int rename_field(J *preset, const char *kind, int index, const char *name);
J *default_refs(void);
int validate_refs(J *refs);
float ref_normalized(int db);
bool ref_db(float value, int *db);
J *document(J *state, J *preset, J *refs);
int unpack_document(J *doc, J **state, J **preset, J **refs);

typedef struct {
  char prefix[101], folder[128], log_path[PATH_MAX];
  pid_t pid;
  int lock_fd, log_fd;
  J *state, *hardware, *nodes, *serials, *links;
} Graph;
J *graph_config(J *state, const char *prefix);
int graph_start(Graph *g, J *state, const char *prefix);
void graph_close(Graph *g);
int graph_apply(Graph *g, J *state);
J *hardware_devices(J *objects);
int graph_connect(Graph *g, const char *port);
int graph_disconnect(Graph *g);
bool graph_links_alive(Graph *g, J *objects);

J *device_status(const char *endpoint);
J *device_meters(const char *endpoint, const char *port);
int device_set(const char *endpoint, int selector, bool enabled);

typedef struct {
  double bar[16], peak[16], targets[16], clip_at[16], tick_at;
  int counter[16], channels;
  bool clipped[16];
  J *preferences;
} Ballistics;
float meter_position(float amplitude);
double position_amplitude(double position);
J *default_preferences(void);
int validate_preferences(J *p);
void ballistics_reset(Ballistics *b);
void ballistics_configure(Ballistics *b, J *prefs, int channels);
void ballistics_advance(Ballistics *b, double now);
void ballistics_update(Ballistics *b, const float *levels, double now);
J *ballistics_view(Ballistics *b, bool active);

typedef struct {
  char path[PATH_MAX];
  J *preferences, *status, *owned;
  char identity[1024];
} Audio;
void audio_init(Audio *a, const char *path);
void audio_close(Audio *a);
int audio_refresh(Audio *a, J *hardware);
int audio_apply(Audio *a, J *hardware, J *request);
int audio_reset(Audio *a);
int audio_release(Audio *a);

typedef struct {
  Graph graph;
  Audio audio;
  pthread_mutex_t lock;
  pthread_t poller, meter_thread;
  bool poller_started, meter_started, stopping, persist_ready;
  pid_t meter_pid;
  int meter_fd;
  char state_path[PATH_MAX], session_path[PATH_MAX], ctl_path[PATH_MAX],
      meter_path[PATH_MAX];
  char route_uid[513], route_port[128], csrf[65], web_dir[PATH_MAX];
  int http_port;
  J *saved, *preset, *saved_preset, *refs, *saved_refs, *devices, *device;
  J *route_error, *session_error, *hardware_levels;
  Ballistics meters, hardware_meters;
  float levels[16];
  double updated, hardware_updated;
  bool exceeded[3][7];
} App;
int app_init(App *a, const char *state_path, const char *session_path,
             const char *prefix, const char *uid, const char *port,
             const char *ctl, const char *web);
void app_close(App *a);
J *app_view(App *a);
J *app_change(App *a, const char *path, J *body);
int app_persist(App *a);
int panel_serve(App *a, int port, const char *ready_path);
int main_cli(int argc, char **argv);
#endif
