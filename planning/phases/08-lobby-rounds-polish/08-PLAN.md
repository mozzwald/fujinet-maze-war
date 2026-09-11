# Phase 8 master plan: Lobby and round polish

## Objective

Build the round-based, multi-room FujiNet Lobby version of Maze War on top of
the user-approved TCP/CRC/reliable-event baseline. Preserve server authority,
DLI-disabled Atari rendering, current movement feel, and the hardware
verification loop.

The detailed source assessment is in `08-RESEARCH.md`. Model/effort choices and
mandatory step handoffs are in [08-MODELS.md](08-MODELS.md). The round boundary
contract is in [08-PROTOCOL.md](08-PROTOCOL.md). Product requirements
remain in `ref/mazewar_lobby_rounds_implementation_plan.md`; where they differ,
the research and numbered plans reflect the current merged source.

## Entry gate

Complete `../06-mixed-session-validation-and-hardening/06-01-PLAN.md` and record
the accepted baseline commit. Do not mix Phase 6 fixes with Phase 8 features.

## Execution order

1. `08-01`: reclaim unreachable Atari title/game-over space and measure memory.
2. `08-02`: encapsulate one server room, prove parity, then add TCP rooms.
3. `08-03`: authoritative round events and reset.
4. Hardware checkpoint for the protocol/reset boundary.
5. `08-04`: nonblocking Atari/SDL round-end presentation.
6. Hardware checkpoint for VBI, effects, results, and restart.
7. `08-05`: voluntary leave and unexpected-disconnect grace.
8. Hardware checkpoint for leave/NetStream shutdown.
9. `08-06`: generated endpoints and title/direct-connect UI.
10. Hardware checkpoint for input and direct connection.
11. `08-07`: AppKeys, URL validation, and boot routing.
12. `08-08`: asynchronous QA publisher (execute serially for model handoff).
13. `08-09`: bounded-memory Atari QA room browser.
14. `08-10`: repeated switching and full external QA Lobby launch.
15. `08-11`: prepare a production candidate, then promote only with explicit
    user approval.

Each numbered plan produces a reviewable diff, test evidence, and summary.
Commit executable changes only after the user's testing-based approval. A
failed or uncertain hardware checkpoint stops progression; it is not a completed
step. End each step with the next model/effort recommendation from
`08-MODELS.md`, then let the user switch before continuing. Do not bundle the
next feature into the current step's regression fixes.

## Cross-plan invariants

- TCP room URLs and one listening port per room.
- COBS + CRC-16 remains mandatory in both directions.
- Linux server is authoritative for movement, combat, scores, wins, and reset.
- DLI remains disabled.
- VBI and network receive/ACK processing continue through round presentation.
- Lobby HTTPS never executes in the simulation thread.
- Ordinary SIO/AppKey/HTTP work begins only after explicit `NET_ENDC`, verified
  firmware connection cleanup, and a menu-safe VBI. Stopping serial IRQs alone
  does not establish that the remote TCP connection has closed.
- No large permanent Atari buffer is added until alias lifetime and symbol-map
  checks prove it fits.
- Manual Direct Connect never overwrites the public Lobby selection AppKey.
- QA precedes production; production registration/publication is a separate
  approval step.

## Final acceptance matrix

Run all player compositions from one human/no Zombies through four humans,
including Zombie bump/backfill, voluntary leave, unexpected disconnect,
return-inside-grace, and grace expiry. Cover human-only, human-versus-Zombie,
and Zombie wins; destroyed bricks; winner/loser departure during intermission;
duplicates/stale round events; repeated rounds; full/unavailable/malformed
rooms; Lobby outage; same-room switching; and Direct Connect.

Platforms are server-local SDL, remote SDL, MCP-managed Atari800 + FujiNet-PC,
and physical Atari XL/FujiNet. Production is not complete until the physical
external-Lobby boot, room switch, reboot/last-room, and clean server shutdown
flows pass.
