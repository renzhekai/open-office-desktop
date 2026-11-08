"""Verify a locked official archive and extract data into a new isolated directory; execute nothing."""
import pathlib,hashlib,json,zipfile,stat,re
WORK=pathlib.Path(r"D:\open office").resolve()
DEP=(WORK/"dependencies"/"onlyoffice-v10.0.0").resolve()
ARCHIVE=DEP/"DesktopEditors_x64.zip"
RUNTIME=DEP/"runtime"
EVIDENCE=WORK/"evidence"/"onlyoffice-release-directory"
EXPECTED_SIZE=397286158
EXPECTED_SHA="54979d04f1cdca56deb0c62e0356355a45f392cdb54f04f6b7e436b1f44c3605"
if not DEP.is_relative_to(WORK) or RUNTIME.resolve().parent!=DEP:
    raise RuntimeError("Target outside intended dependency directory")
if RUNTIME.exists(): raise RuntimeError("Runtime target exists; refusing overwrite")
h=hashlib.sha256()
with ARCHIVE.open("rb") as f:
    while block:=f.read(1024*1024): h.update(block)
if ARCHIVE.stat().st_size!=EXPECTED_SIZE or h.hexdigest()!=EXPECTED_SHA:
    raise RuntimeError("Locked archive verification failed")
with zipfile.ZipFile(ARCHIVE) as z:
    infos=z.infolist()
    if len(infos)!=4911 or sum(x.file_size for x in infos)!=1070010975:
        raise RuntimeError("Archive directory differs from inspected official asset")
    seen=set()
    reserved=re.compile(r"^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)",re.I)
    for item in infos:
        name=item.filename
        parts=name.rstrip("/").split("/")
        if item.orig_filename!=name or not parts or name.startswith(("/","\\")) or "\\" in name or any(p in ("",".","..") or any(ord(ch)<32 or ch in '<>:"|?*' for ch in p) or p.endswith(("."," ")) or reserved.match(p) for p in parts):
            raise RuntimeError("Unsafe Windows archive path: "+name)
        key="/".join(parts).casefold()
        if key in seen: raise RuntimeError("Duplicate/case-colliding archive path: "+name)
        seen.add(key)
        mode=item.external_attr>>16
        if stat.S_ISLNK(mode) or (stat.S_IFMT(mode) not in (0,stat.S_IFREG,stat.S_IFDIR)):
            raise RuntimeError("Non-regular archive member: "+name)
        if item.flag_bits&1: raise RuntimeError("Encrypted member")
        if item.compress_type not in (zipfile.ZIP_STORED,zipfile.ZIP_DEFLATED): raise RuntimeError("Unexpected ZIP compression")
        if not RUNTIME.joinpath(*parts).resolve().is_relative_to(RUNTIME.resolve()):
            raise RuntimeError("Extraction target escapes runtime")
    RUNTIME.mkdir()
    files=[]
    for item in infos:
        dest=RUNTIME.joinpath(*item.filename.rstrip("/").split("/"))
        if item.is_dir():
            dest.mkdir(parents=True,exist_ok=True)
            continue
        dest.parent.mkdir(parents=True,exist_ok=True)
        digest=hashlib.sha256(); written=0
        with z.open(item) as src, dest.open("xb") as out:
            while block:=src.read(1024*1024):
                out.write(block); digest.update(block); written+=len(block)
        if written!=item.file_size: raise RuntimeError("Extracted size mismatch")
        files.append(dict(path=item.filename,size=written,sha256=digest.hexdigest(),zip_crc32=f"{item.CRC:08x}"))
    report=dict(source_url="https://github.com/ONLYOFFICE/DesktopEditors/releases/download/v10.0.0/DesktopEditors_x64.zip",archive=str(ARCHIVE),size=EXPECTED_SIZE,sha256=EXPECTED_SHA,full_archive_hash_verified=True,runtime=str(RUNTIME),zip_entries=len(infos),regular_files=len(files),uncompressed_bytes=sum(x["size"] for x in files),safe_path_checks_passed=True,member_crc_checks_passed=True,executable_files_executed=False,system_installed=False,files=files)
    with (EVIDENCE/"runtime-verification.json").open("x",encoding="utf-8") as out: json.dump(report,out,indent=2)
    print(json.dumps({k:v for k,v in report.items() if k!="files"},indent=2))

