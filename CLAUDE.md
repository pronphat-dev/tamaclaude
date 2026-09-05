# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A desk device that shows live Claude Code session status via a blocky orange Claude mascot,
and uses the same screen for four more pages — weather, crypto, stocks, calendar — that the
user swipes between or lets rotate on their own.
Hardware: ESP32-2432S028R ("Cheap Yellow Display"), ILI9341 320x240 landscape, XPT2046 touch
(horizontal swipes only), BLE first with a sealed LAN path behind it.

## Where a reason goes

Every "why" has exactly **one** home. Walk this in order and stop at the first match — there
is no fifth bucket, and a reason that fits none of them is a reason nobody needed written down:

1. **Can it sit beside the code it governs?** → a Thai comment there. If it can, it **must** —
   and it must **not** be restated in any document. A document that retells the code is a copy
   that ages without anyone noticing. This is where nearly everything lands: a colour, a rect,
   a threshold, a framework quirk, a rejected alternative.
2. **Must it hold in two files at once that cannot reference each other at runtime?** → the
   invariants list below, plus a comment on each side naming its counterpart
   (`gen/calendar.py` ↔ `ct_calendar_ui.c` show the shape).
3. **Is it a decision that is hard to undo, with alternatives worth recording?** → a new
   `docs/adr/`. ADRs are dated records, not live rules: never edit one when the world changes,
   write the next one and say which it reverses.
4. **Is it a term?** → `CONTEXT.md`. **A value measured off the real board?** →
   `docs/hardware.md`.

**Read the code you are about to change** — the reasons are in it, at the density the
surrounding file already uses. Ordinary fixes need no entry anywhere; git history is the log.
`DESIGN.md` was retired for failing all of this: it had no definition, so it only ever grew.
Do not recreate it under any name.

## Data flow

```
Claude Code hooks --> tamaclaude --hook --> Unix socket --> daemon --> BLE GATT --> board
                                                                  \                  ^
                                                                   '-> sealed TCP ---'
                                                                       (only when BLE
                                                                        is 10 s gone)

Open-Meteo --> WeatherService (every 15 min) --> page frame --> the same two paths
CoinGecko  --> CryptoService  (every 60 s)  --> page frame --> the same two paths
Finnhub    --> StocksService  (every 60 s, only while the US market is open)
EventKit   --> CalendarService (every 5 min, this Mac only — never the network)

Claude Code statusline --> ~/.tamaclaude/statusline.sh --.
                                                          >--> ~/.claude/.statusline-usage-cache
menu bar timer --> tamaclaude --usage-poll --> claude.ai --'                |
                                                        daemon reads --> "u" key --> board
```

The quota panel has **two** sources, not one. The statusline pipe needs no credential but is
event-driven, so it goes quiet exactly when the desk display is left alone; the poll pipe uses
the user's `sessionKey` and keeps the number moving with Claude Code closed. Neither replaces
the other and they are separate switches: setting a key never uninstalls the statusline.

The daemon owns every decision about *content*: it knows the two fixed enums the firmware
draws (`VisualState`, `PageKind`) and sends one frame per page. What the board owns is
*which page is on screen right now* — it caches every page in RAM, runs the rotation clock,
holds after a swipe, and jumps to the mascot on an attention event, all so a sleeping Mac
cannot freeze the screen on one page all night.

Because every page is cached, **a page may draw from another page's frame** (ADR-0012) —
the mascot sky reads the weather frame's WMO code, the weather page reads the mascot
snapshot's clock. That costs nothing on the wire and does not move the line above: what
gets drawn is still a value the daemon sent, never one the board synthesised. A frame the
board would have to treat as expired is treated as absent instead.
Tool-to-animation mapping is host-side and user-overridable at `~/.tamaclaude/tools.json`.
Pose timing is overridable the same way at `~/.tamaclaude/timings.json` (keys match the
`Timings` properties; a negative value is dropped, the rest of the file still applies).

## Commands

### Host (Swift, macOS 14+)

