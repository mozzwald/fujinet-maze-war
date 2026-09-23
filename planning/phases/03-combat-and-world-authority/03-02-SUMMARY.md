# 03-02 Summary

## Outcome

Plan `03-02` is complete. The Atari client now guards the first visible draw of an authoritative shot against stale local-visible actor state, and the new parity smoke locks the required combat/world consume seams in place.

## What Changed

- [`clients/atari/maze-war.asm`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/clients/atari/maze-war.asm)
  - Tightened `NET_SHOT_APPLY` so active shots are cleared or deferred when the emitting slot is currently hidden by authoritative dead/erase state.
  - Added `NET_SHOT_VISIBLE_ORIGIN` to derive the first visible authoritative shot origin from committed authoritative actor state (`NET_PX_X`, `NET_PX_Y`, `NET_PJOY`) instead of trusting packet timing alone.
  - Kept score updates rooted in authoritative snapshot bytes and respawn visibility rooted in the existing `NET_RESP_COMMIT` / `NET_RESP_APPLY_WRK` flow.
- [`tests/combat_client_parity_smoke.sh`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/tests/combat_client_parity_smoke.sh)
  - Added a focused Atari parity smoke that verifies the authoritative shot/snapshot/respawn seams required by Phase 3 are present and still wired together.
- [`tests/combat_world_authority_smoke.sh`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/tests/combat_world_authority_smoke.sh)
  - Hardened the real-server world-authority smoke so it remains usable as a stable guard during Atari combat-path changes.

## Validation

Passed:

```bash
bash tests/combat_client_parity_smoke.sh
bash tests/combat_world_authority_smoke.sh
make build/maze-war-client
```

## Notes

- The Atari shot path now prefers committed authoritative-visible state for the first draw and defers when the matching actor state is still hidden or not yet visibly aligned.
- This plan intentionally did not add local combat prediction or change the existing authoritative respawn/score ownership model.
