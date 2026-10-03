# Recon (M0)

Target: PC Steam build, app 6090, build id 251748 (`appmanifest_6090.acf`). Copied to `orig/pc/` on
2026-10-03; hashes in `docs/manifest.md`. All addresses below are for the executable hash they name.

## Recon table

| Question | Answer | Evidence |
| --- | --- | --- |
| Executable hash and size | `GameData/game.exe`, 2,441,216 bytes, SHA-256 `960df24c70d788a34946fa5984685ef69bf2c0cacf04a275769472ce302f5b45` (Steam-wrapped) | `tools/pe_info.py`; `docs/manifest.md` |
| DRM or packer | **SteamStub (Steam DRM wrapper).** Retail SecuROM not present. | `.bind` section 0x56000 bytes at RVA 0x24f000; entry point 0x24f2ed lies in `.bind`; `.text` entropy 8.00 (encrypted). Same wrapper on `ArmedAndDangerous.exe` and `testapp.exe`. |
| Compiler and build number | VC7.x. DIE: C runtime from VC++ 13.10 (VS .NET 2003), libcmt; PE linker field says 7.0 (conflict, open). No Rich header. PDB `c:\dev\code\game\Win32-Final\Game.pdb`. | DIE 3.21; PE optional header; debug directory; step 3 |
| Link-time code generation | Unknown; no Rich header to read it from | Needs the register-argument check in Ghidra |
| C++ type information present | **No RTTI** | 0 `.?AV`/`.?AU` names in the unwrapped file |
| Functions: total / library / game | Unknown | Needs Ghidra |
| Static libraries identified | Havok physics (proprietary), zlib 1.1.4, IJG libjpeg (6b, guess). DivX via DLL. | Version banners and `hk*.cpp` paths, see step 8 |
| Archive format | `.big`, first 4 bytes `DE C0 AD DE` (0xDEADC0DE little-endian). Relation to Giants `.gzp` untested. | `lev_1_m1/lev_1_m1_big01_text.big` header |
| Script interpreter present | Unknown | |
| Named functions recovered from other builds | Not started (Xbox builds need ASK) | |
| Windowed mode and skip-intro switches | Unknown | |
| Spike: success rate and cost per function | Not started | |

## Step 1: inventory

- 2,152 files, 4,579,741,779 bytes.
- Main executable `GameData/game.exe` (built 2004-01-22 17:41:10 UTC per PE timestamp). The root
  `ArmedAndDangerous.exe` (516,096 bytes, built 2003-11-13) is a launcher; `testapp.exe` is byte-size identical
  to it.
- Game DLLs, all unwrapped: `GameData/FileParser.dll` (linker 6.0, 2003-04-16), `DivxDecoder.dll`,
  `DivxMediaLib.dll`.
- Installer leftovers under `Install/` (LucasArts `LecSetup.dll`, `syscheck.exe`) and a DirectX redistributable
  under `directx/` (April 2007 XACT cabs, so Steam-era, not 2003).

File types by extension:

| Extension | Count | MB | First bytes / note |
| --- | ---: | ---: | --- |
| `.big` | 325 | 2,131 | `DE C0 AD DE` in `*_text.big`; `*_txtr.big` differs, check per kind |
| `.pcv` | 26 | 1,467 | `RIFF` (movies; DivX DLLs suggest AVI/DivX) |
| `.raw` | 1,615 | 654 | No header; audio streams under `streams/` |
| `.cab` | 113 | 87 | DirectX redistributable only |
| `.jit` | 25 | 20 | One per level folder; small integer header, unknown |
| `.tga` | 12 | 0.4 | Plain TGA |
| `.dat` | 4 | 0.3 | `shaders.dat`, `lang.dat`, saves |

Level folders: `lev_1_m1` ... `lev_5_m5`, `lev_bonus_m1..m3`, `lev_shell` (likely the front-end menu).

## Step 2: DRM gate

**Result: wrapped. Stopped and asked (2026-10-03).** Decision in `docs/decision.md`: the human unwraps their
own `game.exe` with Steamless and supplies the result; agents do not unpack. Data files are not affected by
the Steam wrapper (checked headers and entropy per type above).

Unwrapped file supplied by the human (Steamless, 2026-10-03): `orig/pc/GameData/game.unpacked.exe`,
2,088,960 bytes, SHA-256 `f6a690a4a5226d0c2090596d37cd9e5ec287c28cf3fa6500e8ee6ec1b47efd23`. Entry point now
RVA 0x139238 (VA 0x539238) in `.text`; `.text` entropy 6.59; `.bind` removed; other section headers and the
PE timestamp unchanged. **All addresses in this project refer to this hash.**

