# Phase 8 model selection and step handoffs

Reviewed 2026-09-11. These are implementation-owner recommendations, including
debugging and validation, not recommendations for merely reading a plan. They
are engineering judgments about this repository; no comparative model benchmark
has been run on these steps. Use OpenAI models only; do not use local Ollama.

## Selection basis

The current Codex model cache exposes all four model IDs below and supports
the recommended reasoning levels. OpenAI describes
[GPT-5.6 Luna](https://developers.openai.com/api/docs/models/gpt-5.6-luna) as
suited to cost-sensitive work,
[GPT-5.6 Terra](https://developers.openai.com/api/docs/models/gpt-5.6-terra) as
balancing intelligence and cost,
[GPT-5.6 Sol](https://developers.openai.com/api/docs/models/gpt-5.6-sol) as
suited to complex work, and
[GPT-6 Astra](https://developers.openai.com/api/docs/models/gpt-6-astra) as
the most capable choice for the hardest work. These official model pages were
checked for this review. API prices are not a statement of the user's Codex
subscription cost or limits.

Terra is the default for bounded implementation and validation. Sol is worth
the extra capacity for ownership refactors, interrupt-sensitive assembly, and
failure recovery. Reserve Astra for round synchronization and final session
integration, where a plausible local fix can violate several other systems.
Medium reasoning is sufficient for routine work; high is justified where
ordering and shared state dominate. No step currently warrants xhigh, max, or
ultra. Luna with low reasoning is suitable for an explicitly isolated wording
or bookkeeping edit, but none of these complete implementation steps is only
bookkeeping. Do not downgrade a whole step because its happy path looks short.

## Recommended execution order

| Step | Recommended model | Reasoning | Why |
|---|---|---|---|
| 06-01 prerequisite | `gpt-5.6-terra` | medium | Run existing validation and establish reliable evidence; escalate an actual cross-system defect separately. |
| 08-01 memory reclamation | `gpt-5.6-terra` | high | Bounded removal, but shared helpers, fixed addresses, and memory aliases require careful reachability analysis. |
| 08-02 multiple rooms | `gpt-5.6-sol` | high | Broad ownership refactor with socket lifetimes, fairness, and hidden per-room state. |
| 08-03 round protocol/reset | `gpt-6-astra` | high | Protocol evolution and synchronization across three clients, reliable queues, prediction, and map readiness. |
| 08-04 presentation | `gpt-5.6-sol` | high | Nonblocking 6502 effects must coexist with VBI, network staging, sound, and display restoration. |
| 08-05 leave/grace/reset | `gpt-5.6-sol` | high | Socket/seat separation, timed grace, actual FujiNet shutdown, and reusable client teardown. |
| 08-06 build/title setup | `gpt-5.6-terra` | medium | Bounded configuration generation and reuse of established text input routines. |
| 08-07 AppKeys/startup | `gpt-5.6-sol` | high | Direct SIO, buffer boundaries, persistent configuration, and safe boot fallback. |
| 08-08 QA publisher | `gpt-5.6-terra` | medium | Isolated publisher with explicit bounded queue/timeout contract and fake endpoint tests. |
| 08-09 room browser | `gpt-5.6-sol` | high | Streaming binary parsing and display storage inside tight Atari memory and SIO constraints. |
| 08-10 switching/QA launch | `gpt-6-astra` | high | Integration of transport, IRQ state, persistent selection, menu memory, and external boot lifecycle. |
| 08-11 release/promotion | `gpt-5.6-terra` | medium | Execute the established release checklist and verify artifacts; new defects return to their owning step. |

Each numbered plan repeats its recommendation and identifies its next step.
The table is the shared source of truth; update both if scope changes.

## Required end-of-step handoff

Every user-facing end-of-step summary and saved `NN-NN-SUMMARY.md` must include:

1. What changed and the meaningful validation results.
2. Any pending hardware test, approval, or unresolved defect. A pending gate
   means implementation is ready for testing, not that the step is complete.
3. The next eligible step, its exact recommended model ID, reasoning level,
   and one short reason. If a gate is pending, make the recommendation
   conditional on acceptance of that gate.

Example: "08-02 is complete; single-room parity and room isolation checks pass.
Next: 08-03 — switch to **gpt-6-astra, high reasoning** for the cross-client
round/reset protocol."

With a gate pending: "08-04 is ready for your Atari/FujiNet test. After your
acceptance, next is 08-05 — **gpt-5.6-sol, high reasoning** for leave and session
teardown."

Stop at this handoff so the user can switch models. Do not automatically start
the next numbered plan, spawn another model, or claim the model has changed.
Keep the current recommendation while fixing that step's failed tests; propose
Sol/high or Astra/high only when evidence shows a harder problem than planned
(for example, an unresolved cross-client ordering or interrupt race). A routine
failed test alone is not a reason to select the most expensive model.

Hardware acceptance belongs to the user. Prepare changes and test evidence
before asking for that acceptance, and commit executable changes only after
the user's testing-based approval. At 08-11 completion, report that no further
Phase 8 step is scheduled; do not invent a next implementation task.
