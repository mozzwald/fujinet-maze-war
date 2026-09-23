# FujiNet Maze War

## What This Is

FujiNet Maze War is a multiplayer Atari 8-bit adaptation of Maze War with a networked server, a Linux test client, and an Atari client built with MADS assembler. The current focus is getting the Atari gameplay loop to feel like the original game while staying in sync with the server during mixed sessions with human wizards and AI zombie replacements.

## Core Value

An Atari wizard can move and fire smoothly while staying visually aligned with the server-authoritative game state in a live multiplayer match.

## Requirements

### Validated

- ✓ Server accepts one canonical client input packet format and normalizes FujiNet NetStream framing before gameplay logic runs — validated in Phase 1
- ✓ Server exposes packet/debug counters and summary logs that distinguish transport-framing faults from later gameplay or reconciliation faults during mixed-session testing — validated in Phase 1

### Active

- [ ] Atari client movement is client-predicted, preserves original Maze War animation, and stays closely aligned with server-authoritative wizard state.
- [ ] Bullets always originate from the wizard position visibly shown on the Atari client.
- [ ] A live match supports 1 Atari client, 1 Linux client, and 2 server AI zombies without gameplay-desync bugs blocking play.
- [ ] Empty wizard slots are filled by server AI zombies, and human connections can replace zombies without breaking gameplay.
- [ ] The net build embeds the current netstream handler built from the `fujinet-atari-netstream` source repo, and game code never touches POKEY channels 3/4, AUDCTL, or SKCTL after netstream init (sound restricted to channels 1+2).

### Out of Scope

- Smarter or more competitive zombie AI — current zombie behavior can remain simple and slow while sync/gameplay issues are addressed.
- Broader polish work unrelated to smoother Atari gameplay sync — this effort is focused on gameplay correctness first.

## Context

This repository already contains a working Linux client, an Atari client that partly works, and a server that coordinates multiplayer communication. The main gameplay issue is that Atari movement can be jumpy and sometimes teleports to the server-reported location. Local Atari wizard state can diverge from the server state badly enough that firing produces bullets in the wrong row or column. The networking stack uses FujiNet NetStream together with FujiNet-PC, and the Atari client code should follow MADS assembler syntax. The repository's `ref` materials include important hardware, networking, and Atari programming references.

Current state: Phases 1, 2 and 3.1 are complete and confirmed; Phase 5 is code-complete. Phase 3 (combat/world authority) is code-complete with green smokes, and its human mixed-session checkpoint is the one outstanding item — now unblocked. Phase 3.1 refreshed the handler from source and remapped all sound to POKEY channels 1+2, fixing a live serial-corruption bug where the walk sound (`MOVSND`) wrote AUDF3/AUDF4, the handler's joined ch3+4 baud timer, whenever a slot-2/3 actor walked; that plausibly explains much of the historical hardware flakiness. A later session fixed connectivity: the server no longer shares its UDP port with FujiNet-PC (which had been silently swallowing client datagrams whenever the host was `localhost`), and the Atari client now times out instead of freezing when no authoritative state arrives. Phase 5 then made slot handoffs clean in both directions.

## Constraints

- **Authority**: Server remains authoritative for wizard position and state — client-side smoothing must reconcile to server truth.
- **Rendering**: Preserve the original game's movement/turning animation feel on the Atari client — smoothness cannot come from removing the original visual behavior.
- **Tooling**: Atari assembly changes must remain valid for MADS assembler — maintain syntax and conventions compatible with the existing build.
- **Validation**: Success is measured in a mixed session with 1 Atari client, 1 Linux client, and 2 AI zombies — fixes must hold in that concrete scenario.

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Keep the server authoritative while adding client prediction on Atari | Smooth local play is required, but shared multiplayer truth must remain consistent | — Pending |
| Cap visible correction at roughly one maze cell in normal play | Large snaps break playability and reveal state divergence | — Pending |
| Treat bullet origin matching the visible wizard position as a hard gameplay requirement | Firing from the wrong row or column is a visible correctness bug, not cosmetic polish | — Pending |
| Allow server-side refactors if needed to improve Atari sync | Fixing Atari gameplay is more important than preserving current server internals | — Pending |
| Normalize DELTA ingress before reconciliation work | Transport ambiguity was polluting gameplay debugging and had to be isolated first | ✓ Good |
| Keep transport observability server-first | Mixed-session diagnosis needs stable counters and summaries at the authoritative ingress boundary | ✓ Good |
| Build NSENGINE.OBX from the sibling netstream source repo, keeping a refreshed checked-in copy as fallback | The embedded handler went stale and missed correctness fixes; building from source keeps it current without breaking builds on machines lacking the repo | ✓ Good — the rule reproduces the checked-in OBX byte for byte |
| Restrict game sound to POKEY channels 1+2 with a priority mix (local player owns ch1, remotes share ch2) | The handler owns channels 3+4 as its joined baud timer; the per-player 4-channel scheme corrupted RX timing whenever slot 2/3 actors made sound | ✓ Good — AUDCTL=$28 and AUDF3/AUDF4 stay handler-programmed through play and handoffs, NET_NS_ERRS clear |
| Insert Phase 3.1 before the Phase 3 human checkpoint, and run Phase 5 before Phase 4 | Hardware symptoms must be re-judged after the baud-timer corruption fix; slot-lifecycle correctness blocks play while render smoothing is release polish | ✓ Good |
| Refuse to share the server's UDP port instead of setting SO_REUSEADDR | FujiNet-PC binds the same netstream port on the same host and silently swallowed the client's datagrams; failing the bind turns an invisible dead connection into a named startup error | ✓ Good |
| Give the Atari client a silence watchdog that returns to the host prompt | NS_INIT succeeding only means FujiNet opened a socket, so a wrong host or a stolen port previously froze the game on a blank screen with no way out | ✓ Good |
| On a slot handoff, reset the occupant's transient state but leave the actor where it stands | Facing, score, in-flight shots and zombie schedules belong to the previous occupant; position is the slot's physical location, and teleporting on handoff would show every other client an unexplained jump | ✓ Good |
| Run Phase 5 before the Phase 3 human checkpoint rather than after | Both need the same mixed session to verify, so checkpointing first would have meant running it twice | — Pending |

---
*Last updated: 2026-09-02 after Phase 3.1 verification and Phase 5 implementation*
