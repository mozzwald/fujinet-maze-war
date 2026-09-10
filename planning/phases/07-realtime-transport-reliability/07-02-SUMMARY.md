# 07-02 — TCP clients implementation and emulator verification

Code and emulator validation completed 2026-09-09 on `realm-net`.
The user subsequently confirmed the same path works on real Atari/FujiNet
hardware, closing this plan's acceptance gate.

Atari `NET_FLAGS` is `$05`: TCP, REGISTER clear, external TX/internal RX clock.
Baud remains 57600. The checked-in handler, transmit framing, receive framing,
and gameplay/reconciliation code are unchanged.

Both Linux clients connect over TCP with TCP_NODELAY and nonblocking I/O after
connect. The shared `net/tcp_stream.h` preserves partial output, buffers partial
COBS frames, dispatches coalesced frames, rejects malformed/checksum-invalid
frames, and detects EOF/errors. The terminal client now sends an idle heartbeat
once per second, like the SDL client. On disconnect the Linux programs exit;
relaunch to connect again.

Plan corrections: neither Linux client previously connected its UDP socket;
both used sendto/recvfrom. Their old decoders also assumed one datagram per
frame and ignored the checksum. A socket-type-only edit would not have worked.

Validation:

- Full build and smoke suite passed; shared framing and write buffering are
  covered by `tcp_stream_smoke.sh`.
- MCP-managed Atari800 XL, BASIC off, NetSIO port 19997, local FujiNet-PC archive
  `v1.6.2-dev+git-ed2d256bf` (Ubuntu 24.04 amd64), server on 127.0.0.2:9000 with
  two zombies. `$05` successfully initialized, DELTA/SNAPSHOT traffic flowed,
  and the playfield and player names rendered correctly.
- Stopped the server deliberately. Atari returned to the host prompt with
  `SERVER STOPPED RESPONDING`. Restarted the server and accepted host/name
  again: traffic and gameplay resumed without resetting/reloading Atari.
  `NET_NS_ERRS` at the freshly built symbol address $7A26 was zero before
  disconnect and after reconnect. No REGISTER fallback was required.
- Ran the actual terminal client in a PTY and the actual SDL client with dummy
  video plus injected SDL keyboard events, sequentially alongside the Atari
  and two zombies. Both decoded snapshots, sent input, and exited cleanly.
- Screenshot evidence was saved by MCP in the session artifact directory:
  `screenshot_1788961732.png` (play), `screenshot_1788961854.png` (lost-server
  prompt), `screenshot_1788961948.png` (reconnected play).

During server downtime the selected FujiNet-PC build repeatedly logged TCP
connect failures, producing a large log burst. A 960-frame MCP run request
hit its command timeout; a subsequent screenshot confirmed the Atari had
reached the recovery prompt, and normal reconnect succeeded. This is recorded
as a sidecar observation, not proof of behavior on physical SIO hardware.

The user confirmed real Atari/FujiNet hardware works against the emulator path,
closing the 07-02 hardware acceptance gate. CRC-16 (07-03) was subsequently
implemented; reliable events (07-04) and authority changes (07-05) remain out
of scope for this plan.
