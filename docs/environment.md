# Environment

Windows 11 Home 10.0.26200, x64. Set up 2026-10-03. Record every tool version here when it changes.

## Installed

| Tool | Version | Location / how |
| --- | --- | --- |
| Git | 2.55.0.windows.5 | winget `Git.Git` |
| GitHub CLI | 2.102.0 | winget `GitHub.cli` (not authenticated; ASK before any push) |
| Python | 3.12.10 | winget `Python.Python.3.12`; project venv `.venv/` (ignored) |
| Python packages | pefile 2024.8.26, lief 1.0.0, capstone 5.0.9, pyghidra 3.1.0, frida 17.22.0 / frida-tools 14.10.4, pytest 9.1.1, mss 10.2.0 | `.venv\Scripts\python.exe -m pip install ...` |
| CMake | 4.4.3 | winget `Kitware.CMake` |
| Ninja | 1.13.2 | winget `Ninja-build.Ninja` |
| LLVM / clang | 23.1.2 | winget `LLVM.LLVM`, `C:\Program Files\LLVM\bin` (**not on PATH**; call by full path) |
| JDK | Temurin 21.0.12.1 | winget `EclipseAdoptium.Temurin.21.JDK`, `C:\Program Files\Eclipse Adoptium\jdk-21.0.12.101-hotspot` |
| VS 2022 Build Tools | MSVC toolset 14.44.35207, Windows SDK 10.0.26100.0 | winget, VCTools workload, `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools` |
| 7-Zip | 26.03 | pre-installed |
| Ghidra | 12.1.4 (2026-09-21) | `C:\Users\nikhi\tools\ghidra_12.1.4_PUBLIC`; user env `GHIDRA_INSTALL_DIR` set |
| Detect It Easy | 3.21 | `C:\Users\nikhi\tools\die\die\diec.exe` (CLI) |
| x64dbg | snapshot 2026-05-27 | `C:\Users\nikhi\tools\x64dbg\release\x32\x32dbg.exe` (use x32 for the game) |
| apitrace | 14.0 win32 | `C:\Users\nikhi\tools\apitrace-14.0-win32` |

## Not installed, and why

| Tool | Reason |
| --- | --- |
| GhidraMCP (LaurieWired) | Latest release 1.4 (2025-06) targets Ghidra 11.x. Using `tools/gh.py` over pyghidra instead. |
| BinDiff 8 / BinExport | BinExport's Ghidra extension targets 11.0.3. Only needed for Xbox cross-build matching, which awaits an ASK. Ghidra Version Tracking is the fallback. |
| Approach A compiler (VC7.x), Detours/MinHook, ASI loader | Approach-specific; installed only when section 6 picks the approach. Old Microsoft installers need an ASK. |

## Setup check (handover s3)

| Check | Result |
| --- | --- |
| `cl`, `cmake`, `ninja`, `python`, `git`, `java` answer from a fresh x86 Developer PowerShell | Pass (2026-10-03) |
| Ghidra headless analyses an executable through pyghidra | Pass: `tools/gh.py import` analysed `game.unpacked.exe` in 2 min 05 s |
| Original game launches, reaches the menu and exits | **Partial**: `game.unpacked.exe` launches without Steam, runs fullscreen 640x480, shows the intro text and plays the intro movie (DivX OK). Menu not yet reached (intro needs input to skip); closed after 65 s. |

## Developer shell

```powershell
$vs = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64'
```

Use `-arch=x86` for anything loaded into the 32-bit game.
