"""Extract the original ICO as PE resource data. No executable is loaded."""
import argparse, struct
from pathlib import Path

def extract(executable: Path, destination: Path):
    data=executable.read_bytes();pe=struct.unpack_from('<I',data,0x3c)[0]
    assert data[pe:pe+4]==b'PE\0\0'
    count=struct.unpack_from('<H',data,pe+6)[0];optional_size=struct.unpack_from('<H',data,pe+20)[0];optional=pe+24
    assert struct.unpack_from('<H',data,optional)[0]==0x10b
    resource_rva=struct.unpack_from('<I',data,optional+96+16)[0]
    sections=[]
    for i in range(count):
        section=optional+optional_size+40*i;virtual,base,raw,pointer=struct.unpack_from('<IIII',data,section+8)
        sections.append((base,max(virtual,raw),pointer))
    def offset(rva):
        for base,size,pointer in sections:
            if base<=rva<base+size:return pointer+rva-base
        raise ValueError('Invalid PE resource RVA')
    resource_base=offset(resource_rva)
    def entries(relative):
        base=resource_base+relative;named,ids=struct.unpack_from('<HH',data,base+12)
        return [struct.unpack_from('<II',data,base+16+8*i) for i in range(named+ids)]
    def language_data(type_id,item_id=None):
        types=dict(entries(0));items=entries(types[type_id]&0x7fffffff)
        selected=items[0][1] if item_id is None else dict(items)[item_id]
        language=entries(selected&0x7fffffff)[0][1]
        rva,size=struct.unpack_from('<II',data,resource_base+language)
        return data[offset(rva):offset(rva)+size]
    group=language_data(14);images=struct.unpack_from('<H',group,4)[0];records=[];bodies=[];position=6+16*images
    for i in range(images):
        w,h,colors,reserved,planes,bpp,size,identifier=struct.unpack_from('<BBBBHHIH',group,6+14*i)
        body=language_data(3,identifier);records.append(struct.pack('<BBBBHHII',w,h,colors,reserved,planes,bpp,len(body),position));bodies.append(body);position+=len(body)
    destination.parent.mkdir(parents=True,exist_ok=True);destination.write_bytes(struct.pack('<HHH',0,1,images)+b''.join(records)+b''.join(bodies))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('executable',type=Path);p.add_argument('destination',type=Path);a=p.parse_args();extract(a.executable,a.destination)
