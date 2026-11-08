# Qt 6 native ONLYOFFICE wrappers

This isolated CMake project compiles the fixed upstream `videoplayer` and
`qtascdocumentscore` source lists against Qt 6.11.1 MSVC x64. Upstream sources,
resources, and license headers remain intact. It does not modify the native
engine or execute the upstream build-tools pipeline.

Source commit: `desktop-sdk` `ae6cf2a039355183c512fc314f7a9bd1a05a6a11`;
`core` `ed63e94de814ff1d4ea28cddfaba6939bc9df9ed`.

The initial targets `videoplayer_objects` and `qtascdocumentscore_objects`
compile real implementation sources, Qt MOC output and video resources. Set
`ONLYOFFICE_NATIVE_IMPORT_DIR` only after real matching `kernel.lib`,
`graphics.lib` and `ascdocumentscore.lib` have been built or generated from
verified native DLL exports. Then the DLL targets become available:
`openoffice_qt6_videoplayer` and `openoffice_qt6_documentscore`. Their distinct
filenames preserve the original runtime cache's Qt 5 wrappers.

## Reviewed build inputs

- `videoplayerlib/videoplayerlib.pro` → `videoplayerlib.pri` →
  `core/Common/base.pri` and `videoplayerlib_deps.pri`.
- `lib/qt_wrapper/qtascdocumentscore.pro` → `core/Common/base.pri` and
  `videoplayerlib_deps.pri`.
- Default multimedia path uses Qt Multimedia and MultimediaWidgets. The libVLC
  branch is not selected, so its separate external build rules are not invoked.
- `base.pri` contains a Windows `system(powershell ... Get-Date)` year query,
  compiler/date queries on other platforms, and Apple-only post-link/Swift
  compiler commands. Its default output paths also point into the upstream
  checkout. This project uses explicit CMake lists and isolated output paths
  instead of evaluating these qmake rules.
- Required Windows definitions include `WIN32`, `WIN64`, `NOMINMAX`,
  `WINDOWS_IGNORE_PACKING_MISMATCH`, and upstream's custom `QT_VERSION_6`.
  `BUILD_VIDEO_LIBRARY` applies to video sources;
  `DESKTOP_USE_DYNAMIC_LIBRARY_BUILDING` applies to the Qt wrapper sources.
- Native dependencies mirror qmake: video → kernel; Qt wrapper → kernel,
  graphics, ascdocumentscore, videoplayer and user32. Qt dependencies are Core,
  Gui, Widgets, PrintSupport, Multimedia and MultimediaWidgets.

Two isolated CMake integration accommodations were required by actual builds:

- Disable source-file AUTOMOC scanning for `qascvideoview.cpp`: its local
  `Q_OBJECT` exists only in the inactive `USE_VLC_LIBRARY_VIDEO` branch. The
  actual public `qascvideoview.h` is still explicitly scanned and compiled.
- Preinclude `qt6_legacy_agg_compat.h` in `qascprinter.cpp` and `qrenderer.cpp`.
  The pinned AGG `agg_basics.h:210` uses the obsolete `register` specifier,
  rejected by strict MSVC C++17. The preinclude temporarily removes that hint
  only while loading the real AGG header and immediately restores macro state.
  No upstream file, type layout, method signature, or algorithm is rewritten.

## Build

Use the x64 MSVC `14.44.35207` developer environment, CMake 3.30.5 and Ninja.
From the workspace root:

```powershell
cmake -S tests/integration/native-qt-wrapper -B build/native-onlyoffice-qt6 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release -DCMAKE_PREFIX_PATH=D:/Qt/6.11.1/msvc2022_64 -DCMAKE_MAKE_PROGRAM=D:/Qt/Tools/Ninja/ninja.exe
cmake --build build/native-onlyoffice-qt6 --target videoplayer_objects qtascdocumentscore_objects --verbose
```

After verified native import libraries are available, repeat configure with
`-DONLYOFFICE_NATIVE_IMPORT_DIR=<real-import-directory>` and build the two DLL
targets. The outputs are isolated under the build directory's `bin/` and
`lib/` directories. No install or application-launch target is provided.