```bash
cd host
swift build                        # debug
swift run tamatest                 # run the whole test suite
swift run tamaclaude --daemon --print --no-ble -v   # daemon without bluetooth, prints snapshots
swift run tamaclaude --send '<json>'                # inject one hand-written hook event
swift run tamaclaude --decide allow <id>           # answer a permission request without a board
swift run tamaclaude --usage-poll                   # one quota fetch -> cache, then exit
swift run tamaclaude --usage-cache < statusline.json  # the statusline pipe, by hand
swift run tamaclaude --install-statusline           # take over statusLine.command
swift run tamaclaude --remove-statusline            # give the slot back
./Scripts/make-app.sh              # release .app -> host/dist/TamaClaude.app
./Scripts/make-app.sh --install    # install to /Applications and launch
```

`--usage-poll` reads the key from `~/.tamaclaude/session-key` (mode 600, never argv, never env)
and exits: `0` = wrote the cache · `2` = key rejected · `3` = key file unusable · `1` = anything else.
The menu bar app runs it on a timer; the key is set from its gear menu, not by hand.

There is **no `testTarget`** and no per-test filter — `swift run tamatest` runs everything
(`Sources/tamatest/Tests.swift`, grouped by `suite("...")`). A machine with only Command Line
Tools would build a `testTarget` and exit 0 without running it, which is worse than no tests.
To narrow the run, temporarily comment out `suite(...)` calls in `runAllTests()`.

Bluetooth only works from the `.app` launched via LaunchServices (`open`) — a bare binary run
from a shell gets `SIGABRT` from TCC, not a polite denial. Every rebuild changes the adhoc
cdhash, so macOS re-asks for Bluetooth permission.

### Firmware (ESP-IDF v5.5)

```bash
cd firmware
idf.py -p /dev/cu.usbserial-XX flash monitor
```

`firmware/probe/` is a separate throwaway IDF project that interrogates the real panel
(MADCTL, colour order, inversion). Its findings are recorded in `docs/hardware.md`; the firmware
uses those constants, not a chip model number.

### Graphics / preview (Python + Pillow)

```bash
python3 tools/preview.py            # render every state + whole screens (weather, crypto, stocks, calendar) to out/
python3 tools/preview.py --sheet    # contact sheet only
python3 tools/preview.py --limits   # measure how many cells each label holds (Text.Limit)
python3 tools/export_layout.py      # tools/layout.toml -> firmware/main/layout.h
python3 tools/export_logos.py       # logos/*.svg + logos.toml -> ct_logos.c + png + LogoCatalog.swift
python3 tools/export_thai.py        # tools/thai.toml -> ThaiTable.swift + gen/thai_table.py
python3 tools/export_thai_font.py   # tools/thai.toml + Sarabun -> ct_font_thai_*.c + fonts/*.json
python3 tools/test_thai.py          # thai golden vectors, python side (swift side is in tamatest)
python3 tools/make_icon.py          # logo PNG (≥128px) + mascot (≤64px) -> host/Resources/AppIcon.icns
```

There is no SDL2 simulator. `tools/preview.py` is the visual dev loop: change a rect, render,
look at `out/`. It proves the *design*, not the C renderer.

## Architecture

### One source of truth per concern

- **`tools/layout.toml`** — every layout constant, palette colour, and randomised table
  (star/grass/cloud positions). Python reads it via `tools/gen/config.py`; C gets it through
  the generated `firmware/main/layout.h`. **Never edit `layout.h`** — edit the TOML and rerun
  `export_layout.py`. If preview and board disagree, that is a renderer bug, by construction.
- **`tools/thai.toml`** — every Thai glyph-variant rule and per-size shift. Swift gets
  `ThaiTable.swift`, Python gets `gen/thai_table.py`, the font gets `ct_font_thai_{12,14}.c`,
  all through `export_thai*.py`. **Never edit the generated files.** The cluster walk itself is
  a parallel port (`TamaCore/ThaiShaper.swift` ↔ `gen/thai.py`) held together by
  `tools/thai-golden.json`, which both test sides read. See ADR-0008.
