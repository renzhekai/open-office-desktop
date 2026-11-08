"""Download and stage the explicitly approved Strawberry Perl portable release."""
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / 'dependencies/strawberry-perl-5.40.0.1'
DEST = ROOT / 'build/full-source/tools/strawberry-perl-5.40.0.1'
NAME = 'strawberry-perl-5.40.0.1-64bit-portable.zip'

def main():
    metadata = json.loads((CACHE / 'release.json').read_text())
    asset = next(item for item in metadata['assets'] if item['name'] == NAME)
    expected_url = 'https://github.com/StrawberryPerl/Perl-Dist-Strawberry/releases/download/SP_54001_64bit_UCRT/' + NAME
    if asset['browser_download_url'] != expected_url:
        raise RuntimeError('Unexpected download origin')
    archive = CACHE / NAME
    partial = CACHE / (NAME + '.part')
    if not archive.exists():
        command = ['curl.exe', '--fail', '--location', '--http1.1', '--retry', '3',
                   '--connect-timeout', '20', '--max-time', '1800', '--continue-at', '-',
                   '--output', str(partial), expected_url]
        with (CACHE / 'download.log').open('a') as log:
            subprocess.run(command, check=True, stdout=log, stderr=log)
        if partial.stat().st_size != asset['size']:
            raise RuntimeError('Publisher size mismatch')
        partial.rename(archive)
    if archive.stat().st_size != asset['size']:
        raise RuntimeError('Archive size mismatch')
    with archive.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    if asset.get('digest') and asset['digest'] != 'sha256:' + digest:
        raise RuntimeError('Publisher digest mismatch')
    if DEST.exists():
        raise RuntimeError('Existing portable tool directory requires inspection')
    with zipfile.ZipFile(archive) as package:
        for name in package.namelist():
            target = DEST / name
            if not target.resolve().is_relative_to(DEST.resolve()):
                raise RuntimeError('Unsafe archive member')
        invalid = package.testzip()
        if invalid:
            raise RuntimeError('ZIP CRC failed: ' + invalid)
        DEST.mkdir(parents=True)
        package.extractall('\\\\?\\' + str(DEST))
    perl = DEST / 'perl/bin/perl.exe'
    if not perl.is_file():
        raise RuntimeError('Portable Perl executable not found')
    result = {'status': 'staged', 'version': '5.40.0.1', 'official_url': expected_url,
              'publisher_size_verified': True, 'zip_crc_verified': True,
              'publisher_digest': asset.get('digest'), 'local_sha256': digest,
              'size': archive.stat().st_size, 'executable': str(perl),
              'system_path_modified': False, 'installer_executed': False}
    (ROOT / 'evidence/full-source-portable-perl.json').write_text(json.dumps(result, indent=2) + '\n')
    print('PORTABLE_PERL_STAGED', flush=True)

if __name__ == '__main__':
    main()
