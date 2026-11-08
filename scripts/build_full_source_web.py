"""Build desktop web resources from the isolated fixed checkout."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/full-source/DesktopEditors/web-apps/build'
EVIDENCE = ROOT / 'evidence'
RECEIPT = EVIDENCE / 'full-source-web-build-result.json'

def main():
    if RECEIPT.exists():
        raise RuntimeError('Inspect previous receipt before repeating this build')
    env = dict(os.environ, PRODUCT_VERSION='10.0.0', BUILD_NUMBER='0',
               OO_BRANDING='onlyoffice-opensource', SYSTEM_ENCODING='utf8')
    tasks = [('common', ['init-build-common', 'increment-build', 'deploy-api',
              'deploy-apps-common', 'deploy-socketio', 'deploy-xregexp', 'deploy-requirejs',
              'deploy-megapixel', 'deploy-jquery', 'deploy-underscore', 'deploy-iscroll',
              'deploy-fetch', 'deploy-es6-promise', 'deploy-common-embed', 'deploy-monaco'])]
    for editor in ['documenteditor', 'spreadsheeteditor', 'presentationeditor', 'pdfeditor', 'visioeditor']:
        chain = ['init-build-' + editor, 'increment-build', 'deploy-app-main']
        if editor == 'presentationeditor':
            chain.append('deploy-reporter')
        tasks.append((editor, chain))
    result = {'status': 'running', 'started_at_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'raster_recompression': False, 'mobile': False, 'forms_addon': False, 'steps': []}
    for name, chain in tasks:
        command = ['node', '-e',
                   "require(process.argv[1]).tasks(JSON.parse(process.argv[4]), {gruntfile:process.argv[2],base:process.argv[3],desktop:true,verbose:true});",
                   str(BUILD / 'node_modules/grunt'), str(ROOT / 'scripts/full_source_web_grunt.cjs'),
                   str(BUILD), json.dumps(chain)]
        step = {'name': name, 'command': command, 'status': 'running'}
        result['steps'].append(step)
        RECEIPT.write_text(json.dumps(result, indent=2) + '\n')
        with (EVIDENCE / ('full-source-web-' + name + '.log')).open('w', encoding='utf-8') as log:
            proc = subprocess.Popen(command, cwd=BUILD, env=env, stdout=log, stderr=subprocess.STDOUT)
            step['pid'] = proc.pid
            RECEIPT.write_text(json.dumps(result, indent=2) + '\n')
            step['exit_code'] = proc.wait()
        step['status'] = 'passed' if step['exit_code'] == 0 else 'failed'
        print(name + ': ' + step['status'], flush=True)
        if step['exit_code']:
            result['status'] = 'failed'
            RECEIPT.write_text(json.dumps(result, indent=2) + '\n')
            return step['exit_code']
    deploy = BUILD.parent / 'deploy/web-apps'
    files = []
    for file in sorted(deploy.rglob('*')):
        if file.is_file():
            files.append({'path': file.relative_to(deploy).as_posix(), 'size': file.stat().st_size,
                          'sha256': hashlib.sha256(file.read_bytes()).hexdigest()})
    (EVIDENCE / 'full-source-web-build-manifest.json').write_text(json.dumps(files, indent=2) + '\n')
    result.update(status='passed', file_count=len(files),
                  finished_at_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
    RECEIPT.write_text(json.dumps(result, indent=2) + '\n')
    return 0

if __name__ == '__main__':
    sys.exit(main())