- **`tools/gen/*.py` ↔ `firmware/main/ct_*.c`** — deliberate parallel ports, file for file:
  `props.py`↔`ct_props.c`, `mascot.py`↔`ct_mascot.c`, `rects.py`↔`ct_rects.c`,
  `screen.py`↔`ct_ui.c`, `weather.py`↔`ct_weather_ui.c`, `crypto.py`↔`ct_crypto_ui.c`,
  `stocks.py`↔`ct_stocks_ui.c`, `calendar.py`↔`ct_calendar_ui.c`,
  `trend.py`↔`ct_trend.c`, `age.py`↔`ct_age.c`,
  `mini.py`↔`ct_mini.c`, `footer.py`↔`ct_footer.c`, `topbar.py`↔`ct_topbar.c`,
  `sky.py` folds into `ct_ui.c`.
  `pages.py` is the odd one out: it feeds `export_layout.py`, which generates `ct_page_kind_t`. A visual change means editing both
  sides; the Python side is where you iterate, the C side is the port.
- **`tools/logos/*.svg` + `tools/logos.toml` → `ct_logos.c` + `tools/logos/png/` +
  `LogoCatalog.swift`** — coin *and* ticker logos, the one asset that is **not** a rect list.
  Full-colour RGB565A8 bitmaps at 32 and 16 px, rasterised once by `export_logos.py`; the board,
  the preview, and the Mac settings window all read that one raster. One table serves both
  watchlist pages — drop `AAPL.svg` in beside `BTC.svg` and it shows up everywhere, and the
  32-file cap is shared (ADR-0011). The SVG is still the artwork and its filename is still the
  key the board looks up; `logos.toml` answers the two questions a filename cannot — coin or
  ticker, and what the thing is called — because the settings menu needs both before it can
  offer a choice. **A file without an entry, or an entry without a file, fails the export**:
  a logo that reaches the board but not the menu is worse than one that has not shipped yet,
  and `tamatest` reads the real folder to catch an export nobody ran.
  **Never edit the generated files.** See ADR-0010, and `gen/logos.py` ↔ `ct_logos.c` for the
  lookup rule (unknown symbol → `_default`, never a blank).
- **Every other asset is a rect list**, `{x, y, w, h, color}` in mascot-relative *unit*
  coordinates — no sprite pipeline. The preview and the board both come from `gen/mascot.py`.
  The **app icon is half an exception** — `.icns` carries per-size art, so ≥128 px is a
  hand-drawn PNG (`docs/images/tamaclaude-logo.png`) and ≤64 px is drawn from the same rect
  list — a rect list can only draw a flat silhouette, which is right for a 320x240 panel and
  wrong beside Dock icons that carry material and light. See `tools/make_icon.py`.

### Host layout (`host/Sources/`)

