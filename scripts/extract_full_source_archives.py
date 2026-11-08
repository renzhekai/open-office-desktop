"""Stage publisher-verified archives into the isolated engine worktree only."""
import datetime
import hashlib
import json
from pathlib import Path
import tarfile

ROOT = Path(__file__).resolve().parents[1]
ISOLATED = ROOT / 'build/full-source'
THIRD = ISOLATED / 'DesktopEditors/core/Common/3dParty'
RECEIPT = ROOT / 'evidence/full-source-archive-extraction.json'
LAYOUT = {'boost': ('boost', 'boost_1_72_0', 'boost_1_72_0'),
          'icu4c': ('icu', 'icu', 'icu'),
          'openssl': ('openssl', 'openssl-1.1.1f', 'openssl')}

def main():
    manifest = json.loads((ROOT / 'dependencies/full-source/source-manifest.json').read_text())
    results = json.loads(RECEIPT.read_text()) if RECEIPT.exists() else {}
    for source in manifest['sources']:
        name = source['name']
        parent, archive_root, destination = LAYOUT[name]
        archive = Path(source['local_path'])
        with archive.open('rb') as stream:
            actual = hashlib.file_digest(stream, source['expected_hash_algorithm']).hexdigest()
        if actual != source['expected_hash']:
            raise RuntimeError('Archive checksum mismatch: ' + name)
        target = THIRD / parent / destination
        if not target.resolve().is_relative_to(ISOLATED.resolve()):
            raise RuntimeError('Target escapes isolated workspace')
        if target.exists():
            if results.get(name, {}).get('status') != 'extracted':
                raise RuntimeError('Unexpected existing target: ' + str(target))
            print(name + ': already staged', flush=True)
            continue
        staging = ISOLATED / ('extract-' + name)
        if not staging.resolve().is_relative_to(ISOLATED.resolve()):
            raise RuntimeError('Staging directory escapes workspace')
        resuming = staging.exists()
        staging.mkdir(exist_ok=True)
        with tarfile.open(archive) as package:
            members = package.getmembers()
            if any(m.name.split('/')[0] != archive_root for m in members):
                raise RuntimeError('Unexpected archive layout')
            allowed = {m.name.rstrip('/').lower() for m in members}
            if resuming:
                for existing in staging.rglob('*'):
                    if existing.is_file() and existing.relative_to(staging).as_posix().lower() not in allowed:
                        raise RuntimeError('Unexpected partial-extraction file: ' + str(existing))
            skipped = 0
            for index, member in enumerate(members):
                target_file = staging / member.name
                if resuming and member.isfile() and target_file.is_file() and target_file.stat().st_size == member.size:
                    with target_file.open('rb') as current, package.extractfile(member) as original:
                        if hashlib.file_digest(current, 'sha256').digest() == hashlib.file_digest(original, 'sha256').digest():
                            skipped += 1
                            continue
                package.extract(member, '\\\\?\\' + str(staging), filter='data')
                if index % 5000 == 0:
                    print(f'{name}: processed {index}/{len(members)} entries, reused {skipped}', flush=True)
        extracted = staging / archive_root
        extracted.rename(target)
        results[name] = {'status': 'extracted', 'archive': str(archive),
                         'destination': str(target), 'archive_member_count': len(members),
                         'verified_hash': actual,
                         'completed_at_utc': datetime.datetime.now(datetime.timezone.utc).isoformat()}
        RECEIPT.write_text(json.dumps(results, indent=2) + '\n')
        print(name + ': extracted ' + str(len(members)) + ' entries', flush=True)

if __name__ == '__main__':
    main()
