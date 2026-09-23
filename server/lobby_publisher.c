#include "lobby_publisher.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum { LOBBY_JSON_MAX = 1024, LOBBY_RETRY_MIN_MS = 1000, LOBBY_RETRY_MAX_MS = 30000 };

static uint64_t monotonic_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static void sleep_ms(unsigned ms) {
  struct timespec delay = {.tv_sec = ms / 1000u,
                           .tv_nsec = (long)(ms % 1000u) * 1000000L};
  while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
  }
}

static int json_append(char *out, size_t out_len, size_t *used,
                       const char *text) {
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    const char *escaped = NULL;
    char unicode[7];
    if (*p == '"') escaped = "\\\"";
    else if (*p == '\\') escaped = "\\\\";
    else if (*p == '\b') escaped = "\\b";
    else if (*p == '\f') escaped = "\\f";
    else if (*p == '\n') escaped = "\\n";
    else if (*p == '\r') escaped = "\\r";
    else if (*p == '\t') escaped = "\\t";
    else if (*p < 0x20) {
      snprintf(unicode, sizeof(unicode), "\\u%04x", (unsigned)*p);
      escaped = unicode;
    }
    if (escaped != NULL) {
      size_t n = strlen(escaped);
      if (*used + n >= out_len) return -1;
      memcpy(out + *used, escaped, n);
      *used += n;
    } else {
      if (*used + 1 >= out_len) return -1;
      out[(*used)++] = (char)*p;
    }
  }
  out[*used] = '\0';
  return 0;
}

static int json_field(char *out, size_t out_len, size_t *used,
                      const char *prefix, const char *value) {
  size_t n = strlen(prefix);
  if (*used + n >= out_len) return -1;
  memcpy(out + *used, prefix, n);
  *used += n;
  return json_append(out, out_len, used, value);
}

static int make_request(const struct lobby_publisher *publisher, int room_index,
                        int curplayers, int offline, char *url,
                        size_t url_len, char *json, size_t json_len) {
  const struct lobby_config *config = &publisher->config;
  const struct lobby_room_state *room = &publisher->rooms[room_index];
  const char *slash = config->base_url[strlen(config->base_url) - 1] == '/' ?
                          "server" : "/server";
  int written = snprintf(url, url_len, "%s%s", config->base_url, slash);
  if (written < 0 || (size_t)written >= url_len) return -1;

  char serverurl[LOBBY_HOST_MAX + 24];
  written = snprintf(serverurl, sizeof(serverurl), "tcp://%s:%d",
                     config->public_host, room->port);
  if (written < 0 || (size_t)written >= sizeof(serverurl)) return -1;

  size_t used = 0;
  if (json_field(json, json_len, &used, "{\"game\":\"", config->game) ||
      json_field(json, json_len, &used, "\",\"appkey\":", "") != 0) {
    return -1;
  }
  written = snprintf(json + used, json_len - used,
                     "%u,\"server\":\"", config->app_id);
  if (written < 0 || (size_t)written >= json_len - used) return -1;
  used += (size_t)written;
  if (json_append(json, json_len, &used, config->room_names[room_index]) ||
      json_field(json, json_len, &used, "\",\"region\":\"", config->region) ||
      json_field(json, json_len, &used, "\",\"serverurl\":\"", serverurl) ||
      json_field(json, json_len, &used, "\",\"status\":\"",
                 offline ? "offline" : "online")) {
    return -1;
  }
  written = snprintf(json + used, json_len - used,
                     "\",\"maxplayers\":4,\"curplayers\":%d,\"clients\":[{\"platform\":\"atari\",\"url\":\"",
                     offline ? 0 : curplayers);
  if (written < 0 || (size_t)written >= json_len - used) return -1;
  used += (size_t)written;
  if (json_append(json, json_len, &used, config->client_url) != 0) return -1;
  if (used + strlen("\"}]}" ) >= json_len) return -1;
  memcpy(json + used, "\"}]}", 5);
  return 0;
}