| File | Role |
|---|---|
| `TamaCore/Protocol.swift` | `HookEvent`, `VisualState` (+ `priority`), `Snapshot`, MTU squeeze |
| `TamaCore/Pages.swift` | `PageKind` (the firmware contract), `PageFrame`, `PageHub` — what may be sent, and what is worth resending |
| `TamaCore/Weather.swift` | the weather page frame + the Open-Meteo payloads, as pure functions over bytes |
| `TamaCore/WeatherService.swift` | the weather settings and the fetch schedule — fed `tick(now:)`, owns no timer |
| `TamaCore/Crypto.swift` | the crypto page frame + the CoinGecko payloads — the squeeze never cuts a symbol |
| `TamaCore/CryptoService.swift` | the watchlist (5 max) and its 60 s round — the shape the stock page borrows |
| `TamaCore/Stocks.swift` | the stocks page frame + the Finnhub payloads + what "the market is open" means |
| `TamaCore/StocksService.swift` | the watchlist (5 max), the user's key, and a round that stops at the closing bell |
| `TamaCore/Calendar.swift` | the calendar page frame + the appointment-to-rows converter, as pure functions |
| `TamaCore/CalendarService.swift` | which calendars may show, the 5 min round, and what the page says when it cannot read |
| `TamaCore/EventKitCalendars.swift` | the only file that touches EventKit — read-only, and thin enough to have nothing to test |
| `TamaCore/SessionStore.swift` | all the logic: hook → per-session state → snapshot · `Timings` reads `~/.tamaclaude/timings.json` |
| `TamaCore/ToolMap.swift` | tool name → `VisualState`, overridable via `~/.tamaclaude/tools.json` |
| `TamaCore/Risk.swift` | what a glance may not approve — the board offers Allow only for things that can be undone |
| `TamaCore/HookInstaller.swift` | writes our hooks into `~/.claude/settings.json`, reads back their `Status`, and repairs a stale path at launch |
| `TamaCore/Text.swift` | strip to the board font's charset, shape Thai, then truncate |
| `TamaCore/ThaiShaper.swift` | the Thai cluster walk and glyph-variant choice (ADR-0008) |
| `TamaCore/ThaiTable.swift` | generated from `tools/thai.toml` — never edit |
| `TamaCore/SocketServer.swift` / `HookClient.swift` | Unix socket between `--hook` and the daemon |
| `TamaCore/BLETransport.swift` | CoreBluetooth central + auto-reconnect + board events |
| `TamaCore/WiFiProvisioning.swift` | the Wi-Fi commands and reports that ride the config/event characteristics |
| `TamaCore/LanFrame.swift` | the sealed frame on the wire: nonce, counter, greeting — pure, both directions |
| `TamaCore/LanKey.swift` | the 32-byte LAN key: where it lives, how it is fingerprinted |
| `TamaCore/LanTransport.swift` | the second path: find the board, connect, seal, resend |
| `TamaCore/Failover.swift` | when the second path may open (10 s grace) + the composite transport |
| `TamaCore/Usage{Reader,Writer}.swift` | the `.statusline-usage-cache` contract |
| `TamaCore/UsagePoll.swift` | `--usage-poll`: one claude.ai quota fetch, then exit |
| `TamaCore/UsagePoller.swift` | when to poll and what the last poll said — fed `tick(now:)`, owns no timer |
| `TamaCore/SessionStarter.swift` | when the app may open a session of its own — same shape, plus the guards and what locks it |
| `TamaCore/Spawn.swift` | spawning a child that answers to TCC for itself, not in the app's name (ADR-0013) |
| `TamaCore/ChildOutput.swift` | what a child process said, drained off its pipe without blocking it |
| `TamaCore/SecretFile.swift` | the one rule for a secret on disk: mode 600 from birth, refused if anyone else can read it |
| `TamaCore/SessionKeyFile.swift` | writes `~/.tamaclaude/session-key` through that rule |
| `TamaCore/SessionKeyState.swift` | what the settings window says under the key button — saved is not the same as accepted |
| `TamaCore/{Hook,Statusline}Installer.swift` | writes into `~/.claude/settings.json` |
| `TamaCore/Paths.swift` | the `~/.tamaclaude` paths + `Log` (`settings.json` belongs to `HookInstaller`) |
| `TamaCore/Daemon.swift` | wires it together + 1 s tick |
| `TamaCore/MenuBadge.swift` | what the menu bar icon knows: percent + pace position |
| `TamaCore/PanelText.swift` | what the foot of the popover says (board link, session rows, figure age) |
| `TamaCore/QuotaCard.swift` | what a quota card says: colour level, pace tick, reset line |
| `TamaCore/RefreshControl.swift` | the refresh button's discipline: cooldown, and when opening the panel polls |
| `tamaclaude/MenuBarApp.swift` | the menu bar app **is** the daemon (Bluetooth TCC is per-`.app`) |
| `tamaclaude/PreferencesWindowController.swift` | the settings window: General + Wi-Fi (the gear is down to Settings…/Quit) |
| `tamaclaude/PanelViewController.swift` | the popover: header + gear, the cards, the foot |
| `tamaclaude/QuotaCardView.swift` | how a quota card is drawn (bar, pace tick, palette) |
| `tamaclaude/MenuBadgeImage.swift` | how the menu bar icon is drawn (template vs red) |

### Invariants worth knowing before you touch things

- **The daemon must fit one MTU (500 bytes).** `Snapshot.encoded` shrinks `n` (cards) — body,
  then title, then drop cards — and never touches `s` (sessions). Cards dropped for any reason
  (2-card cap or MTU) are counted into the separate `m` key.
