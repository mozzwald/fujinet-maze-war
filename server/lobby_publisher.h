#ifndef MAZE_WAR_LOBBY_PUBLISHER_H
#define MAZE_WAR_LOBBY_PUBLISHER_H

#include <pthread.h>
#include <stdint.h>

enum {
  LOBBY_MAX_ROOMS = 64,
  LOBBY_URL_MAX = 256,
  LOBBY_HOST_MAX = 63,
  LOBBY_GAME_MAX = 16,
  LOBBY_ROOM_NAME_MAX = 32,
  LOBBY_CLIENT_URL_MAX = 64,
  LOBBY_REGION_MAX = 2
};

struct lobby_config {
  int enabled;
  unsigned creator_id;
  unsigned app_id;
  int refresh_ms;
  int timeout_ms;
  int shutdown_ms;
  char base_url[LOBBY_URL_MAX + 1];
  char public_host[LOBBY_HOST_MAX + 1];
  char game[LOBBY_GAME_MAX + 1];
  char client_url[LOBBY_CLIENT_URL_MAX + 1];
  char region[LOBBY_REGION_MAX + 1];
  char room_names[LOBBY_MAX_ROOMS][LOBBY_ROOM_NAME_MAX + 1];
};

struct lobby_room_state {
  int port;
  int curplayers;
  int dirty;
  int offline_sent;
  uint64_t next_attempt_ms;
  unsigned retry_ms;
};

struct lobby_publisher {
  pthread_t thread;
  pthread_mutex_t mutex;
  pthread_cond_t wake;
  struct lobby_config config;
  struct lobby_room_state rooms[LOBBY_MAX_ROOMS];
  int room_count;
  int started;
  int stopping;
  uint64_t shutdown_deadline_ms;
};

int lobby_publisher_start(struct lobby_publisher *publisher,
                          const struct lobby_config *config, int room_count,
                          const int *ports);
void lobby_publisher_submit(struct lobby_publisher *publisher, int room_index,
                            int curplayers);
void lobby_publisher_stop(struct lobby_publisher *publisher);

#endif
