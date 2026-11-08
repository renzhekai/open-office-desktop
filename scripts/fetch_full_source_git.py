"""Fetch pinned additional engine source checkouts, without running hooks or installers."""
import datetime
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / 'build/full-source'
THIRD = WORK / 'DesktopEditors/core/Common/3dParty'
SOURCES = {
    'libxml2': ('https://github.com/GNOME/libxml2.git', '39b66eb2089f491eacf8e22a7965a235f7f5735e', 'libxml/libxml2'),
    'v8': ('https://chromium.googlesource.com/v8/v8.git', 'a2d8eca22ad478a95ca67bb2421a9a2535113925', 'v8_89/v8'),
}

def main():
    name = sys.argv[1]
    url, commit, relative = SOURCES[name]
    dest = THIRD / relative
    if not dest.resolve().is_relative_to(WORK.resolve()):
        raise RuntimeError('Destination escapes isolated workspace')
    receipt = ROOT / ('evidence/full-source-' + name + '-source-result.json')
    git = ['git', '-c', 'core.longpaths=true', '-c', 'core.hooksPath=' + str(WORK / 'empty-hooks'),
           '-c', 'http.version=HTTP/1.1']
    if dest.exists() and not (dest / '.git').is_dir():
        raise RuntimeError('Unexpected existing source destination')
    result = {'status': 'running', 'source_only': True, 'url': url, 'commit': commit,
              'destination': str(dest), 'started_at_utc': datetime.datetime.now(datetime.timezone.utc).isoformat()}
    receipt.write_text(json.dumps(result, indent=2) + '\n')
    with (ROOT / ('evidence/full-source-' + name + '-fetch.log')).open('a', encoding='utf-8') as log:
        def run(arguments):
            command = git + arguments
            log.write('COMMAND ' + json.dumps(command) + '\n'); log.flush()
            subprocess.run(command, check=True, stdout=log, stderr=log)
        try:
            if not dest.exists():
                run(['init', '--initial-branch=source-build', str(dest)])
                run(['-C', str(dest), 'remote', 'add', 'origin', url])
            actual_url = subprocess.check_output(git + ['-C', str(dest), 'remote', 'get-url', 'origin'], text=True).strip()
            if actual_url != url:
                raise RuntimeError('Existing repository origin differs')
            run(['-C', str(dest), 'fetch', '--depth=1', 'origin', commit])
            run(['-C', str(dest), 'checkout', '--detach', commit])
            actual = subprocess.check_output(git + ['-C', str(dest), 'rev-parse', 'HEAD'], text=True).strip()
            if actual != commit:
                raise RuntimeError('Source commit mismatch')
            result.update(status='checked_out', finished_at_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
        except Exception as error:
            result.update(status='failed', error=str(error))
            receipt.write_text(json.dumps(result, indent=2) + '\n')
            raise
    receipt.write_text(json.dumps(result, indent=2) + '\n')
    print(name + ': source checkout verified')

if __name__ == '__main__':
    main()