/* curl is deliberately a child of the publisher worker, never of the game
   loop. It provides HTTPS without adding a library dependency to this small C
   server. The parent owns a monotonic deadline and kills a wedged child. */
static int post_json(const char *url, const char *json, int timeout_ms) {
  char timeout_text[16];
  snprintf(timeout_text, sizeof(timeout_text), "%d", timeout_ms / 1000 + 1);
  int output_pipe[2];
  if (pipe(output_pipe) != 0) return -1;
  pid_t child = fork();
  if (child < 0) {
    close(output_pipe[0]);
    close(output_pipe[1]);
    return -1;
  }
  if (child == 0) {
    close(output_pipe[0]);
    if (dup2(output_pipe[1], STDOUT_FILENO) < 0) _exit(127);
    close(output_pipe[1]);
    execlp("curl", "curl", "--silent", "--show-error", "--fail",
           "--max-time", timeout_text, "--connect-timeout", timeout_text,
           "--request", "POST", "--header", "Content-Type: application/json",
           "--data-binary", json, "--output", "/dev/null", "--write-out",
           "%{http_code}", url,
           (char *)NULL);
    _exit(127);
  }
  close(output_pipe[1]);
  uint64_t deadline = monotonic_ms() + (uint64_t)timeout_ms;
  int status = 0;
  for (;;) {
    pid_t result = waitpid(child, &status, WNOHANG);
    if (result == child) {
      char http_status[8] = {0};
      ssize_t received = read(output_pipe[0], http_status, sizeof(http_status) - 1);
      close(output_pipe[0]);
      return WIFEXITED(status) && WEXITSTATUS(status) == 0 && received == 3 &&
                     memcmp(http_status, "201", 3) == 0
                 ? 0
                 : -1;
    }
    if (result < 0 && errno != EINTR) {
      close(output_pipe[0]);
      return -1;
    }
    if (monotonic_ms() >= deadline) {
      kill(child, SIGTERM);
      (void)waitpid(child, &status, 0);
      close(output_pipe[0]);
      return -1;
    }
    sleep_ms(10);
  }
}

static int pick_online_request(struct lobby_publisher *publisher,
                               uint64_t now, int *room_index) {
  for (int i = 0; i < publisher->room_count; i++) {
    if (publisher->rooms[i].dirty || now >= publisher->rooms[i].next_attempt_ms) {
      publisher->rooms[i].dirty = 0;
      *room_index = i;
      return 1;
    }
  }
  return 0;
}

