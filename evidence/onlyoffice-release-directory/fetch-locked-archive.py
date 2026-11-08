"""Fetch one locked official archive with bounded HTTPS Range requests; no extraction/execution."""
import concurrent.futures, hashlib, json, pathlib, re, time, urllib.request, urllib.parse
ROOT=pathlib.Path(r"D:\open office\dependencies\onlyoffice-v10.0.0")
URL="https://github.com/ONLYOFFICE/DesktopEditors/releases/download/v10.0.0/DesktopEditors_x64.zip"
SIZE=397286158
SHA="54979d04f1cdca56deb0c62e0356355a45f392cdb54f04f6b7e436b1f44c3605"
PREFIX=ROOT/"DesktopEditors_x64.zip.part"
FINAL=ROOT/"DesktopEditors_x64.zip"
PARTS=ROOT/"range-parts"
PLAN=PARTS/"plan.json"
if FINAL.exists(): raise RuntimeError("Final archive exists; refusing replacement")
if not PREFIX.is_file(): raise RuntimeError("Expected preserved prefix missing")
PARTS.mkdir(exist_ok=True)
if PLAN.exists():
    plan=json.loads(PLAN.read_text())
    if plan["url"]!=URL or plan["size"]!=SIZE or plan["sha256"]!=SHA: raise RuntimeError("Plan mismatch")
    if PREFIX.stat().st_size!=plan["prefix_bytes"]: raise RuntimeError("Prefix size changed")
else:
    prefix_bytes=PREFIX.stat().st_size
    if not 0<prefix_bytes<SIZE: raise RuntimeError("Invalid prefix size")
    chunk=(SIZE-prefix_bytes+7)//8
    segments=[dict(index=i,start=prefix_bytes+i*chunk,end=min(SIZE-1,prefix_bytes+(i+1)*chunk-1)) for i in range(8) if prefix_bytes+i*chunk<SIZE]
    plan=dict(url=URL,size=SIZE,sha256=SHA,prefix_bytes=prefix_bytes,segments=segments)
    with PLAN.open("x") as f: json.dump(plan,f,indent=2)

def fetch(seg):
    path=PARTS/f"segment-{seg['index']:02d}.part"
    expected=seg["end"]-seg["start"]+1
    failures=0
    while True:
        have=path.stat().st_size if path.exists() else 0
        if have==expected: return str(path)
        if have>expected: raise RuntimeError("Segment exceeds declared length")
        start=seg["start"]+have
        end=min(seg["end"],start+4*1024*1024-1)
        request=urllib.request.Request(URL,headers={"Range":f"bytes={start}-{end}","User-Agent":"ONLYOFFICE-build-dependency-metadata-check","Accept-Encoding":"identity"})
        try:
            with urllib.request.urlopen(request,timeout=30) as response:
                final_url=urllib.parse.urlparse(response.geturl())
                if final_url.scheme!="https" or final_url.hostname not in {"github.com","release-assets.githubusercontent.com","objects.githubusercontent.com"}: raise RuntimeError("Unexpected asset redirect host")
                expected_range=f"bytes {start}-{end}/{SIZE}"
                if response.status!=206 or response.headers.get("Content-Range")!=expected_range: raise RuntimeError("Unexpected HTTP range response")
                remaining=end-start+1
                if int(response.headers.get("Content-Length","-1"))!=remaining: raise RuntimeError("Unexpected segment response size")
                with path.open("ab") as output:
                    while remaining:
                        block=response.read(min(64*1024,remaining))
                        if not block: raise OSError("Interrupted segment")
                        output.write(block)
                        remaining-=len(block)
            failures=0
            if path.stat().st_size==expected:
                print(f"segment {seg['index']} complete: {expected} bytes",flush=True)
                return str(path)
        except (OSError,TimeoutError) as exc:
            failures+=1
            if failures>=20: raise
            print(f"segment {seg['index']} retry {failures}: {type(exc).__name__}",flush=True)
            time.sleep(1)
    raise RuntimeError("Incomplete segment")

print(f"Preserved prefix {plan['prefix_bytes']} bytes; fetching {len(plan['segments'])} bounded ranges.",flush=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
    futures=[pool.submit(fetch,seg) for seg in plan["segments"]]
    for f in concurrent.futures.as_completed(futures): f.result()
assembling=ROOT/"DesktopEditors_x64.zip.assembling"
if assembling.exists(): raise RuntimeError("Assembly file exists; inspect before retry")
digest=hashlib.sha256(); written=0
with assembling.open("xb") as out:
    for path in [PREFIX]+[PARTS/f"segment-{seg['index']:02d}.part" for seg in plan["segments"]]:
        with path.open("rb") as inp:
            while block:=inp.read(1024*1024):
                out.write(block); digest.update(block); written+=len(block)
if written!=SIZE or digest.hexdigest()!=SHA: raise RuntimeError("Full archive size/SHA256 mismatch; assembly retained for inspection")
if FINAL.exists(): raise RuntimeError("Final path appeared; refusing replacement")
assembling.rename(FINAL)
print(json.dumps({"archive":str(FINAL),"size":written,"sha256":digest.hexdigest(),"verified":True}),flush=True)

