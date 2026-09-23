# FujiNet Lobby integration

Maze War owns its FujiNet AppKeys: creator `$3022`, application `$03`. Key
`$00` stores the player name and key `$01` stores the selected Maze War room.
The game writes only these keys. `MAZEWAR_CREATOR_ID` and `MAZEWAR_APP_ID`
generate those exact bytes into the Atari executable; their defaults are
`$3022` and `$03`.

The official Lobby uses its separate creator `$0001` and application `$01`.
Maze War may read, but never writes, its key `$00` (legacy Lobby username) and
key `$03` (Maze War's application ID, used by the Lobby's launch handoff). This preserves
direct launch from the unmodified Lobby while keeping every Maze War change in
Maze War's own namespace. On boot, Maze War prefers its own name and room,
then falls back to the Lobby values only when its own value is absent or
invalid. A browser or Direct Connect selection is always saved as
`$3022/$03/key $01`.

The Lobby HTTP API does not carry a creator ID. Its `appkey` field is the
one-byte game application ID, so Maze War publishes and queries `3` for app
`$03`. There is no separate game-type configuration. QA and production builds
use the Lobby browser; LAN builds retain Direct Connect. `$2A`/`42` remains a
private smoke-fixture application ID.

The Atari client issues direct SIO commands to FujiNet device `$70`, unit 1:
OPEN `$DC`, READ `$DD`, WRITE `$DE`, and CLOSE `$DB`. OPEN sends six bytes in
little-endian order: creator, application, key, mode, and reserved. Maze War
pins the default 64-byte mode (`0` for read and `1` for write). READ transfers
66 bytes because firmware prefixes the payload with a two-byte little-endian
count. The client reserves 67 contiguous bytes so a 64-byte result can receive
a local terminator without writing past the buffer. WRITE sends a zero-padded
64-byte buffer and carries the meaningful byte count in DAUX.

The scratch buffer aliases `NET_BRICK_BUF` and the following packet staging
only while NetStream is stopped. AppKey reads occur before the first NS_INIT;
username writes occur from the Direct Connect menu after teardown. Validated
username, host, and port are copied into persistent configuration before that
storage is reused by gameplay. The build and memory tests enforce the alias
extent and keep it below `NET_STATE_END`.

The Maze War username is sanitized exactly like the Maze War server: lower-case
letters become upper case, letters, digits, spaces, hyphen, and period survive,
and other bytes are skipped. The result must contain 1–8 characters. A longer
or empty result falls back to manual name entry. Completing the name field in
Direct Connect writes Maze War key `$00`; a failed write reports `NAME OK - APPKEY WAS
NOT SAVED` and leaves the entered name usable for the current connection.

The selected-room value must exactly match `tcp://HOST:PORT`. `HOST` must equal
the generated public host byte-for-byte, and `PORT` must be decimal, fit in 16
bits, and fall in `ROOM_PORT_BASE .. ROOM_PORT_BASE + ROOM_COUNT - 1`. Paths,
queries, fragments, alternate schemes, host aliases, missing fields, oversized
values, and trailing bytes are rejected. Invalid or unavailable AppKeys retain
the generated Direct Connect defaults. Manual Direct Connect never writes the
selected-room key.

At cold start, valid Maze War values, or valid read-only Lobby fallback values,
trigger one
automatic join. The flag is consumed before NS_INIT, so a failed connection
returns to the title instead of looping. Holding OPTION during startup waits
for release and opens Direct Connect, bypassing even a valid stored selection.
An absent SD card, missing key, invalid value, or SIO error leaves the title and
manual setup operational.

Leaving an active game with OPTION clears the two stored room-selection values:
Maze War `$3022/$03/key $01` and the Lobby handoff `$0001/$01/key $03`.
The stored player names remain intact. The client performs those empty AppKey
writes only after it has stopped NetStream and closed the firmware socket, so
the next reset opens the menu instead of autojoining a prior room.

## Atari room browser

When a QA or production build has no valid selected-room AppKey triggering an
automatic join, the Atari title enters the Lobby room browser. NetStream and the
game VBI are stopped at this point. The browser opens FujiNet network device
`$71`, unit 1, for an HTTP GET of:

```text
<LOBBY_BASE>/view?bin=1&platform=atari&appkey=<MAZEWAR_APP_ID>&pagesize=4&page=<0..7>
```

For the current QA registration, this must contain `appkey=3`.

The response uses Lobby binary format 1. Its three-byte header contains a
record count from zero through four followed by two reserved zero bytes. Each
record is exactly 189 bytes:

