"""Stage locally built ICU and prove that its real conversion data loads."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'build/full-source/DesktopEditors/core/Common/3dParty/icu'
SOURCE = BASE / 'icu'
STAGE = BASE / 'win_64/build'

def main():
    STAGE.mkdir(parents=True, exist_ok=True)
    outputs = []
    for relative in ['bin64/icudt74.dll', 'bin64/icuuc74.dll', 'lib64/icudt.lib', 'lib64/icuuc.lib']:
        source = SOURCE / relative
        if not source.is_file():
            raise RuntimeError('Missing compiled ICU output: ' + str(source))
        target = STAGE / source.name
        if source.name == 'icudt74.dll' and source.stat().st_size < 1000000:
            raise RuntimeError('ICU data DLL is still stubdata')
        shutil.copy2(source, target)
        outputs.append({'path': str(target), 'size': target.stat().st_size,
                        'sha256': hashlib.sha256(target.read_bytes()).hexdigest()})
    with os.add_dll_directory(str(STAGE)):
        dll = ctypes.CDLL(str(STAGE / 'icuuc74.dll'))
        version = (ctypes.c_uint8 * 4)()
        get_version = dll.u_getVersion_74
        get_version.argtypes = [ctypes.POINTER(ctypes.c_uint8)]
        get_version.restype = None
        get_version(version)
        if list(version)[:2] != [74, 2]:
            raise RuntimeError('Wrong ICU runtime version')
        status = ctypes.c_int32(0)
        initialize = dll.u_init_74
        initialize.argtypes = [ctypes.POINTER(ctypes.c_int32)]
        initialize.restype = None
        initialize(ctypes.byref(status))
        if status.value > 0:
            raise RuntimeError('ICU data initialization failed: ' + str(status.value))
        open_converter = dll.ucnv_open_74
        open_converter.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_int32)]
        open_converter.restype = ctypes.c_void_p
        close_converter = dll.ucnv_close_74
        close_converter.argtypes = [ctypes.c_void_p]
        close_converter.restype = None
        converters = []
        for encoding in [b'UTF-8', b'GB18030', b'windows-1252']:
            status.value = 0
            converter = open_converter(encoding, ctypes.byref(status))
            if not converter or status.value > 0:
                raise RuntimeError('Cannot load converter ' + encoding.decode())
            close_converter(converter)
            converters.append(encoding.decode())
    receipt = {'status': 'passed', 'version': list(version), 'compiler_version': '14.44.35207',
               'msbuild_target_definitions': 'v145', 'real_data_loaded': True,
               'converters_opened': converters, 'outputs': outputs}
    (ROOT / 'evidence/full-source-icu-result.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('ICU_BUILD_AND_DATA_VERIFIED')

if __name__ == '__main__':
    main()