`generate_import_library.py` prepares one real import library in a new isolated
directory. Run it only after the complete official release ZIP passes its
independent SHA-256 check. It requires the extracted DLL, its independently
verified hash, matching PE metadata JSON, and the selected MSVC `lib.exe` path.
It checks the DLL hash, re-parses actual PE export/section bytes, compares the
metadata, and marks exports in non-executable sections as `DATA` (including
vtable exports). Unsupported forwarders, unnamed exports, or mismatches fail
explicitly. It never loads the DLL or supplies replacement implementations.

Use separate new directories `build/native-qt-wrapper-imports/kernel/`,
`graphics/`, and `ascdocumentscore/`. The CMake import-directory option accepts
that common parent as well as a flat directory of the three import libraries.
Each generation records the exact `lib.exe` argument array, real export list,
classification, and input/output hashes in `<name>.import-audit.json`.
The generator has now run against all three verified native DLLs. It also uses
`dumpbin /LINKERMEMBER:2` to verify every expected `__imp_` symbol after `lib.exe`
returns. This caught and corrected an initial `.def` formatting error: quoting
decorated export names caused MSVC to preserve the quote characters in symbols.
The validated whitelist permits emitting the exact unquoted export spelling.

Compilation and DLL linkage are separate gates. Neither proves CEF startup,
media playback, ABI behavior during execution, or document round-trip fidelity.

## Observed object-build result: 2026-11-01

Configuration and final object compilation returned exit code 0 with Qt 6.11.1,
MSVC 19.44.35228.0 (toolset folder 14.44.35207), CMake 3.30.5 and Ninja.
`videoplayer_objects` produced 12 objects (10 implementation sources, MOC, RCC);
`qtascdocumentscore_objects` produced 8 (7 implementation sources and MOC).

Evidence lives under `build/native-onlyoffice-qt6/`:

- `configure-command.txt`, `configure.log`: successful isolated configuration.
- `objects-command.txt`: exact developer-environment and compile command.
- `objects-build.log`: initial AUTOMOC failure on the inactive VLC branch.
- `objects-build-2.log`: real C++17 AGG `register` error after resolving MOC.
- `objects-build-3.log`: successful final compilation with the localized shim.
- `object-manifest.json`: paths, sizes and SHA-256 for all 20 produced objects.

## Observed DLL-link result: 2026-11-01

After full official ZIP SHA-256 and member CRC verification, the three native
import libraries were generated from real PE exports, with exact symbol checks:

| Library | Exported symbols | DATA entries |
| --- | ---: | ---: |
| kernel | 567 | 9 |
| graphics | 1544 | 44 |
| ascdocumentscore | 200 | 10 |

Both Qt6 wrapper DLL targets then linked successfully (exit 0):

- `bin/openoffice_qt6_videoplayer.dll`: 230,912 bytes.
- `bin/openoffice_qt6_documentscore.dll`: 135,168 bytes.
- Their matching MSVC import libraries are under `lib/`.

`dll-build-2.log` records the successful link. `dll-hashes.json` records the
output hashes. The two `*.dependencies.log` files show Qt6 imports and the
documents wrapper importing the independently named Qt6 video wrapper; there
are no Qt5 or libVLC imports in these newly built wrappers. The first failed
link remains in `dll-build.log`, with its original malformed import libraries
preserved under `build/native-qt-wrapper-imports-quoted-symbols/` for diagnosis.
Final valid imports and per-library audit records remain at the documented
`build/native-qt-wrapper-imports/<name>/` paths.

No native core or Qt wrapper runtime has been launched by this build. Successful
DLL linkage still does not establish editor startup or C++ lifecycle behavior.

Media behavior still needs separate Qt6 validation: the upstream sources retain
string-based `videoAvailableChanged(bool)` and `sourceChanged(QMediaContent)`
connections, while Qt6 uses `hasVideoChanged(bool)` and `sourceChanged(QUrl)`.
No `QAudioOutput` setup was found in the reviewed source path. These findings do
not prevent object compilation and must not be mistaken for working playback.