| Offset | Size | Field |
|-------:|-----:|-------|
| 0 | 1 | AppKey |
| 1 | 17 | NUL-terminated game name |
| 18 | 33 | NUL-terminated server/room name |
| 51 | 65 | NUL-terminated public server URL |
| 116 | 65 | NUL-terminated Atari client URL |
| 181 | 3 | NUL-terminated two-character region |
| 184 | 1 | online flag |
| 185 | 1 | current human players |
| 186 | 1 | maximum players |
| 187 | 2 | reserved/ping-age bytes |

The client reads the header and then reuses one 189-byte record buffer for each
entry. It never retains the complete HTTP body. That buffer aliases
`NET_MAP_CELLS`; counters and four validated 16-bit room ports alias inactive
NetStream state. These aliases exist only on the title screen, before
`NS_INIT`, and the visible room names and occupancy remain in `HOSTSCR`. The
memory-layout and Lobby-browser smoke tests enforce the record extent and keep
the added high code below the `$A000` BASIC ROM window.

Every record is validated again even though the query is filtered. The game
application ID
and game name must match exactly, fixed text fields must be printable,
NUL-terminated, and zero-padded, the record must be online, and occupancy must
be sane. The public URL passes the same strict `tcp://<generated-host>:<room
port>` validator used by selected-room AppKey startup. An invalid record is
omitted and cannot be selected or persisted.

Up/down changes the highlighted room, left/right changes pages, Return or the
joystick trigger joins, `R` refreshes, and OPTION opens Direct Connect. A page
contains at most four records and browsing stops after eight pages. One shared
response timer bounds the header and all record reads; SIO open/read/status
operations also have finite device timeouts. Truncation, extra bytes, invalid
headers, early connection close, and timeouts close the network channel and
show a retryable error. A valid selection is written to Maze War AppKey
`$3022/$03/key $01` before joining. If that write fails, the client reports
the failure briefly and still joins the validated room for the current
session.

## Server publication

The game server does not contact a Lobby unless `--lobby-enabled` is supplied.
That flag also requires all of `--lobby-base`, `--lobby-client-url`, and
`--lobby-public-host`; it defaults Maze War's identity to creator `$3022` and
application `$03`. `--lobby-creator-id 0x3022` and `--lobby-app-id 0x03`
make that identity explicit. `--lobby-appkey` remains a compatible alias for
`--lobby-app-id`. Incomplete values make the
server reject its command line before it opens any game listener. This keeps
LAN development and ordinary test invocations private by default.

For an explicitly enabled server, each configured room independently upserts
`POST <lobby-base>/server`. The request body is the official Lobby server
record:

```json
{
  "game": "Maze War",
  "appkey": 3,
  "server": "Maze War Room 1",
  "region": "us",
  "serverurl": "tcp://public.example:9000",
  "status": "online",
  "maxplayers": 4,
  "curplayers": 1,
  "clients": [{"platform": "atari", "url": "fujinet://maze-war"}]
}
```

The official `POST /server` schema has no creator-ID field. Maze War retains
the configured creator ID for its application identity and logs it at startup;
the published `appkey` is its application ID (`3`). `serverurl` is built from
`--lobby-public-host` and that room's own listener
port. `--lobby-room-names` may provide one comma-separated printable name per
room; otherwise the server supplies `Maze War Room N`. `curplayers` counts
only completed human handshakes: Zombies, sockets waiting for HELLO, and
departing sockets do not increase it. The publisher accepts only the Lobby's
`201 Created` response; malformed HTTP, any other status, timeout, or a 5xx
response is logged and retried with capped exponential backoff.

The available timing controls are `--lobby-refresh-ms` (default 240000),
`--lobby-timeout-ms` (default 1500), and `--lobby-shutdown-ms` (default 2000).
The main simulation copies only the latest occupancy into a one-entry state
per room. A worker thread invokes `curl` as a deadline-controlled child, so
DNS, TLS, HTTP, retries, and a stuck endpoint cannot block the 10 Hz game
loop. On SIGINT or SIGTERM, no further online work is accepted; the worker
uses the remaining shutdown budget to upsert each room with `status: offline`
and `curplayers: 0`, then exits even if the Lobby is unavailable.

The pinned contract is the FujiNet Lobby `POST /server` upsert endpoint: a
valid request returns HTTP `201` with a JSON success object. The local smoke
test covers this request shape, independent rooms, update and refresh,
default-off behavior, failed/malformed replies, timeout isolation, and bounded
offline publication. It is deliberately a local fake endpoint test. Do not
point a development server at QA or production, and do not reuse a temporary
AppKey, until the Lobby owner provides the endpoint and registered Maze War
identity for that environment.

The live QA checkpoint on 2026-09-13 used Maze War AppKey `3` and two rooms.
Both room records appeared, player counts followed joins and leaves, and the
Lobby client launched the selected room on physical Atari/FujiNet and
emulation. This confirms the QA identity only; production promotion remains a
separate 08-11 gate.
