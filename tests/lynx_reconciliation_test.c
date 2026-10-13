/* Differential test: actual Lynx prediction versus actual server admission,
   movement and applied-input snapshots. No fake movement implementation. */
#define main maze_server_main
#include "../server/main.c"
#undef main
#include "../clients/lynx/src/protocol.h"
#include <assert.h>
static struct room room;
static uint8_t out[11], snapshot[SNAPSHOT_LEN];
static void setup(void) {
  memset(&room,0,sizeof(room)); game_reset();
  room.round_id=1; room.round_state=ROUND_PLAYING; room.config.kill_limit=10;
  room.clients[0].in_use=room.clients[0].handshake_ok=1;
  room.clients[0].fd=-1; room.occupied_mask=1;
  room.players[0].x=room.players[0].y=5;
  for (int i=0;i<MAX_PLAYERS;++i) room.players[i].joy=15;
  /* Going Up before Right hits the inside corner wall at (5,4). */
  room.brick_bits[85>>3]|=1u<<(85&7);
  game.have_welcome=game.round_authorized=game.snapshot_ready=game.map_ready=1;
  game.local_pid=0; game.round_id=1; game.seat_mask=game.name_echoed=1;
  game.players[0].x=game.players[0].y=5;
  memcpy(game.bricks,room.brick_bits,48);
}
static uint8_t input(uint8_t direction,uint16_t tick,int drop) {
  game_sample_input(direction,0,tick);
  assert(game_next_outbound(out,sizeof(out))==5);
  if (!drop) handle_client_packet(&room,0,out,5,0,tick);
  return out[1];
}
static void server_step(uint64_t tick) {
  apply_queued_input(&room,0,tick); step_players(&room,0);
  build_snapshot(room.seq++,room.players,room.clients[0].applied_input_seq,1,
                 snapshot,sizeof(snapshot));
  snapshot[2]=0x81;
}
static void receive(uint16_t tick) {
  game_input_tick(tick); assert(game_receive(snapshot,sizeof(snapshot)));
}
static void spawn(uint8_t seq,uint8_t flags,uint8_t x,uint8_t y) {
  uint8_t packet[]={PKT_RESPAWN,seq,0,x,y,flags,1};
  assert(game_receive(packet,sizeof(packet)));
}
static void history(uint64_t tick) {
  uint8_t packet[19] = {PKT_INPUT_HISTORY, 1, game.pending_count};
  for (unsigned i=0; i<game.pending_count; ++i) {
    packet[3+2*i]=game.pending_seq[i]; packet[4+2*i]=game.pending_joy[i];
  }
  handle_client_packet(&room,0,packet,3+2*game.pending_count,0,tick);
}
int main(void) {
  setup(); input(2,0,0); input(1,1,0);
  assert(game.players[0].x==6 && game.players[0].y==4);
  server_step(6); receive(18); /* ACK Right, replay queued Up */
  assert(game.players[0].x==6 && game.players[0].y==4 && !game.local_corrections);
  server_step(12); receive(24);
  assert(game.players[0].x==room.players[0].x && game.players[0].y==room.players[0].y);
  assert(!game.local_corrections && !game.pending_count);
  puts("delayed corner, no loss: zero prediction disagreements");

  setup(); input(2,0,1); uint8_t turn=input(1,1,0);
  server_step(6); receive(18);
  assert(game.local_corrections==1 && !game.pending_count);
  assert(game.players[0].x==5 && game.players[0].y==5);
  assert(reconcile_trace[TRACE_BEFORE]==0x0406);
  assert(reconcile_trace[TRACE_SERVER]==0x0505);
  assert(reconcile_trace[TRACE_EXPECTED]==0x0406);
  assert(reconcile_trace[TRACE_REPLAYED]==0x0505);
  assert((reconcile_trace[TRACE_ACK]&255)==turn);
  assert(reconcile_trace[TRACE_DETAIL]==0x000e); /* Up was predicted to move */
  puts("lost Right before admitted Up: P=0406 S=0505 E=0406 R=0505, cumulative ACK skips missing turn precursor");

  /* Direct final, movement, re-stamped echo/reliable final. Neither copy may
     teleport back or delete the newly pending movement. */
  setup(); game_input_tick(100); spawn(20,3,5,5);
  input(2,101,0); input(1,102,0);
  spawn(23,3,5,5); spawn(20,3,5,5);
  assert(game.players[0].x==6 && game.players[0].y==4 && game.pending_count==2);
  assert(reconcile_trace[TRACE_RESPAWN_IGNORED]==2);
  spawn(19,1,0,0); /* old reliable hide arriving after final placement */
  assert(!game.players[0].respawning && game.pending_count==2);
  /* A genuinely new death/placement still resets prediction, even at the
     same spawn cell. Sequence arithmetic and the repair clock can wrap. */
  spawn(24,1,0,0); assert(game.players[0].respawning && !game.pending_count);
  game_input_tick(230); spawn(25,3,5,5); assert(!game.players[0].respawning);
  game_input_tick(65530); spawn(254,3,8,5);
  input(2,65531,0); spawn(1,3,8,5);
  assert(game.players[0].x==9 && game.pending_count==1);
  puts("respawn repair: moved player/pending corner preserved; stale hide ignored; new death and clock/sequence wrap accepted");

  /* Replay can change without any already-applied command disagreement:
     a new authoritative remote occupant blocks an unacked local move. */
  setup(); input(2,0,0);
  build_snapshot(1,room.players,0,1,snapshot,sizeof(snapshot));
  snapshot[2]=1; snapshot[5]=6; snapshot[6]=5; game.seat_mask=3;
  receive(6);
  assert(game.local_corrections==1 && game.pending_count==1);
  assert(reconcile_trace[TRACE_EXPECTED]==65535u);
  assert(reconcile_trace[TRACE_REPLAYED]==0x0505);
  puts("changed remote occupancy: pending replay blocks Right, no applied-input expectation available");
  setup();
  clear_brick(room.brick_bits,5,4); /* client missed the destruction */
  input(1,0,0); server_step(6); receive(18);
  assert(reconcile_trace[TRACE_DETAIL]==0x060e); /* Up blocked by local wall */
  assert(reconcile_trace[TRACE_EXPECTED]==0x0505 && reconcile_trace[TRACE_SERVER]==0x0405);
  puts("stale client brick: K060E distinguishes blocked Up prediction from a missing precursor");
  setup(); game.seat_mask=3; game.players[1].x=6; game.players[1].y=5;
  input(2,0,0); server_step(6); receive(18);
  assert(reconcile_trace[TRACE_DETAIL]==0x0707); /* Right blocked by remote */
  puts("stale client occupancy: K0707 identifies a player-blocked Right prediction");

  /* Losing the entire first history frame is repaired by the next frame.
     Duplicates (including applied-but-not-yet-ACKed inputs) never move twice. */
  setup(); input(2,0,1); input(1,1,1); history(1); history(2);
  assert(room.clients[0].input_count==2);
  server_step(6); history(7); receive(18);
  assert(game.players[0].x==6 && game.players[0].y==4 && !game.local_corrections);
  server_step(12); receive(24);
  assert(room.players[0].x==6 && room.players[0].y==4 && !game.local_corrections);
  assert(!game.pending_count);
  puts("input history: lost precursor repaired before turn, duplicate frames apply each action once, zero corrections");

  setup();
  uint8_t batch[]={PKT_INPUT_HISTORY,1,8,250,7,252,7,255,7,1,7,2,7,4,7,5,7,7,7};
  handle_client_packet(&room,0,batch,sizeof(batch),0,0);
  assert(room.clients[0].input_count==INPUT_QUEUE_MAX && room.clients[0].last_delta_seq==4);
  server_step(6);
  handle_client_packet(&room,0,batch,sizeof(batch),0,7);
  assert(room.clients[0].last_delta_seq==5 && room.clients[0].input_count==INPUT_QUEUE_MAX);
  server_step(12);
  handle_client_packet(&room,0,batch,sizeof(batch),0,13);
  assert(room.clients[0].last_delta_seq==7);
  for (unsigned i=0;i<6;++i) server_step(18+6*i);
  assert(room.players[0].x==13 && !room.clients[0].input_count);
  puts("input history: queue pressure admits only ordered prefixes, sequence gaps/wrap and repeated history apply eight moves exactly once");

  setup(); uint8_t invalid[]={PKT_INPUT_HISTORY,1,2,0,7,1,0};
  handle_client_packet(&room,0,invalid,sizeof(invalid),0,0);
  assert(!room.clients[0].input_count && !room.clients[0].have_delta_seq);
  invalid[6]=7; invalid[5]=0;
  handle_client_packet(&room,0,invalid,sizeof(invalid),0,0);
  assert(!room.clients[0].input_count);
  invalid[5]=255;
  handle_client_packet(&room,0,invalid,sizeof(invalid),0,0);
  assert(!room.clients[0].input_count);
  invalid[5]=1; invalid[1]=2;
  handle_client_packet(&room,0,invalid,sizeof(invalid),0,0);
  assert(!room.clients[0].input_count);
  invalid[1]=1;
  handle_client_packet(&room,0,invalid,sizeof(invalid)-1,0,0);
  assert(!room.clients[0].input_count);
  puts("input history: malformed joy/order/round/length rejects whole frame without admitting a precursor");

  setup(); uint8_t full[52]={PKT_BRICK_FULL,0,1}; full[51]=1;
  memcpy(full+3,game.bricks,48);
  uint8_t cleared[]={PKT_BRICK_DELTA,1,5,4,1};
  assert(game_receive(cleared,sizeof(cleared)));
  assert(game_receive(full,sizeof(full)));
  assert(!(game.bricks[85>>3] & (1u<<(85&7))));
  puts("same-round delayed full map cannot resurrect a destroyed brick");
  uint8_t next_round[]={PKT_RELIABLE_EVENT,0,1,0,PKT_ROUND_START,2,10};
  assert(game_receive(next_round,sizeof(next_round)) && game.bricks[85>>3]==255);
  cleared[4]=2; full[51]=2;
  assert(game_receive(cleared,sizeof(cleared)));
  assert(game_receive(full,sizeof(full)) && !game.map_ready);
  assert(!(game.bricks[85>>3] & (1u<<(85&7))));
  puts("new round resets map; destruction arriving before the initial full map survives initialization");
  return 0;
}
