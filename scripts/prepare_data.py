"""Create a seek index for the user's unchanged APK/OBB audio and font entries."""
import struct, zipfile, json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
rows=[]
for archive,name in enumerate(('game.apk','main.16.com.turner.bestparkintheuniverse.obb')):
    path=ROOT/'data'/name
    with zipfile.ZipFile(path) as z,path.open('rb') as raw:
        for i in z.infolist():
            key=i.filename.removeprefix('assets/')
            if i.is_dir() or not (key.startswith('fonts/') or key.startswith('sounds/')):continue
            if i.compress_type not in (0,8) or i.file_size>32*1024*1024:raise ValueError(key)
            raw.seek(i.header_offset);h=raw.read(30)
            assert h[:4]==b'PK\x03\x04'
            nl,xl=struct.unpack_from('<HH',h,26);offset=i.header_offset+30+nl+xl
            assert offset+i.compress_size<=path.stat().st_size
            b=key.encode();rows.append((key,struct.pack('<4I3H',offset,i.compress_size,i.file_size,i.CRC,i.compress_type,archive,len(b))+b))
assert len({k for k,_ in rows})==len(rows)
out=struct.pack('<2I',0x31415042,len(rows))+b''.join(v for _,v in sorted(rows))
(ROOT/'data/assets.idx').write_bytes(out)
print(f'Indexed {len(rows)} entries; {len(out)} bytes. Original archives unchanged.')
