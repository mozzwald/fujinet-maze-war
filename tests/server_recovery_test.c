/* Exercise the production ACK and admission paths with a private socket. */
#define main maze_server_main
#include "../server/main.c"
#undef main
#include <assert.h>

static struct room room;
static void queued(struct client_slot *c, uint8_t index, uint16_t rev) {
  c->reliable_q[index].rev = rev;
  c->reliable_q[index].len = 5;
  c->reliable_q[index].pkt[0] = PKT_RELIABLE_EVENT;
  c->reliable_q[index].pkt[4] = PKT_NAME;
}

int main(void) {
  struct client_slot *c = &room.clients[0];
  int sockets[2];
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  c->fd = sockets[0];
  c->in_use = c->handshake_ok = 1;
  c->reliable_count = 2;
  queued(c, 0, 1); queued(c, 1, 2);
  c->reliable_sent_rev = 1;
  c->reliable_last_fast_ms = 1000;
  for (unsigned i = 1; i <= 4; ++i) reliable_ack(c, 0, 0, 1000 + 10*i, 0);
  assert(c->reliable_q[0].retries == 0); /* no fallthrough resend */
  reliable_ack(c, 0, 65535, 1050, 0); /* stale */
  reliable_ack(c, 0, 2, 1060, 0); /* unsent */
  assert(c->reliable_count == 2 && c->reliable_q[0].retries == 0);
  reliable_ack(c, 0, 0, 1075, 0);
  assert(c->reliable_q[0].retries == 1);
  reliable_ack(c, 0, 0, 1080, 0);
  assert(c->reliable_q[0].retries == 1);
  reliable_ack(c, 0, 1, 1090, 0);
  assert(c->reliable_count == 1 && c->reliable_acked_rev == 1);
  assert(c->reliable_q[1].retries == 1 && c->reliable_sent_rev == 2);
  reliable_ack(c, 0, 0, 1200, 0); /* stale even after fast interval */
  assert(c->reliable_q[1].retries == 1);
  reliable_ack(c, 0, 2, 1201, 0);
  assert(c->reliable_count == 0);
  /* Revision wrap still advances normally. */
  c->reliable_head = 0; c->reliable_count = 2;
  c->reliable_acked_rev = 65534; c->reliable_sent_rev = 65535;
  queued(c, 0, 65535); queued(c, 1, 0);
  reliable_ack(c, 0, 65535, 1300, 0);
  assert(c->reliable_count == 1 && c->reliable_sent_rev == 0);
  reliable_ack(c, 0, 0, 1301, 0);
  assert(c->reliable_count == 0 && c->reliable_acked_rev == 0);

  room.round_id = 1; room.round_state = ROUND_PLAYING;
  room.players[0].x = room.players[0].y = 5;
  uint8_t delta[] = {PKT_DELTA, 10, 0, 7, 1};
  /* A full queue must not consume a rejected sequence or count admission. */
  c->input_count = INPUT_QUEUE_MAX;
  handle_client_packet(&room, 0, delta, sizeof(delta), 0, 2000);
  assert(!c->have_delta_seq && c->transport.accepted_delta == 0);
  c->input_count = 0;
  handle_client_packet(&room, 0, delta, sizeof(delta), 0, 2001);
  assert(c->input_count == 1 && c->last_delta_seq == 10);
  handle_client_packet(&room, 0, delta, sizeof(delta), 0, 2002);
  assert(c->input_count == 1); /* admitted retry does not move twice */
  apply_queued_input(&room, 0, 2003);
  assert(c->input_count == 0 && c->applied_input_seq == 10);
  handle_client_packet(&room, 0, delta, sizeof(delta), 0, 2004);
  assert(c->input_count == 0); /* ACK lost: applied retry also ignored */
  delta[1] = 11; c->input_count = INPUT_QUEUE_MAX;
  handle_client_packet(&room, 0, delta, sizeof(delta), 0, 2005);
  assert(c->last_delta_seq == 10);
  c->input_count = 0;
  handle_client_packet(&room, 0, delta, sizeof(delta), 0, 2006);
  assert(c->input_count == 1 && c->last_delta_seq == 11);
  /* Full queues still fold neutral keepalives. */
  c->input_count = INPUT_QUEUE_MAX; c->input_head = 0;
  c->input_q[INPUT_QUEUE_MAX-1].joy = 15;
  delta[1] = 12; delta[3] = 15;
  handle_client_packet(&room, 0, delta, sizeof(delta), 0, 2007);
  assert(c->input_count == INPUT_QUEUE_MAX && c->last_delta_seq == 12);
  assert(c->input_q[INPUT_QUEUE_MAX-1].seq == 12);
  c->input_count = 0; c->last_delta_seq = 255;
  delta[1] = 0; delta[3] = 7;
  handle_client_packet(&room, 0, delta, sizeof(delta), 0, 2008);
  assert(c->input_count == 1 && c->last_delta_seq == 0);
  room.round_state = ROUND_OVER;
  delta[1] = 1;
  handle_client_packet(&room, 0, delta, sizeof(delta), 0, 2009);
  assert(c->input_count == 1 && c->last_delta_seq == 0);
  close(sockets[0]); close(sockets[1]);
  puts("server recovery passed: ACK throttle/stale/unsent/wrap, admission, duplicate input, neutral folding and round barrier");
  return 0;
}
