# skyro.wtf — GUI Shell

GUI-only Windows shell modeled on the local `CMakeToSln` ImGui loader. Borderless rounded frame, live blur, translucent purple glass, left tab rail, compact cards, smooth ImGui widgets, and a visible `assets/login.jpg` backdrop.

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
│   └── skyro_wtf.cpp                 # Win32 shell, DX11 renderer, ImGui pages
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

| Tab | Contents |
| --- | --- |
| Overview | Full-window artwork hero, session/render/artwork/build cards, quick actions. |
| Deep Link | Editable place ID, port, retries, and delay fields; live URI preview; clipboard copy. |
| Appearance | Four accent swatches, panel opacity, corner radius, blur toggle, image crop toggle. |
| Assets | Large artwork preview, path, dimensions, shader-view state, reload and copy actions. |
| About | Renderer, window, runtime, and build cards. |

Top bar drag area moves the window. The right-side `-` and `x` controls minimize and close. `Esc` closes the window.

## Layout details

- Window: `1000 x 640`, `WS_POPUP`, layered, rounded with a 24 px region.
- Frame: `SetLayeredWindowAttributes`, `DwmExtendFrameIntoClientArea`, rounded region, `SetWindowCompositionAttribute` blur.
- Rail: `190 px`, custom animated tab buttons with title and description on separate lines.
- Content: transparent child region over the image, `14 px` rounded cards, low-alpha ChildBg, accent border.
- Theme: Segoe UI body at 15 px, semibold headers at 19 px, small meta at 12 px.
- Rendering: DX11 swap chain with transparent clear, ImGui draw data over a GDI+-loaded JPEG texture.

## Runtime dependencies

The executable is linked statically. No `libgcc_s_seh-1.dll`, `libstdc++-6.dll`, or `libwinpthread-1.dll` ships with it. Imports are Windows system DLLs only: `KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `gdiplus.dll`, `msvcrt.dll`, `d3d11.dll`, `dxgi.dll`, `D3DCOMPILER_47.dll`, `dwmapi.dll`.

MSVC builds use `/MT`; MinGW builds use `-static -static-libgcc -static-libstdc++`.

## Notes

- This build is the GUI shell only. There is no listener, worker, Lua generator, or deep-link launch path in `src/skyro_wtf.cpp`.
- Deep Link values are preview state. `COPY URI` writes the preview string to the clipboard.
- `assets/login.jpg` is drawn full-window with aspect-preserving cover cropping. `Assets` can switch to stretched UVs.
- Lower `Panel opacity` in Appearance shows more of the artwork.
- If `assets/login.jpg` is missing, the background falls back to a purple gradient.
- Dear ImGui 1.90.9 is vendored under `vendor/imgui` with its MIT license; no external package manager is needed.
