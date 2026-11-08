"""Apply isolated Boost 1.72/MSVC 14.44 compatibility settings."""
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / 'build/full-source'
ALIAS = Path(r'C:\Users\18268\AppData\Local\Temp\oo10-full-src')
BASE = WORK / 'DesktopEditors/core/Common/3dParty/boost'
SOURCE = BASE / 'boost_1_72_0'

def replace_once(path, old, new):
    text = path.read_text()
    if new in text:
        return
    if text.count(old) != 1:
        raise RuntimeError('Unexpected patch context: ' + str(path))
    path.write_text(text.replace(old, new))

def main():
    receipt = json.loads((ROOT / 'evidence/full-source-archive-extraction.json').read_text())
    if receipt['boost']['status'] != 'extracted' or ALIAS.resolve() != WORK.resolve():
        raise RuntimeError('Extraction or alias precondition failed')
    if sys.argv[1] == 'configure':
        setup = WORK / 'boost-keep-environment.cmd'
        setup.write_text('@echo off\nif not "%VCToolsVersion%"=="14.44.35207" exit /b 1\nexit /b 0\n')
        (WORK / 'boost-empty-user-config.jam').write_text('# Deliberately empty project-local user configuration.\n')
        config = SOURCE / 'project-config.jam'
        if config.exists() and not (SOURCE / 'project-config.bootstrap.jam').exists():
            (SOURCE / 'project-config.bootstrap.jam').write_bytes(config.read_bytes())
        setup_alias = (ALIAS / setup.name).as_posix()
        compiler = 'D:/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe'
        config.write_text('import option ;\nusing msvc : 14.3 : "' + compiler + '" : <setup>' + setup_alias +
                          ' <rewrite-setup-scripts>off ;\noption.set keep-going : false ;\n')
        replace_once(SOURCE / 'boost/config/auto_link.hpp',
                     '#  elif defined(BOOST_MSVC)\n\n     // vc14.2:',
                     '#  elif defined(BOOST_MSVC) && (BOOST_MSVC >= 1930)\n\n#    define BOOST_LIB_TOOLSET "vc143"\n\n#  elif defined(BOOST_MSVC)\n\n     // vc14.2:')
        replace_once(BASE / 'boost.pri', '    vs2019:VS_VERSION=142\n',
                     '    vs2019:VS_VERSION=142\n    vs2022:VS_VERSION=143\n')
    elif sys.argv[1] == 'verify':
        include = BASE / 'build/win_64/include'
        versioned = include / 'boost-1_72/boost'
        standard = include / 'boost'
        if versioned.exists() and not standard.exists():
            if not versioned.resolve().is_relative_to(WORK.resolve()) or not standard.resolve().is_relative_to(WORK.resolve()):
                raise RuntimeError('Header move escapes workspace')
            versioned.rename(standard)
        if not (standard / 'shared_ptr.hpp').is_file():
            raise RuntimeError('Missing installed Boost headers')
        outputs = []
        for component in ['filesystem', 'system', 'date_time', 'regex']:
            lib = BASE / ('build/win_64/lib/libboost_' + component + '-vc143-mt-x64-1_72.lib')
            if not lib.is_file() or not lib.stat().st_size:
                raise RuntimeError('Missing library: ' + str(lib))
            outputs.append({'path': str(lib), 'size': lib.stat().st_size})
        (ROOT / 'evidence/full-source-boost-result.json').write_text(json.dumps({
            'status': 'passed', 'source_version': '1.72.0', 'msvc': '14.44.35207',
            'library_toolset_label': 'vc143', 'runtime': 'MD', 'configuration': 'Release',
            'outputs': outputs}, indent=2) + '\n')
        print('BOOST_BUILD_VERIFIED')
    else:
        raise RuntimeError('Unknown operation')

if __name__ == '__main__':
    main()
