import struct
def form(typ,payload): return b'FORM'+struct.pack('>I',4+len(payload))+typ.encode('latin-1').ljust(4)+payload
def chunk(tag,payload): return tag.encode('latin-1').ljust(4)+struct.pack('>I',len(payload))+payload
def walk(b,o,end,path,cb):
    while o<end:
        tag=b[o:o+4]; size=struct.unpack_from('>I',b,o+4)[0]; o+=8
        if tag==b'FORM': walk(b,o+4,o+size,path+[b[o:o+4].decode('latin-1')],cb)
        else: cb(list(path),tag.decode('latin-1'),b[o:o+size])
        o+=size
def parse(path):
    b=open(path,'rb').read(); out=[]
    walk(b,0,len(b),[],lambda p,t,d: out.append((p,t,d)))
    return out
