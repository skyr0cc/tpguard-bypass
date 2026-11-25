# skyro.wtf — Deep Link Server Joiner

Purple/white Win32 GUI launcher for Roblox place `109983668079237`. It starts a local job-ID listener, fires the `roblox://experiences/start` deep link, and generates the auto-execute Lua that feeds intercepted job IDs back into the launcher.

![skyro.wtf background](assets/login.jpg)

## Layout

```
.
├── assets/
│   └── login.jpg          # window background, loaded at runtime
├── build/                 # generated binaries, ignored by git
│   ├── skyro_wtf.exe
│   └── assets/login.jpg
├── src/
│   └── skyro_wtf.cpp      # GUI, HTTP listener, retry worker, Lua generator
├── tools/
│   ├── tpguard.sh         # original shell proof of concept
│   └── tpguard.bat        # original batch proof of concept
├── .gitignore
├── CMakeLists.txt
├── Makefile
├── build.bat              # MSVC one-shot build
└── build.sh               # MinGW cross-build wrapper
```

## Build

MSVC, from a Developer Command Prompt:

```bat
build.bat
```

MinGW cross-build on Linux:

```bash
make
# or
./build.sh
```

CMake, any generator:

```bash
cmake -S . -B out -A x64
cmake --build out --config Release
```

Each build copies `assets/` next to the executable. The exe resolves `assets\login.jpg` from its own module path, so keep the folder beside the binary when moving it.

## Controls

| Control | Behaviour |
| --- | --- |
| `INTERCEPTOR` toggle | Gates job intake and deep-link retries. Off means captured jobs are dropped. |
| `START SERVER` | Binds `127.0.0.1:<port>`, starts the worker, fires `roblox://experiences/start?placeId=<place>`. Becomes `STOP SERVER`. |
| `COPY SCRIPT` | Copies the generated Lua to the clipboard. |
| `PLACE ID` | Target place, default `109983668079237`. |
| `PORT` | Local listener port, default `6969`. Read on server start. |
| `RETRIES` | Deep-link attempts per captured job, default `10`. |
| `DELAY (MS)` | Milliseconds between attempts, default `1500`. |

The Lua text box regenerates on every field change. Place ID, port, retries, and delay in the script always match the GUI.

## How it works

1. `START SERVER` calls `ShellExecuteW` with `roblox://experiences/start?placeId=<place>`, the same deep link the original `tpguard` proof of concept used. A Winsock listener then binds `127.0.0.1:<port>` on a background thread.
2. The generated Lua is pasted into auto execute. It hooks `TeleportService.TeleportToPlaceInstance`, `TeleportService.TeleportToPlaceInstanceAsync`, and the game metatable `__namecall`. Any string matching the UUID shape of a Roblox job ID is captured.
3. Captured job IDs are deduplicated in a 128-entry cache, then POSTed as JSON to `http://127.0.0.1:<port>/job`. Executor HTTP fallbacks are tried in order: `syn.request`, `http_request`, `request`.
4. The listener parses `jobId` out of the request body, returns `200 {"ok":true}`, and pushes the ID to a bounded work queue. If the toggle is off, the ID is discarded and the status bar says so.
5. The worker builds `roblox://experiences/start?placeId=<place>&gameInstanceId=<jobId>` and fires it through `ShellExecuteW`. It repeats up to `RETRIES` times with `DELAY (MS)` between attempts. The Lua side runs the same retry count through `TeleportToPlaceInstance` and exits early once `game.JobId` matches the captured ID.
6. Status updates stream into the bottom bar from the listener and worker threads through a mutex-protected string and a 200 ms repaint timer. Shutdown stops the producer, wakes and joins the worker, closes the listener, then releases GDI+ and GUI resources.

## Lua shape

Generated script constants:

```lua
local PLACE_ID = 109983668079237
local PORT = 6969
local RETRIES = 10
local DELAY = 1.5000
local ENDPOINT = "http://127.0.0.1:" .. PORT .. "/job"
```

Executor hook coverage:

- `hookfunction` when available, direct method assignment otherwise.
- `hookmetamethod` on `__namecall` with `getrawmetatable` and `setreadonly` guards.
- All hook setup wrapped in `pcall`, so auto execute keeps running if one hook is unavailable.
- No varargs in the generated script, which keeps it portable across the executor Lua variants that lack `table.pack`.

## Notes

- Port changes require `STOP SERVER` then `START SERVER`; the listener reads the port once at bind time.
- Retries and delay are clamped: retries `1..1000`, delay `50..60000` ms, port `1..65535`.
- If the Lua runs on a different machine than the exe, replace `127.0.0.1` in `ENDPOINT` with the host running the listener.
- The background image is drawn with GDI+ and covered by a translucent purple veil so the controls stay readable. If `assets/login.jpg` is missing, the window falls back to a solid purple background.
