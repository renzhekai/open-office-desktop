# ONLYOFFICE native header compile probe

This standalone CMake project compiles actual upstream declarations to one
Windows x64 MSVC object file. It has no executable, link step, mock definitions,
downloads, install steps, or editor runtime. It does not enable the application's
`OnlyOfficeEngineAdapter`.

The probe covers `QCefView`, `QAscApplicationManager`, CEF application startup
declarations, DOCX open calls, ordinary save, Save As command and path reply,
event payload fields, listener signatures, and event reference ownership methods.
Passing compilation proves that these expressions agree with the supplied
headers and compiler. It does **not** prove ABI compatibility with built native
libraries, CEF deployment, document opening, editing, saving, or file fidelity.

## Inputs

- Existing `upstream/DesktopEditors` checkout, including `desktop-sdk` and `core`.
- Windows x64 MSVC toolchain with Windows SDK.
- Qt 6.11.1 MSVC x64 kit, expected at `D:/Qt/6.11.1/msvc2022_64`.
- CMake 3.24 or newer and Ninja when using the commands below.

The selected local build environment is MSVC `14.44.35207`, initialized with
`D:\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat x64 -vcvars_ver=14.44.35207`.
The local tools are `D:\Qt\Tools\CMake_64\bin\cmake.exe` and
`D:\Qt\Tools\Ninja\ninja.exe`. If Ninja is not on PATH, set
`-DCMAKE_MAKE_PROGRAM=D:/Qt/Tools/Ninja/ninja.exe` when configuring.

Expected source revisions at preparation time:

| Repository | Commit |
| --- | --- |
| DesktopEditors | `daf8c6948148827201b2907ac1134d222fbfa7b4` |
| desktop-sdk | `ae6cf2a039355183c512fc314f7a9bd1a05a6a11` |
| core | `ed63e94de814ff1d4ea28cddfaba6939bc9df9ed` |

The two upstream include directories mirror `desktop-apps/win-linux/defaults.pri`.
`WIN32`, `WIN64`, and `NOMINMAX` mirror `core/Common/base.pri`; MSVC supplies
`_WIN32` and `_WIN64`. The probe remains a DLL consumer: it does not define
`DESKTOP_USE_DYNAMIC_LIBRARY_BUILDING` or `DESKTOP_NO_USE_DYNAMIC_LIBRARY`.
Qt's imported Widgets target supplies its transitive Core/Gui include and
compile settings. No Chromium CEF headers are directly needed by these public
wrapper declarations; compiling their implementation is a separate gate.

## Compile after the Qt kit is available

From an x64 MSVC developer PowerShell, with CMake and Ninja on PATH, in the
workspace root:

```powershell
cmake -S tests/integration/onlyoffice-native-api -B build/onlyoffice-native-api-msvc -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release -DCMAKE_PREFIX_PATH=D:/Qt/6.11.1/msvc2022_64
cmake --build build/onlyoffice-native-api-msvc --target onlyoffice_native_api_probe --verbose
```

For another existing checkout, also pass `-DONLYOFFICE_SOURCE_ROOT=<path>`.
Do not reuse the application's MinGW build directory for this MSVC check.
Keep configure/build output and the generated object file as evidence. There is
deliberately no CTest or application launch command for this compile-only gate.

## Observed result: 2026-11-01

The object target compiled successfully with CMake 3.30.5, the Ninja generator,
MSVC 19.44.35228.0 from toolset directory `14.44.35207`, and Qt 6.11.1 MSVC x64.
Configure and build both returned exit code 0. The native API signatures needed
no corrections after compilation. The compiler reported 16 C4100 warnings for
unused parameters in the unmodified upstream `applicationmanager_events.h`.

The first configure attempt used CMake's default Debug compiler self-test and
failed with C1902 (program database manager mismatch) under `/Zi /FS`. Repeating
configuration with `CMAKE_TRY_COMPILE_CONFIGURATION=Release` passed the real
compiler checks. This makes the Release probe reproducible; it does not repair
or validate that environment's Debug/PDB configuration. The optional missing
`WrapVulkanHeaders` diagnostic did not prevent Qt Widgets configuration.

Evidence under `build/onlyoffice-native-api-msvc/`:

- `configure-command.txt` and `configure.log`: initial command and failure.
- `configure-release-command.txt` and `configure-release.log`: passing configure.
- `build-command.txt` and `build.log`: actual single-object compilation.
- `object-symbols.log`: `dumpbin /symbols` output. The object retains unresolved
  DLL-import references to the real `QCefView::Create`, `QCefView::GetCefView`,
  `CCefViewEditor::OpenLocalFile`, `CCefViewEditor::GetFileFormat`, `CCefView::Apply`,
  and `CAscApplicationManager::Apply`. No substitute function definitions were
  linked to satisfy these imports.
- `CMakeFiles/onlyoffice_native_api_probe.dir/native_api_probe.cpp.obj`: 72,864
  bytes, SHA-256 `CEF8BF606D2423C8543CB7DB1D2F181888CD61E85B828FE4240532C0DA4F6A09`.

No ONLYOFFICE native libraries were linked and no editor was launched. CMake's
ordinary compiler checks produced their own trivial toolchain-check binaries;
those are not the application or an editor integration test.

## Runtime bridge constraints still requiring implementation and validation

- `OpenLocalFile` returns `void`; only a matching `doc:onready` event can advance
  the open request to ready. A browser page-load event is insufficient.
- `ASC_MENU_EVENT_TYPE_CEF_EXECUTE_COMMAND` uses `CAscExecCommand` (`Param`),
  whereas outgoing `ASC_MENU_EVENT_TYPE_CEF_EDITOR_EXECUTE_COMMAND` uses
  `CAscEditorExecuteCommand` (`Params`). No verified `saveAs` command string for
  the latter is assumed by this probe.
- The upstream macOS menu's Save As call uses `CAscExecCommandJS`, command
  `file:saveas`, frame `frameEditor`, and event `CEF_EXECUTE_COMMAND_JS`
  (`desktop-apps/macos/ONLYOFFICE/AppDelegate.mm:699`). The probe checks that
  declaration usage; its Windows renderer behavior still requires validation.
- The Save As path reply is `CAscLocalSaveFileDialog` with `Id`, `Path`, and
  `FileType`, sent via manager `Apply` for routing to the matching view.
- `DOCUMENT_NAME` and `ONSAVE` must be correlated to the view and pending
  operation. `ONSAVE.IsCancel` does not distinguish user cancellation from all
  conversion failures/export cases. It must not be treated as a rich save result.
- Preserve the upstream application's existing listener/common event handling.
  Copy callback data before releasing its event and marshal signals to the Qt
  object's thread. Do not retain borrowed payload pointers across queued calls.
- Native views, allowed-close events, CEF subprocess/message-loop handling,
  helper executable, converter, editor assets and library deployment are still
  separate requirements. This object file cannot validate their lifecycle.