- **Encode with `sortedKeys` always.** Otherwise the "did it change?" comparison is always
  true and the board gets written every second.
- **Every pose stays ≥ 5 s (`Timings.minPose`).** Poses skipped during that hold are dropped,
  never queued — a queue makes the mascot narrate an ever-later past.
- **`--hook` must always exit 0**, daemon running or not. A broken hook breaks the user's
  session, which is far worse than a frozen screen.
- **Unknown ≠ zero** in the usage panel: `-1` means unknown, `0` is a real value. Within one
  window (same `resets_at`) the percentage only increases, so a lower value is stale — never
  overwrite a newer one. Foreign keys in the shared cache file (`PROFILE_NAME`, `COST_*`)
  must survive.
- **The LAN counter only ever goes up.** A repeated nonce in GCM destroys the confidentiality
  of both frames that used it, so `LanSealer` increments before sealing and the board rejects
  anything not strictly greater than what it has already accepted. The board tells the Mac
  where to continue from when it accepts the connection — neither side stores a counter file.
- **The `sessionKey` is a full-account credential.** File only (`~/.tamaclaude/session-key`,
  mode 600), never argv, never env, never logged, re-read every poll. Not the Keychain: the
  adhoc signature changes cdhash on every build, so the item would prompt on every upgrade.
  The Finnhub key (`~/.tamaclaude/finnhub-key`) follows the same rule through `SecretFile`
  even though it is a smaller credential — a rule with an exception is a rule nobody
  remembers which file it applies to. It rides in a query string, so a quote URL is itself a
  secret: log it through `StocksSource.describe` or not at all.
- **`-v` and `-psn_*` are not modes.** LaunchServices appends `-psn_0_12345`; an app that
  rejects unknown args dies on double-click.
- **`PageKind` is a three-way contract**: `Pages.swift`, `firmware/main/ct_pages.h`, and
  `tools/gen/pages.py`. The raw values travel on the wire, so reordering one side silently
  changes what every frame means. `tamatest` reads the real header to check.
- **The weather strip is five columns on all three sides.** `[weather] fc_cols` sizes the C
  arrays and `WeatherFrame.hourLimit` caps what the Mac sends; a sixth hour on the wire is
  bytes the board drops. `tamatest` reads `layout.h` to check, as it does for `PageKind`.
- **The 24h shape is 16 levels wide on all three sides.** `[crypto] spark_cols` sizes the C
  array, `CryptoFrame.sparkPoints` caps what the Mac sends, and a 17th point is bytes the board
  drops — same shape as the weather strip, and `tamatest` reads `layout.h` to check. **The
  stocks page draws a day-range band in that same box instead**: Finnhub's free tier has no
  history endpoint to plot, but every `/quote` carries today's `h`/`l`, so the box answers a
  different question — where the price stands in today's swing, not how it got there. It ships
  as one `"r"` key for the hero row only, and it is the first thing the squeeze drops.
  Everything else on the two pages stays pixel for pixel identical, because the user swipes
  between them.
