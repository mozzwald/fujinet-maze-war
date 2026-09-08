# 03-01 Summary

## Outcome

Plan `03-01` is complete. The server-side combat/world contract is now explicit in code and protocol docs, and the new smoke coverage proves authoritative ordering and world outcomes against the real debug server.

## What Changed

- [`server/main.c`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/server/main.c)
  - Added stable debug markers for authoritative combat order and combat/world events.
  - Documented the same-tick authoritative order in `step_players`.
  - Exposed explicit debug traces for fire evaluation, movement application/gating, shot spawn/step, brick breaks, immediate hits, moving hits, and respawn finalization.
- [`doc/protocol.md`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/doc/protocol.md)
  - Updated the snapshot table to the current 20-byte contract.
  - Added a dedicated `Combat And World Authority Semantics` section.
  - Clarified that fire is intent-only on clients and that score/death/respawn/brick outcomes are server-authoritative.
- [`tests/combat_ordering_smoke.sh`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/tests/combat_ordering_smoke.sh)
  - Added a real-server smoke harness for authoritative ordering markers.
  - Uses a deterministic temporary brick layout and drives move/fire/brick/hit cases against the debug server.
- [`tests/combat_world_authority_smoke.sh`](/mnt/sda5/home/mozzwald/fujicode/fujinet-maze-war/tests/combat_world_authority_smoke.sh)
  - Added a real-server smoke harness for authoritative score/respawn/brick outcomes.
  - Uses live packet observation plus log assertions to validate immediate hit, moving-shot hit, brick mutation, and final respawn publication.

## Validation

Passed:

```bash
bash tests/combat_ordering_smoke.sh
bash tests/combat_world_authority_smoke.sh
make build/maze-war-server
```

## Notes

- The smoke harnesses now use a temporary border-plus-interior-brick layout and a lower server tick rate to make authoritative one-step setup sequences deterministic without changing gameplay semantics.
- The ordering smoke relies on stable server debug markers instead of overfitting to one exact shot-packet timing path.