## Step 3: compiler

- Linker 7.0 = Visual C++ .NET 2002 toolchain (guess; confirm by CRT function signatures in Ghidra).
- **No Rich header** in either the wrapped or unwrapped file (`pefile.parse_rich_header()` returns None).
  VC7 linkers emit one, so it was probably stripped. Compiler build numbers must come from CRT code instead.
- `mscoree.dll` appears as a string: the VC7.x C runtime's `__crtExitProcess` looks it up. Consistent with VC7.
- **Detect It Easy 3.21** on the unpacked file: `Compiler: Microsoft Visual C/C++(13.10)[libcmt]`, i.e. the
  VS .NET 2003 (7.1) C runtime, statically linked, multithreaded. This conflicts with the PE header's linker
  version 7.0 (VS .NET 2002 would read 7.0, VS .NET 2003 would read 7.10). Open question; it matters only for
  Approach A.
- DIE also reports `Protector: PELock(2.X)` on the unpacked file. **False positive**: the entry point at
  0x539238 is the stock VC7.x `WinMainCRTStartup` (`push 0x60; push 0x56bc98; call 0x53df64` (`__SEH_prolog`);
  `mov edi, 0x94` (sizeof `OSVERSIONINFOA`); `call 0x539f60` (`_alloca_probe`); `call [0x56b0f0]`
  (`GetVersionExA`)). On the wrapped file DIE reports no protector at all (it does not recognise SteamStub).
- `FileParser.dll`: DIE reports VC++ 6.0 (compiler 12.20.9044, linker 6.00.8447). A separate toolchain.
- **CodeView debug record**: PDB path `c:\dev\code\game\Win32-Final\Game.pdb`, age 2, timestamp equal to the PE
  timestamp. The build configuration was named `Win32-Final`. The PDB itself is not shipped.

## Step 5: imports

| DLL | Imports | Notes |
| --- | ---: | --- |
| KERNEL32 | 96 | Timers: `GetTickCount`, `QueryPerformanceCounter`/`Frequency` (time-control hook targets) |
| USER32 | 30 | |
| DivxMediaLib | 14 | Movie playback (`OpenDivxFile`, `DivxDoFrame`, ...) |
| d3d9 | 1 | `Direct3DCreate9`; `d3d9d.dll` also named as a string (debug runtime switch?) |
| DINPUT8 | 1 | `DirectInput8Create` |
| DSOUND | 1 | ordinal 11 (`DirectSoundCreate8`) |
| ADVAPI32 | 3 | Registry reads (`RegOpenKeyA`, `RegQueryValueExA`, `RegCloseKey`) |
| GDI32 | 1 | `GetStockObject` |

No `d3dx9_*.dll` import (no D3DX dependency for Approach B). `FileParser.dll` is **not** imported by
`game.exe`; check whether it is loaded with `LoadLibraryA` or unused.

## Step 6: strings and type information

- 8,998 printable strings (length 5 or more).
- **No C++ RTTI** (`.?AV`/`.?AU` names: 0). Class names must come from other evidence.
- 14 source-path strings. Planet Moon modules use a `pms_` prefix (`pms_audio\pms_audio.cpp`,
  `pms_audio\pms_pcaudio.cpp`, `pms_input\pms_input.cpp`, `pms_movie\pms_movie_divx_pc.cpp`), plus
  `filesys\filesys.cpp`, `gfx\d3d\vertexshader.cpp`, `keycode.cpp`.
- A reflection-like string `DATA(PlayerWeapon::m_weaponDescriptor)` suggests a data-description macro system
  that may give class and member names. Investigate.

## Step 8: static libraries (from banners)

| Library | Version | Evidence | Consequence |
| --- | --- | --- | --- |
| Havok physics | 2.x era (guess) | `hkWorld.cpp`, `hkCollidable.cpp`, `hkbase/config/hkConfigMemoryStats.h` | Proprietary, no public source. Identify and fence off; do not decompile by hand. ASK before any decision on it. |
| zlib | 1.1.4 | `deflate 1.1.4 Copyright 1995-2002`, `inflate 1.1.4` | Replace with public zlib 1.1.4 source |
| IJG libjpeg | 6b (guess) | `Copyright (C) 1998, Thomas G. Lane`, JFIF strings | Replace with public libjpeg 6b source |

## Steps 4, 7, 9 to 12

Not started. Step 7 (first Ghidra pass) waits for Ghidra.
