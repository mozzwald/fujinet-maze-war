# Phase 8 round boundary contract

This is the design/acceptance contract for 08-03, not a claim that the wire
format is implemented. Finalize byte layouts in `doc/protocol.md` and fixtures
before changing production decoders. Recheck unused packet IDs then.

## Source findings that require changes

`server/main.c:reliable_send_from` sends the oldest queued event; snapshots and
full maps use separate direct sends. TCP ordering alone therefore cannot ensure
that a queued ROUND_START arrives before a direct reset map/snapshot. CRC loss
on the serial leg can also remove a frame despite TCP delivery.

`reliable_enqueue_client` currently silently returns on queue saturation. A
round boundary cannot inherit that behavior. `reliable_ack` also needs a bound
against acknowledging revisions that have not been sent.

Atari's `NET_REL_INNER` currently compares the NAME wrapper's exact length with
`REL_PKT_MAX`. Increasing that maximum must not change the accepted NAME size.
Audit every fixed length, staging buffer, test fixture, and Linux decoder.

## Session compatibility and round identity

1. Introduce an explicitly versioned round-capable session handshake before
   admitting gameplay. Reserve free IDs for hello/welcome/rejection only after
   checking all current definitions. Set bounded join/handshake deadlines and
   a readable client error for missing/incompatible capability. Old clients
   must not occupy a new round room indefinitely while failing to ACK unknown
   events. Document the coordinated server/client upgrade requirement; keep
   08-02's legacy single-room parity checkpoint before this change.
2. Carry round identity in all round-scoped inputs and authoritative state:
   snapshots, full/delta bricks, shot/respawn events, MATCH_END, ROUND_START,
   and any retained compatibility echo. NAME and SEATS remain session/seat
   metadata; they must not overwrite frozen results.
3. A one-byte round ID is adequate only with modulo-256 comparison, an explicit
   initial anchor from the handshake, and bounded session/retry lifetimes that
   prevent an outstanding event surviving 128 rounds. Test 255->0. If the
   final timeout policy cannot prove that bound, widen the epoch and re-budget
   frames before implementation; do not rely on ordinary integer comparison.
4. Reliable revisions remain monotonic for the TCP session, across rounds.
   Consume/ACK a valid old-round reliable event to advance the stream while
   suppressing its gameplay effect. Discard stale round inputs without falsely
   advancing applied-input acknowledgements. Clear prediction/pending input
   rings at a round transition; no held fire/move from results enters new play.

## Frozen results

Proposed MATCH_END inner payload (43 bytes):

```
[0]      $54
[1]      round_id
[2]      winner_pid
[3]      final_active_mask
[4]      final_zombie_mask
[5]      kill_limit
[6..9]   final scores p0..p3
[10]     historical_zombie_mask
[11..42] four frozen 8-byte names
```

Rows describe occupants at the winning instant, not all previous occupants of
a reused seat. Freeze their final scores and roles together. The historical
mask answers whether Zombies participated; it must never relabel a later human
winner as a Zombie. Preserve the winner's role/name even after disconnection.
Unused rows are blank; fallback names are slot-qualified WIZARD n/ZOMBIE n.
This deliberately avoids promising a full departed-player history in four rows.

MATCH_END uses 49 decoded bytes with the four-byte reliable wrapper and CRC.
Check encoded COBS length as well as decoded size against `NET_FRAME_MAX=60`.
The same checks apply to the larger reliable round-map baseline described below.

## Reset and recovery

1. Freeze the result at the first winning score mutation. Stop subsequent
   combat and gate ingress mutations too, including client brick/respawn
   commands. Preserve the frozen result separately from changing live seats.
2. Keep frozen snapshots and a neutral client heartbeat flowing throughout
   intermission, presentation, and baseline synchronization. Server-side idle
   timeout must not fire merely because the Atari suppresses movement/fire.
   Heartbeats must not become predicted movement or falsify applied-input ACKs.
3. Reset canonical bricks, actors, scores, shots, pending respawns, input queues,
   echoes, and per-round history exactly once on the server. Retain session
   identity and reliable revisions. Clients reset round state without calling
   START/NET_INIT or clearing the session reliable stream.
4. Send a reliable ROUND_START authorization plus a matching round-tagged full
   map baseline and a fresh matching snapshot. Prefer making the full baseline
   a reliable event: a 51-byte current BRICK_FULL plus one-byte epoch, four-byte
   wrapper, and CRC is 58 decoded bytes (59 COBS bytes, excluding delimiter).
   Confirm actual encoder buffers and maximums before adopting this layout.
   Periodic full-map repair remains useful but cannot authorize a new round.
5. Receiving a new epoch invalidates old map/snapshot readiness. Stage data by
   epoch and reveal gameplay only after authorization, fully applied matching
   map, and matching fresh snapshot are present. A lost/corrupt frame must be
   retried/recovered; stale readiness bits cannot open this gate. A gate timeout
   returns through controlled session recovery rather than showing stale play.
6. Control events/baselines must never be silently dropped on a full reliable
   queue. Reserve bounded transition capacity or disconnect only the stalled
   peer with a documented resync path. Do not block room ticks or all other
   clients. Preserve queued/partially written TCP frame integrity; do not clear
   arbitrary queued bytes to make room. Reject ACKs beyond the sent watermark.
7. Late joiners anchor to the current epoch and phase. During intermission show
   the frozen results directly; do not animate actors whose old positions were
   never observed. A fully ready ROUND_START interrupts unfinished local effects
   and restores play; animations cannot veto server authorization.

## Required fault cases

Cover delayed old inputs/events across reset, old full map after ROUND_START,
corrupt/lost baseline with valid snapshots, duplicate transitions, wraparound,
late join, saturated reliable queue, unsent-revision ACK, stalled peer, and
intermission longer than both endpoint watchdogs. Inject framed serial loss
separately from TCP fragmentation/delay; TCP itself does not expose packet loss
as reordered application messages. Prove failure recovery and unchanged
ordinary-play movement cadence before the 08-03 hardware checkpoint.
