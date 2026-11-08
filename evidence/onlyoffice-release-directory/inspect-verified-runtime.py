"""Parse verified runtime binaries as byte data only; never load or execute them."""
import pathlib,json,hashlib,importlib.util
EVIDENCE=pathlib.Path(__file__).resolve().parent
verification=json.loads((EVIDENCE/"runtime-verification.json").read_text(encoding="utf-8"))
if not verification["full_archive_hash_verified"] or verification["sha256"]!="54979d04f1cdca56deb0c62e0356355a45f392cdb54f04f6b7e436b1f44c3605":
    raise RuntimeError("Complete locked archive verification required")
runtime=pathlib.Path(verification["runtime"])
files={x["path"]:x for x in verification["files"]}
spec=importlib.util.spec_from_file_location("static_pe_parser",EVIDENCE/"inspect-pe-metadata.py")
parser=importlib.util.module_from_spec(spec)
spec.loader.exec_module(parser)
summaries=[]
for relative in ("ascdocumentscore.dll","qtascdocumentscore.dll","converter/kernel.dll","converter/graphics.dll","videoplayer.dll","editors_helper.exe","converter/x2t.exe"):
    source=runtime.joinpath(*relative.split("/"))
    data=source.read_bytes()
    name=source.name
    if hashlib.sha256(data).hexdigest()!=files[relative]["sha256"]: raise RuntimeError("Runtime file changed")
    report=parser.parse_pe(name,data,files[relative]["zip_crc32"])
    report["source_relative_path"]=relative
    report["full_archive_sha256_verified"]=verification["sha256"]
    ranged=EVIDENCE/(name+".pe-metadata.json")
    if name in ("ascdocumentscore.dll","qtascdocumentscore.dll"):
        previous=json.loads(ranged.read_text(encoding="utf-8"))
        if previous["sha256"]!=report["sha256"] or previous["exports"]!=report["exports"] or previous["imports"]!=report["imports"]:
            raise RuntimeError("Range-derived PE metadata differs from complete verified archive")
        output=EVIDENCE/(name+".verified-pe-metadata.json")
    else:
        output=ranged
    with output.open("x",encoding="utf-8") as f: json.dump(report,f,indent=2)
    summaries.append(dict(file=name,source_relative_path=relative,metadata=str(output),sha256=report["sha256"],machine=report["machine"],linker_version=report["linker_version"],named_exports=sum(x["name"] is not None for x in report["exports"]),data_exports=sum(not x["executable_section"] and not x["forwarder"] for x in report["exports"]),imports=[x["dll"] for x in report["imports"]]))
with (EVIDENCE/"verified-native-summary.json").open("x",encoding="utf-8") as out: json.dump(summaries,out,indent=2)
print(json.dumps(summaries,indent=2))