- **A sparkline's baseline is its first point**, which is the 24h open. That is what makes the
  last bar agree with the printed percentage by construction rather than by luck, and it is why
  `fold` must keep both ends (`gen/trend.py` ↔ `ct_trend.c` ↔ `CryptoSource.spark`, all three
  using integer half-up rounding — Python's `round()` goes to even and would drift alone).
- **One frame per page, and each must fit `Wire.maxPayload` alone** (ADR-0003). A frame is a
  mascot `Snapshot` exactly when it has **no** `g` key; a frame with `pl` is page settings.
  `{"g":N,"x":1}` retires a page the user turned off — not sending it is not enough (ADR-0002).
- **The screen jumps back to the mascot on an event *id*, not on a state.** `Snapshot.attention`
  (`"a"`) counts sessions *entering* a needs-human state; the board jumps only when that number
  goes up. Deciding from the state itself yanks the screen back every snapshot for as long as a
  permission prompt sits unanswered, and never jumps for the second session that asks while the
  first is still waiting.
- **The top bar belongs to every page, and never repeats what a page already shows bigger.**
  It lives outside every page root (`ct_topbar.c` ↔ `gen/topbar.py`), built last so LVGL keeps
  it on top. The mascot page answers `ct_ui_shows_clock`/`ct_ui_shows_usage` for itself — the
  rule is in the bar, the fact is in the page. Get that backwards and the idle screen shows the
  same clock twice.
- **The footer belongs to every page too, but unevenly — and that split is the rule.**
  Its band, age line, and mini mascot are for the four data pages (`ct_footer.c` ↔
  `gen/footer.py`, built before the age label and the mini so both sit *on* the band and the
  mascot straddles its top rule). Its **page pips are for all five**, mascot page included:
  swiping is the device's only input and an affordance present on four screens out of five
  teaches nobody. The pips count only pages `in_rotation` — a pip for a page a swipe skips is
  a promise the board does not keep — and the current pip doubles as the rotation clock, so
  "which page" and "how long until it moves on its own" are one object, not two rows.
- **Page frames carry a data *age*, not a timestamp**, and the board counts on from there.
  A re-read with identical figures is still sent (the age is the difference); an age that is
  merely ticking is not a change.
- **LVGL's 48 KB pool is the budget that runs out first, and it fails as a hang, not an
  error.** `CONFIG_LV_MEM_SIZE_KILOBYTES` is separate from the ESP heap (which has ~110 KB
  free); building the five pages already spends ~88% of it, and the remainder is what draw
  tasks and layers are allocated from *while rendering*. Exhaust it and `lv_draw_dispatch`
  spins waiting for a unit that never frees: `lv_timer_handler` never returns, the task
  watchdog fires every 5 s, and the only symptom is a frozen screen — nothing points at
  memory. So **draw composite graphics in one custom-draw object per page, not one widget per
  shape** (`ct_mini`, `ct_footer`, and the weather scene all do this); nine widgets per page
  was enough to wedge the board. `main.c` prints `lv_mem` beside `heap` once a minute.
- **The `VisualState` enum is a contract with the firmware.** Adding or reordering it means
  changing `ct_model.c`/`ct_mascot.c` too. `tamatest` guards this.

### GATT

```
service  7A9B0001-4C1E-4B6D-9E2A-1D5C3F0A0001
state    ...0002   daemon writes the snapshot here (write with response)
config   ...0003   brightness + Wi-Fi commands + the LAN key — write requires an encrypted link
event    ...0004   board -> host: Wi-Fi scan results and link status (`BoardEvent`)
```

## The second path (LAN)

When BLE has been quiet for 10 s the daemon opens a TCP connection to the board on
port 7333 and sends the same snapshot, sealed with AES-256-GCM under a key it pushed
over the config characteristic. The board finds nothing on its own and **never talks to
claude.ai** — the `sessionKey` stays on the Mac. The reasons are in `LanFrame.swift`,
`LanTransport.swift` and `ct_lan.c`; the frame layout must match `ct_lan.c` byte for byte.

```
[4B len BE][12B nonce][ciphertext][16B tag]     nonce = 4 zero bytes + 8B BE counter
```

## Conventions

- Comments and design docs are in **Thai**; identifiers, commands, and error strings stay in
  English. Existing comments explain *why*, often citing what was tried and rejected — match
  that density rather than describing what the code does.
- Commit subjects are lowercase imperative with a conventional prefix (`feat:`, `fix:`) and
  read as a sentence about the visible effect, e.g. `feat: give the sky half the screen and
  cap cards at two`.
- The board font has no em dash — anything crossing BLE goes through `Text.swift` first.
  Thai does cross, but only shaped: `Text.fit` picks the glyph variants (ADR-0008), and it is
  the only door, so nothing else may hand a raw Thai string to the wire.

## Agent skills

### Issue tracker

Issues live as GitHub issues in `thaitop/tamaclaude`, managed with the `gh` CLI. External PRs
are not a triage surface. See `docs/agents/issue-tracker.md`.

### Triage labels

The five canonical roles use their default label strings (`needs-triage`, `needs-info`,
`ready-for-agent`, `ready-for-human`, `wontfix`). See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: `CONTEXT.md` + `docs/adr/` at the repo root. See `docs/agents/domain.md`.
