# skyro.wtf — Deep Link GUI

Small GUI-only Windows shell with a locked place ID, editable delay and rejoin count, and a visible `assets/login.jpg` backdrop.

![skyro.wtf background](assets/login.jpg)

## Layout

```
.
├── assets/
│   └── login.jpg                     # full-window background, loaded at runtime
├── build/                            # generated binaries, ignored by git
│   ├── skyro_wtf.exe
│   └── assets/login.jpg
├── src/
│   └── skyro_wtf.cpp                 # Win32 shell, DX11 renderer, ImGui UI
├── tools/
│   ├── tpguard.sh                    # original shell proof of concept
│   └── tpguard.bat                   # original batch proof of concept
├── vendor/
│   └── imgui/                        # Dear ImGui 1.90.9 + Win32/DX11 backends
├── .gitignore
├── CMakeLists.txt
├── Makefile
├── build.bat                         # MSVC one-shot build
└── build.sh                          # MinGW cross-build wrapper
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

## Interface

Window size is `600 x 400`. Top bar drag area moves the window, and `x` closes it. `Esc` also closes it.

| Area | Behaviour |
| --- | --- |
| Deep Link header | Title and one-line description. |
| Target | Shows fixed Place ID `109983668079237`, marked locked. Not editable. |
| Timing | Editable `Delay ms` and `Rejoin`. Values clamp to `50..60000` ms and `1..1000`. |
| Rejoin button | Full-width button with a short accent pulse on click. GUI state only. |

There is no URL preview field and no place ID field.

## Layout details

- Window: `600 x 400`, `WS_POPUP`, layered, rounded with a 22 px region.
- Frame: `SetLayeredWindowAttributes`, `DwmExtendFrameIntoClientArea`, rounded region, `SetWindowCompositionAttribute` blur.
- Content: two translucent cards over the artwork, fixed `14 px` rounding, low-alpha ChildBg, accent border.
- Theme: Segoe UI body at 15 px, semibold header at 19 px, small meta at 12 px.
- Rendering: DX11 swap chain with transparent clear, ImGui draw data over a GDI+-loaded JPEG texture.

## Runtime dependencies

The executable is linked statically. No `libgcc_s_seh-1.dll`, `libstdc++-6.dll`, or `libwinpthread-1.dll` ships with it. Imports are Windows system DLLs only: `KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `gdiplus.dll`, `msvcrt.dll`, `d3d11.dll`, `dxgi.dll`, `D3DCOMPILER_47.dll`, `dwmapi.dll`.

MSVC builds use `/MT`; MinGW builds use `-static -static-libgcc -static-libstdc++`.

## Notes

- This build is the GUI only. There is no listener, worker, Lua generator, or deep-link launch path in `src/skyro_wtf.cpp`.
- `assets/login.jpg` is drawn full-window with aspect-preserving cover cropping.
- If `assets/login.jpg` is missing, the background falls back to a purple gradient.
- Dear ImGui 1.90.9 is vendored under `vendor/imgui` with its MIT license; no external package manager is needed.
