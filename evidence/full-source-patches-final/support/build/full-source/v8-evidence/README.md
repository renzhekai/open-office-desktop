# V8 source build evidence

This directory records the isolated Windows x64 build of V8 8.9.255.25 at
`a2d8eca22ad478a95ca67bb2421a9a2535113925`. It is a source compilation, not a
copy of V8 from CEF or an ONLYOFFICE binary release.

Completed on 2026-11-02: the monolith build, dependency-record rebuild,
regeneration followed by an incremental no-work build, and the executable
JavaScript/Intl/WebAssembly smoke all passed. The stable monolith is
218,458,654 bytes with SHA256
`5a481cbed294ebfd4c9c378edc978c0c1f652dbd1ac61be9cf01ea59ab2ed0c4`.
Its ICU 68.1 data is 10,413,552 bytes with SHA256
`c5f9a62ecc982a52777c96d96aee791373f0a132c4ec291891d76ddcf2ec2fa9`.

The work is driven by `scripts/full_source_v8.py`. The source is under
`build/full-source/DesktopEditors/core/Common/3dParty/v8_89/v8`; the existing
`C:/Users/18268/AppData/Local/Temp/oo10-full-src` alias resolves to
`build/full-source`. Commands use that alias to avoid spaces in old build
scripts. The alias is checked before build execution.

## Inputs and approved tools

- `sources.json`: fixed Git revisions from the V8 DEPS file, fetched without
  executing gclient hooks or depot_tools bootstrap. `deps-audit.json` is the
  initial no-hooks review snapshot. The sole later hook action explicitly
  reviewed and run is `build/util/lastchange.py`; see its dedicated log.
- `tools.json`: official python.org 3.11.8 embedded ZIP and pinned Chromium
  CIPD client provenance, hashes, sizes, and local Python initialization
  adjustment. Python's archive digest was compared with official HTTPS
  Sigstore metadata; the signature chain was not separately verified.
- `gn.json` and `gn-cipd.json`: pinned official GN package and CIPD receipt.
- Existing tools reused: MSVC **14.44.35207**, Windows SDK **10.0.26100.0**,
  and `D:/Qt/Tools/Ninja/ninja.exe`. SDK/toolset choices are explicit rather
  than whichever version the current VS installation selects by default.
- Python 2.7, Clang binaries, pip packages, system Git configuration, global
  PATH changes, and system-wide installs are not part of this build.

## Reproduction

Run from the project root, preserving the existing isolated checkout:

```powershell
python scripts/full_source_v8.py sync --group all
python scripts/full_source_v8.py python-metadata
python scripts/full_source_v8.py tools
python scripts/full_source_v8.py gn
python scripts/full_source_v8.py prepare
python scripts/full_source_v8.py generate
python scripts/full_source_v8.py build
python scripts/full_source_v8.py smoke
```

Network acquisition uses official origins, normal TLS verification, fixed
source commits, and pinned binary digests/versions. The script fails rather
than replacing a nonempty unrecognized source directory or mismatched tool.
`patches.json` records the isolated legacy-build adaptations. Upstream source
checkouts outside this isolated build are not edited.

The build uses x64 Release, `/MD`, pointer compression, a static monolith,
embedded V8 startup snapshots, and the pinned V8 ICU dependency with external
ICU data. `icudtl.dat` in the V8 output must accompany an embedding executable
that calls `InitializeICUDefaultLocation`. Core ICU 74 or CEF data is not a
substitute for this file.

## Localized compiler dependency correction

The installed MSVC emits CP936 Chinese `/showIncludes` output even with
`VSLANG=1033`. `localization-probe` verifies the actual prefix using a real
header and a separate tiny Ninja compile. `generate` and `build` apply the
verified raw-byte prefix to generated `cc` and `cxx` rules; the resource
compiler rule is unchanged. Always use this driver after GN regeneration.

The first build began before this correction. The one-time `repair-deps`
action preserves its `.ninja_deps` file under a unique backup name, then
recompiles with four Ninja jobs so real header dependencies are populated.
It does not delete source, objects, libraries, or other core outputs.
`dependency-rebuild.json` records the migration and real object checks;
its status must be `passed` before declaring this correction complete.

## Completion evidence

- `build-result.json`: actual compiler exit code and monolith path/size/SHA256.
- `dependency-rebuild.json`: repaired dependency checks for real V8 objects.
- `monolith-smoke.json`: actual executable linked only to our monolith plus
  Windows system libraries, verifying version, JavaScript, ICU-backed Intl,
  and WebAssembly. It also records matching ICU data and executable hashes.
- `incremental-no-work.log`: a subsequent Ninja dry run has no pending work.
- `ninja-build.log` and dedicated smoke logs: execution details.

Presence of this README is not a passing result. Use the actual result JSON
and logs above; absent results mean the corresponding phase is unfinished.