static void *publisher_thread(void *opaque) {
  struct lobby_publisher *publisher = opaque;
  for (;;) {
    int room_index = -1;
    int offline = 0;
    int timeout_ms = 0;
    pthread_mutex_lock(&publisher->mutex);
    uint64_t now = monotonic_ms();
    if (publisher->stopping) {
      if (now >= publisher->shutdown_deadline_ms) {
        pthread_mutex_unlock(&publisher->mutex);
        break;
      }
      for (int i = 0; i < publisher->room_count; i++) {
        if (!publisher->rooms[i].offline_sent) {
          publisher->rooms[i].offline_sent = 1;
          room_index = i;
          offline = 1;
          uint64_t left = publisher->shutdown_deadline_ms - now;
          timeout_ms = (int)(left < (uint64_t)publisher->config.timeout_ms ?
                                 left : (uint64_t)publisher->config.timeout_ms);
          if (timeout_ms < 1) timeout_ms = 1;
          break;
        }
      }
      if (room_index < 0) {
        pthread_mutex_unlock(&publisher->mutex);
        break;
      }
    } else if (!pick_online_request(publisher, now, &room_index)) {
      struct timespec wait_until;
      clock_gettime(CLOCK_REALTIME, &wait_until);
      wait_until.tv_nsec += 100000000L;
      if (wait_until.tv_nsec >= 1000000000L) {
        wait_until.tv_sec++;
        wait_until.tv_nsec -= 1000000000L;
      }
      (void)pthread_cond_timedwait(&publisher->wake, &publisher->mutex,
                                   &wait_until);
      pthread_mutex_unlock(&publisher->mutex);
      continue;
    } else {
      timeout_ms = publisher->config.timeout_ms;
    }

    char url[LOBBY_URL_MAX + 16];
    char json[LOBBY_JSON_MAX];
    int curplayers = publisher->rooms[room_index].curplayers;
    int port = publisher->rooms[room_index].port;
    int built = make_request(publisher, room_index, curplayers, offline, url,
                             sizeof(url), json, sizeof(json));
    pthread_mutex_unlock(&publisher->mutex);

    int result = built == 0 ? post_json(url, json, timeout_ms) : -1;
    now = monotonic_ms();
    pthread_mutex_lock(&publisher->mutex);
    if (!offline) {
      struct lobby_room_state *room = &publisher->rooms[room_index];
      if (result == 0) {
        room->retry_ms = LOBBY_RETRY_MIN_MS;
        room->next_attempt_ms = now + (uint64_t)publisher->config.refresh_ms;
        printf("lobby room=%d online curplayers=%d\n", port, curplayers);
      } else {
        unsigned retry = room->retry_ms ? room->retry_ms : LOBBY_RETRY_MIN_MS;
        room->next_attempt_ms = now + retry;
        room->retry_ms = retry < LOBBY_RETRY_MAX_MS / 2 ? retry * 2 : LOBBY_RETRY_MAX_MS;
        printf("lobby room=%d publish failed; retry_ms=%u\n", port, retry);
      }
    } else {
      printf("lobby room=%d offline %s\n", port,
             result == 0 ? "published" : "not-published");
    }
    pthread_mutex_unlock(&publisher->mutex);
  }
  return NULL;
}

int lobby_publisher_start(struct lobby_publisher *publisher,
                          const struct lobby_config *config, int room_count,
                          const int *ports) {
  if (!publisher || !config || !ports || room_count < 1 ||
      room_count > LOBBY_MAX_ROOMS) return -1;
  memset(publisher, 0, sizeof(*publisher));
  publisher->config = *config;
  publisher->room_count = room_count;
  for (int i = 0; i < room_count; i++) {
    publisher->rooms[i].port = ports[i];
    publisher->rooms[i].dirty = 1;
    publisher->rooms[i].retry_ms = LOBBY_RETRY_MIN_MS;
  }
  if (pthread_mutex_init(&publisher->mutex, NULL) != 0 ||
      pthread_cond_init(&publisher->wake, NULL) != 0) return -1;
  if (pthread_create(&publisher->thread, NULL, publisher_thread, publisher) != 0) {
    pthread_cond_destroy(&publisher->wake);
    pthread_mutex_destroy(&publisher->mutex);
    return -1;
  }
  publisher->started = 1;
  return 0;
}

void lobby_publisher_submit(struct lobby_publisher *publisher, int room_index,
                            int curplayers) {
  if (!publisher || !publisher->started || room_index < 0 ||
      room_index >= publisher->room_count) return;
  if (curplayers < 0) curplayers = 0;
  if (curplayers > 4) curplayers = 4;
  pthread_mutex_lock(&publisher->mutex);
  if (!publisher->stopping &&
      publisher->rooms[room_index].curplayers != curplayers) {
    publisher->rooms[room_index].curplayers = curplayers;
    publisher->rooms[room_index].dirty = 1;
    pthread_cond_signal(&publisher->wake);
  }
  pthread_mutex_unlock(&publisher->mutex);
}

void lobby_publisher_stop(struct lobby_publisher *publisher) {
  if (!publisher || !publisher->started) return;
  pthread_mutex_lock(&publisher->mutex);
  publisher->stopping = 1;
  publisher->shutdown_deadline_ms =
      monotonic_ms() + (uint64_t)publisher->config.shutdown_ms;
  pthread_cond_signal(&publisher->wake);
  pthread_mutex_unlock(&publisher->mutex);
  (void)pthread_join(publisher->thread, NULL);
  pthread_cond_destroy(&publisher->wake);
  pthread_mutex_destroy(&publisher->mutex);
  publisher->started = 0;
}
