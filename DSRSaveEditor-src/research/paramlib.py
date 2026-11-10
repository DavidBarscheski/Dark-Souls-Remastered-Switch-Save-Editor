import re, struct, xml.etree.ElementTree as ET
TYPES={'s8':('b',1),'u8':('B',1),'s16':('h',2),'u16':('H',2),'s32':('i',4),'u32':('I',4),'f32':('f',4)}
import os
DEFS=os.environ.get("PARAMDEX_DS1R_DEFS","/home/claude/soulsmods/paramdex/DS1R/Defs/")
def load_def(name):
    root=ET.parse(DEFS+name+".xml").getroot()
    fields=[]; off=0; bitpos=None; bitbase=None
    for f in root.find("Fields"):
        d=f.get("Def").split("=")[0].strip()
        m=re.match(r"(\w+)\s+(\w+)(\[(\d+)\])?(?::(\d+))?",d)
        t,n,_,arr,bits=m.groups()
        if t=="dummy8":
            sz=int(arr) if arr else 1
            if bits: # bitfield of dummy8
                pass
            bitpos=None
            off+=sz; continue
        fmt,sz=TYPES[t]
        if bits:
            b=int(bits)
            if bitpos is None or bitbase[1]!=sz or bitpos+b>sz*8:
                bitbase=(off,sz); bitpos=0; off+=sz
            fields.append((n,t,bitbase[0],sz,fmt,(bitpos,b))); bitpos+=b
        else:
            bitpos=None
            if arr:
                for k in range(int(arr)): fields.append((f"{n}{k}",t,off+k*sz,sz,fmt,None))
                off+=sz*int(arr)
            else:
                fields.append((n,t,off,sz,fmt,None)); off+=sz
    return fields, off
def load_param(path):
    d=open(path,"rb").read()
    strs,dstart,_,_,rows=struct.unpack_from('<IHHHH',d,0)
    ptype=d[0xc:0x2c].split(b'\0')[0].decode()
    ents=[struct.unpack_from('<III',d,0x30+12*i) for i in range(rows)]
    return d,ptype,ents
def rows(path,defname):
    fields,size=load_def(defname)
    d,pt,ents=load_param(path)
    out={}
    offs=sorted(e[1] for e in ents)
    rs=(offs[1]-offs[0]) if len(offs)>1 else size
    for rid,do,no in ents:
        r={}
        for n,t,o,sz,fmt,bits in fields:
            v=struct.unpack_from('<'+fmt,d,do+o)[0]
            if bits: v=(v>>bits[0])&((1<<bits[1])-1)
            r[n]=v
        out[rid]=r
    return out,size,rs,pt
