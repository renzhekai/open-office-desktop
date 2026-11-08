"""Static metadata inspection only: never imports/loads/executes the DLL payload."""
import json, pathlib, struct, zlib, hashlib
ROOT = pathlib.Path(__file__).resolve().parent
directory = json.loads((ROOT / "zip-directory.json").read_text(encoding="utf-8"))
by_name = {x["path"]: x for x in directory["entries"]}

def inspect(name):
    entry = by_name[name]
    raw = (ROOT / (name.removesuffix(".dll") + ".entry-range.bin")).read_bytes()
    h = struct.unpack_from("<4s5H3I2H", raw, 0)
    if h[0] != b"PK\x03\x04" or h[3] != 8:
        raise ValueError("Expected deflated ZIP local entry")
    data_offset = 30 + h[9] + h[10]
    filename = raw[30:30+h[9]].decode("ascii")
    if filename != name:
        raise ValueError("Entry name mismatch")
    compressed = raw[data_offset:data_offset+entry["compressed_size"]]
    if len(compressed) != entry["compressed_size"]:
        raise ValueError("Incomplete compressed entry")
    dec = zlib.decompressobj(-15)
    data = dec.decompress(compressed, entry["uncompressed_size"]+1)
    if not dec.eof or dec.unconsumed_tail or len(data) != entry["uncompressed_size"]:
        raise ValueError("Invalid decompressed size")
    if f"{zlib.crc32(data):08x}" != entry["crc32"]:
        raise ValueError("ZIP CRC mismatch")
    return parse_pe(name, data, entry["crc32"], "ZIP entry inflated in memory; CRC checked; PE parsed as bytes; DLL never loaded/executed")

def parse_pe(name, data, crc32=None, inspection="PE parsed as bytes from verified runtime; DLL never loaded/executed"):
    def u16(o): return struct.unpack_from("<H", data, o)[0]
    def u32(o): return struct.unpack_from("<I", data, o)[0]
    def u64(o): return struct.unpack_from("<Q", data, o)[0]
    pe = u32(0x3c)
    if data[:2] != b"MZ" or data[pe:pe+4] != b"PE\0\0":
        raise ValueError("Invalid PE signature")
    machine, sections_count, timestamp, _, _, optional_size, characteristics = struct.unpack_from("<HHIIIHH", data, pe+4)
    optional = pe+24
    magic = u16(optional)
    if magic != 0x20b: raise ValueError("Expected PE32+ x64")
    section_offset = optional+optional_size
    sections=[]
    for i in range(sections_count):
        offset=section_offset+i*40
        secname=data[offset:offset+8].split(b"\0")[0].decode("ascii", "replace")
        vsize,rva,rawsize,rawptr=struct.unpack_from("<IIII", data, offset+8)
        sections.append(dict(name=secname,rva=rva,virtual_size=vsize,raw_size=rawsize,raw_offset=rawptr,characteristics=u32(offset+36)))
    def section(rva):
        return next((s for s in sections if s["rva"]<=rva<s["rva"]+max(s["virtual_size"],s["raw_size"])),None)
    def ptr(rva, count=1):
        s=section(rva)
        if s is None or rva-s["rva"]+count>s["raw_size"]: raise ValueError("RVA outside raw section")
        off=s["raw_offset"]+rva-s["rva"]
        if off+count>len(data): raise ValueError("Out of bounds")
        return off
    def cstr(rva):
        off=ptr(rva); end=data.find(b"\0",off,min(off+16384,len(data)))
        if end<0: raise ValueError("Unterminated string")
        return data[off:end].decode("ascii","replace")
    dirs=[struct.unpack_from("<II",data,optional+112+i*8) for i in range(min(u32(optional+108),16))]
    exports=[]; er,es=dirs[0]
    if er:
        e=struct.unpack_from("<IIHHIIIIIII",data,ptr(er,40))
        base,nfuncs,nnames,funcs,names,ordinals=e[5:]
        if nfuncs>50000 or nnames>50000: raise ValueError("Unexpected export count")
        names_by_ord={u16(ptr(ordinals+i*2,2)):cstr(u32(ptr(names+i*4,4))) for i in range(nnames)}
        for i in range(nfuncs):
            rva=u32(ptr(funcs+i*4,4))
            if not rva: continue
            s=section(rva); forwarded=er<=rva<er+es
            exports.append(dict(name=names_by_ord.get(i),ordinal=base+i,rva=rva,forwarder=cstr(rva) if forwarded else None,section=s["name"] if s else None,executable_section=bool(s and s["characteristics"]&0x20000000)))
    imports=[]; ir,isz=dirs[1]
    if ir:
        offset=ptr(ir,20)
        for i in range(1024):
            original,timed,forward,namer,first=struct.unpack_from("<IIIII",data,offset+i*20)
            if (original,timed,forward,namer,first)==(0,0,0,0,0): break
            dll=cstr(namer); thunk=original or first; symbols=[]
            for j in range(50000):
                v=u64(ptr(thunk+j*8,8))
                if not v: break
                symbols.append({"ordinal":v&0xffff} if v&(1<<63) else {"name":cstr(v+2)})
            else: raise ValueError("Unterminated import thunk")
            imports.append(dict(dll=dll,symbols=symbols))
        else: raise ValueError("Unterminated imports")
    result=dict(file=name,inspection=inspection,uncompressed_size=len(data),sha256=hashlib.sha256(data).hexdigest(),zip_crc32=crc32,machine=hex(machine),pe_timestamp=timestamp,linker_version=f"{data[optional+2]}.{data[optional+3]}",characteristics=hex(characteristics),has_delay_import_directory=bool(len(dirs)>13 and dirs[13][0]),sections=sections,imports=imports,exports=exports)
    return result

if __name__ == "__main__":
    for name in ("ascdocumentscore.dll","qtascdocumentscore.dll"):
        r=inspect(name)
        output=ROOT/(name+".pe-metadata.json")
        if output.exists():
            if json.loads(output.read_text(encoding="utf-8"))!=r:
                raise ValueError("Existing metadata differs; refusing overwrite")
        else:
            with output.open("x",encoding="utf-8") as f: json.dump(r,f,indent=2)
        print(json.dumps(dict(file=name,machine=r["machine"],linker_version=r["linker_version"],exports=len(r["exports"]),imports=[x["dll"] for x in r["imports"]],has_delay_import_directory=r["has_delay_import_directory"],sha256=r["sha256"]),indent=2))

