"""Match the source OpenSSL static library CRT to the Qt/native host CRT."""
import json
from pathlib import Path
import hashlib

ROOT = Path(__file__).resolve().parents[1]
FILE = ROOT / 'build/full-source/DesktopEditors/core/Common/3dParty/openssl/openssl/Configurations/10-main.conf'
old = 'lib_cflags       => add(sub { $disabled{shared} ? "/MT /Zl" : () }),'
new = 'lib_cflags       => add(sub { $disabled{shared} ? "/MD /Zl" : () }),'
text = FILE.read_text()
if new not in text:
    if text.count(old) != 1:
        raise RuntimeError('OpenSSL CRT patch context changed')
    FILE.write_text(text.replace(old, new))
(ROOT / 'evidence/full-source-openssl-crt-patch.json').write_text(json.dumps({
    'path': str(FILE), 'change': 'VC static library /MT /Zl -> /MD /Zl',
    'sha256': hashlib.sha256(FILE.read_bytes()).hexdigest()}, indent=2) + '\n')
