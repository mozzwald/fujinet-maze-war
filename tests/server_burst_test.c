/* Verify production packet scheduling and legacy repair with a private peer. */
#define main maze_server_main
#include "../server/main.c"
#undef main
#include <assert.h>

static struct room room;
static struct tcp_rx rx;
static int peer;
static unsigned repair_count;
static unsigned packets(uint8_t type) {
  uint8_t payload[64];
  unsigned count = 0;
  repair_count = 0;
  int n;
  while ((n = tcp_recv_frame(peer, &rx, payload, sizeof(payload))) > 0) {
    if (payload[0] == type) count++;
    if (payload[0] == PKT_RESPAWN || payload[0] == PKT_BRICK_DELTA) repair_count++;
  }
  assert(n == 0);
  return count;
}
int main(void) {
  int sockets[2];
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  peer = sockets[1];
  assert(fcntl(peer, F_SETFL, O_NONBLOCK) == 0);
  struct client_slot *client = &room.clients[0];
  client->fd = sockets[0]; client->in_use = client->handshake_ok = 1;
  room.round_id = 1; room.round_state = ROUND_PLAYING; room.tick_ms = 100;
  room.occupied_mask = 1; room.config.kill_limit = 10;
  room.players[0].x = room.players[0].y = 5;
  uint8_t x[4] = {5}, y[4] = {5};
  /* Border impact: one immediate clear, then two on later ticks. Dropping
     the initial clear still leaves the same two future repair opportunities. */
  room.shots[0].active = 1; room.shots[0].x = 19; room.shots[0].y = 5;
  room.shots[0].dx = 1;
  step_shots(&room, 0, x, y); assert(packets(PKT_SHOT) == 1);
  assert(room.shots[0].clear_burst == 2);
  step_shots(&room, 0, x, y); assert(packets(PKT_SHOT) == 1);
  step_shots(&room, 0, x, y); assert(packets(PKT_SHOT) == 1);
  step_shots(&room, 0, x, y); assert(packets(PKT_SHOT) == 0);
  /* A slot handoff has no initial clear; retain all three deferred sends. */
  room.shots[0].active = 1; reset_slot_gameplay(&room, 0, 1000);
  for (int i = 0; i < 3; ++i) {
    step_shots(&room, 0, x, y); assert(packets(PKT_SHOT) == 1);
  }
  /* Periodic repairs do not consume a reliable revision or require an ACK. */
  for (int i = 0; i < 4; ++i) {
    broadcast_next_name(&room, 0); assert(packets(PKT_NAME) == 1);
  }
  assert(client->reliable_count == 0);
  uint8_t name[] = {PKT_NAME, 1, 0, 'T','E','S','T',' ',' ',' ',' '};
  handle_client_packet(&room, 0, name, sizeof(name), 0, 1000);
  assert(client->reliable_count == 1); /* real change stays reliable */
  packets(PKT_NAME);
  uint8_t spawn[RESPAWN_LEN];
  build_respawn(0, 0, 5, 5, 3, 1, spawn, sizeof(spawn));
  queue_respawn_echo(&room, spawn); queue_brick_echo(&room, 10, 10);
  /* Each class keeps its two echoes, spread over four ticks, one per tick. */
  for (int i = 0; i < 4; ++i) {
    tick_room(&room, 0, 1100 + i*100);
    assert(packets(i & 1 ? PKT_RESPAWN : PKT_BRICK_DELTA) == 1);
    assert(repair_count == 1);
  }
  assert(!room.respawn_echo[0].left && !room.brick_echo[0].left);
  close(sockets[0]); close(peer);
  puts("server burst checks passed: one clear per tick, later loss repair, handoff, periodic names, reliable rename and staggered echoes");
  return 0;
}
