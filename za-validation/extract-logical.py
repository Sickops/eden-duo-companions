from pathlib import Path
import sys,subprocess,re
sys.path.insert(0,r'C:\Users\Sick8\AppData\Local\Temp\opencode\scvi')
from za_pack import *
fs,ix,fd,infos,offs=open_archive('.za-extract/romfs');hs=fd.vec_u64(fd.root(),0);by={h:i for i,h in enumerate(hs)};rows=fd.vec_tab(fd.root(),2);paths=fd.vec_str(fd.root(),1);out=Path('companions/za-validation/logical');out.mkdir(exist_ok=True);cache={}
for name in sys.argv[1:]:
 h=fnv1a64(name)
 if h not in by:print('missing',name);continue
 pi=fd.u32(fd.field(rows[by[h]],0));pack=paths[pi]
 if pi not in cache:
  _,pid=ix[pack];sz=fd.u64(fd.field(infos[pi],0));cache[pi]=parse_pack(fetch(fs,offs[pid],sz))
 for f in cache[pi]:
  if f['hash']!=h:continue
  p=out/(Path(name).name+'.compressed');dest=out/(Path(name).name+'.bin');p.write_bytes(f['data']);subprocess.run([KRAK,str(p),str(dest),str(f['uncomp'])],capture_output=True)
  if dest.exists():
   b=dest.read_bytes();print(name,len(b),'centres',len(re.findall(b'pokemon_center',b)));print([s.decode() for s in re.findall(rb'[A-Za-z_][A-Za-z_0-9./]{5,}',b) if b'pokemon_center' in s or s.endswith(b'trscn')][:35])
