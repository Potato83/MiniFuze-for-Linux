/* SPDX-License-Identifier: MIT */
#include "mf.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <openssl/evp.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
_Thread_local char mf_error[1024];
int fail(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(mf_error, sizeof mf_error, fmt, ap);
  va_end(ap);
  return -1;
}
J *get(J *o, const char *key) {
  J *v = NULL;
  if (o)
    json_object_object_get_ex(o, key, &v);
  return v;
}
const char *str(J *o) {
  return o && json_object_is_type(o, json_type_string)
             ? json_object_get_string(o)
             : "";
}
J *copy(J *o) {
  J *v = NULL;
  if (o && json_object_deep_copy(o, &v, NULL))
    fail("Cannot copy JSON");
  return v;
}
void put(J *o, const char *key, J *v) { json_object_object_add(o, key, v); }
bool fields(J *o, const char *keys) {
  if (!o || !json_object_is_type(o, json_type_object))
    return false;
  char *s = strdup(keys), *save = NULL;
  int n = 0;
  bool ok = true;
  if (!s)
    return false;
  for (char *p = strtok_r(s, ",", &save); p; p = strtok_r(NULL, ",", &save)) {
    J *v;
    n++;
    if (!json_object_object_get_ex(o, p, &v))
      ok = false;
  }
  ok = ok && json_object_object_length(o) == n;
  free(s);
  return ok;
}
bool integer(J *o) { return o && json_object_is_type(o, json_type_int); }
bool numeric(J *o, double lo, double hi) {
  return o && (integer(o) || json_object_is_type(o, json_type_double)) &&
         isfinite(json_object_get_double(o)) &&
         json_object_get_double(o) >= lo && json_object_get_double(o) <= hi;
}
double monotonic(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec / 1e9;
}
void pause_ms(int ms) {
  struct timespec t = {ms / 1000, (ms % 1000) * 1000000L};
  while (nanosleep(&t, &t) && errno == EINTR) {
  }
}
char *read_file(const char *path, size_t limit) {
  int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) {
    fail("%s: %s", path, strerror(errno));
    return NULL;
  }
  struct stat st;
  /* sysfs attributes advertise a page-sized st_size even when their actual
   * text is only a few bytes. Enforce the bound on bytes read below. */
  if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0) {
    close(fd);
    fail("Invalid or oversized file: %s", path);
    return NULL;
  }
  char *b = malloc(limit + 1);
  if (!b) {
    close(fd);
    fail("Out of memory");
    return NULL;
  }
  size_t n = 0;
  for (;;) {
    ssize_t z = read(fd, b + n, limit - n + 1);
    if (z < 0 && errno == EINTR)
      continue;
    if (z < 0 || n + (size_t)z > limit) {
      free(b);
      close(fd);
      fail("Cannot read file: %s", path);
      return NULL;
    }
    if (!z)
      break;
    n += (size_t)z;
  }
  close(fd);
  if (memchr(b, 0, n)) {
    free(b);
    fail("Text file contains NUL bytes: %s", path);
    return NULL;
  }
  b[n] = 0;
  return b;
}
J *parse_json(const char *s, size_t n) {
  if (n > INT_MAX) {
    fail("JSON too large");
    return NULL;
  }
  for (size_t i = 0; i < n; i++) {
    if (s[i] != '\\')
      continue;
    size_t start = i;
    while (i < n && s[i] == '\\')
      i++;
    if ((i - start) % 2 && n - i >= 5 && !memcmp(s + i, "u0000", 5)) {
      fail("NUL characters are not supported in JSON text");
      return NULL;
    }
    if (i == n)
      break;
    i--;
  }
  struct json_tokener *t = json_tokener_new();
  json_tokener_set_flags(t, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
  J *v = json_tokener_parse_ex(t, s, (int)n);
  enum json_tokener_error e = json_tokener_get_error(t);
  size_t end = json_tokener_get_parse_end(t);
  while (end < n &&
         (s[end] == ' ' || s[end] == '\r' || s[end] == '\n' || s[end] == '\t'))
    end++;
  if (e != json_tokener_success || end != n) {
    if (v)
      json_object_put(v);
    v = NULL;
    fail("Invalid JSON: %s", json_tokener_error_desc(e));
  }
  json_tokener_free(t);
  return v;
}
J *read_json(const char *path, size_t limit) {
  char *s = read_file(path, limit);
  if (!s)
    return NULL;
  J *j = parse_json(s, strlen(s));
  free(s);
  return j;
}
int atomic_write(const char *path, const char *data, size_t n) {
  char tmp[PATH_MAX];
  if (snprintf(tmp, sizeof tmp, "%s.XXXXXX", path) >= (int)sizeof tmp)
    return fail("File path too long");
  int fd = mkostemp(tmp, O_CLOEXEC);
  if (fd < 0)
    return fail("Cannot create state file: %s", strerror(errno));
  size_t off = 0;
  int rc = 0;
  while (off < n) {
    ssize_t k = write(fd, data + off, n - off);
    if (k < 0 && errno == EINTR)
      continue;
    if (k <= 0) {
      rc = -1;
      break;
    }
    off += (size_t)k;
  }
  if (!rc && fsync(fd))
    rc = -1;
  if (close(fd))
    rc = -1;
  if (!rc && rename(tmp, path))
    rc = -1;
  if (rc) {
    int e = errno;
    unlink(tmp);
    return fail("Cannot save %s: %s", path, strerror(e));
  }
  return 0;
}
int save_json(const char *path, J *v) {
  const char *s = json_object_to_json_string_ext(v, JSON_C_TO_STRING_PRETTY);
  return atomic_write(path, s, strlen(s));
}
char *file_hash(const char *path) {
  if (access(path, F_OK) && errno == ENOENT)
    return strdup("");
  char *s = read_file(path, 131072);
  if (!s)
    return NULL;
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int n = 0;
  if (!EVP_Digest(s, strlen(s), digest, &n, EVP_sha256(), NULL)) {
    free(s);
    fail("SHA256 failure");
    return NULL;
  }
  free(s);
  char *hex = malloc(2 * n + 1);
  if (!hex)
    return NULL;
  for (unsigned i = 0; i < n; i++)
    sprintf(hex + 2 * i, "%02x", digest[i]);
  return hex;
}
bool child_alive(pid_t pid) {
  if (pid <= 0)
    return false;
  siginfo_t info = {0};
  return waitid(P_PID, (id_t)pid, &info, WEXITED | WNOHANG | WNOWAIT) == 0 &&
         info.si_pid == 0;
}
void stop_child(pid_t *pid) {
  if (*pid <= 0)
    return;
  kill(*pid, SIGTERM);
  double end = monotonic() + 3;
  int st;
  while (waitpid(*pid, &st, WNOHANG) == 0 && monotonic() < end)
    pause_ms(10);
  if (waitpid(*pid, &st, WNOHANG) == 0) {
    kill(*pid, SIGKILL);
    while (waitpid(*pid, &st, 0) < 0 && errno == EINTR) {
    }
  }
  *pid = 0;
}
int spawn_child(char *const argv[], int *read_fd, int log_fd, pid_t *pid) {
  int p[2] = {-1, -1};
  if (read_fd && pipe2(p, O_CLOEXEC))
    return fail("pipe: %s", strerror(errno));
  posix_spawn_file_actions_t a;
  posix_spawn_file_actions_init(&a);
  posix_spawn_file_actions_addopen(&a, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
  if (read_fd) {
    posix_spawn_file_actions_adddup2(&a, p[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&a, p[0]);
    posix_spawn_file_actions_addclose(&a, p[1]);
  } else if (log_fd >= 0)
    posix_spawn_file_actions_adddup2(&a, log_fd, STDOUT_FILENO);
  else
    posix_spawn_file_actions_addopen(&a, STDOUT_FILENO, "/dev/null", O_WRONLY,
                                     0);
  if (log_fd >= 0)
    posix_spawn_file_actions_adddup2(&a, log_fd, STDERR_FILENO);
  else
    posix_spawn_file_actions_addopen(&a, STDERR_FILENO, "/dev/null", O_WRONLY,
                                     0);
  /* The C executable arms parent-death notification before exec. This also
   * removes owned audio children if the foreground panel is killed outright. */
  size_t count = 0;
  while (argv[count])
    count++;
  char parent[32];
  snprintf(parent, sizeof parent, "%ld", (long)getpid());
  char **wrapped = calloc(count + 4, sizeof *wrapped);
  if (!wrapped) {
    posix_spawn_file_actions_destroy(&a);
    if (read_fd) {
      close(p[0]);
      close(p[1]);
    }
    return fail("Out of memory");
  }
  wrapped[0] = "/proc/self/exe";
  wrapped[1] = "--child-exec";
  wrapped[2] = parent;
  for (size_t i = 0; i < count; i++)
    wrapped[i + 3] = argv[i];
  int e = posix_spawn(pid, wrapped[0], &a, NULL, wrapped, environ);
  free(wrapped);
  posix_spawn_file_actions_destroy(&a);
  if (read_fd) {
    close(p[1]);
    if (e)
      close(p[0]);
    else
      *read_fd = p[0];
  }
  if (e) {
    *pid = 0;
    return fail("Cannot start %s: %s", argv[0], strerror(e));
  }
  return 0;
}
char *run(char *const argv[], int timeout_ms) {
  pid_t pid = 0;
  int fd;
  if (spawn_child(argv, &fd, -1, &pid))
    return NULL;
  size_t cap = 8192, n = 0;
  char *s = malloc(cap);
  double end = monotonic() + timeout_ms / 1000.0;
  int rc = 0;
  if (!s) {
    close(fd);
    stop_child(&pid);
    fail("Out of memory");
    return NULL;
  }
  for (;;) {
    int remaining = (int)((end - monotonic()) * 1000);
    if (remaining <= 0) {
      rc = fail("%s timed out", argv[0]);
      break;
    }
    struct pollfd p = {fd, POLLIN, 0};
    int ready = poll(&p, 1, remaining);
    if (ready < 0 && errno == EINTR)
      continue;
    if (ready <= 0) {
      rc = fail("%s timed out", argv[0]);
      break;
    }
    if (n + 4097 > cap) {
      if (cap >= 16 * 1024 * 1024) {
        rc = fail("%s output too large", argv[0]);
        break;
      }
      cap *= 2;
      char *more = realloc(s, cap);
      if (!more) {
        rc = fail("Out of memory");
        break;
      }
      s = more;
    }
    ssize_t k = read(fd, s + n, cap - n - 1);
    if (k < 0 && errno == EINTR)
      continue;
    if (k < 0) {
      rc = fail("%s read error", argv[0]);
      break;
    }
    if (!k)
      break;
    n += (size_t)k;
  }
  close(fd);
  if (rc) {
    free(s);
    stop_child(&pid);
    return NULL;
  }
  int st = 0;
  while (waitpid(pid, &st, WNOHANG) == 0) {
    if (monotonic() >= end) {
      free(s);
      stop_child(&pid);
      fail("%s timed out", argv[0]);
      return NULL;
    }
    pause_ms(5);
  }
  if (!WIFEXITED(st) || WEXITSTATUS(st)) {
    free(s);
    fail("%s failed (status %d)", argv[0], st);
    return NULL;
  }
  s[n] = 0;
  return s;
}
J *pw_snapshot(void) {
  char *argv[] = {"pw-dump", NULL};
  char *s = run(argv, 10000);
  if (!s)
    return NULL;
  J *v = parse_json(s, strlen(s));
  free(s);
  return v;
}
