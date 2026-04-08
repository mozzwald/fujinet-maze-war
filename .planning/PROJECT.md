# FujiNet Maze War

## What This Is

FujiNet Maze War is a multiplayer Atari 8-bit adaptation of Maze War with a networked server, a Linux test client, and an Atari client built with MADS assembler. The current focus is getting the Atari gameplay loop to feel like the original game while staying in sync with the server during mixed sessions with human wizards and AI zombie replacements.

## Core Value

An Atari wizard can move and fire smoothly while staying visually aligned with the server-authoritative game state in a live multiplayer match.

## Requirements

### Validated

(None yet — ship to validate)

### Active

- [ ] Atari client movement is client-predicted, preserves original Maze War animation, and stays closely aligned with server-authoritative wizard state.
- [ ] Bullets always originate from the wizard position visibly shown on the Atari client.
- [ ] A live match supports 1 Atari client, 1 Linux client, and 2 server AI zombies without gameplay-desync bugs blocking play.
- [ ] Empty wizard slots are filled by server AI zombies, and human connections can replace zombies without breaking gameplay.

### Out of Scope

- Smarter or more competitive zombie AI — current zombie behavior can remain simple and slow while sync/gameplay issues are addressed.
- Broader polish work unrelated to smoother Atari gameplay sync — this effort is focused on gameplay correctness first.

## Context

This repository already contains a working Linux client, an Atari client that partly works, and a server that coordinates multiplayer communication. The main gameplay issue is that Atari movement can be jumpy and sometimes teleports to the server-reported location. Local Atari wizard state can diverge from the server state badly enough that firing produces bullets in the wrong row or column. The networking stack uses FujiNet NetStream together with FujiNet-PC, and the Atari client code should follow MADS assembler syntax. The repository's `ref` materials include important hardware, networking, and Atari programming references.

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

---
*Last updated: 2026-04-07 after initialization*
