from pathlib import Path
import struct,json
class Lua:
 def __init__(self,b):self.b=b;self.p=32;self.protos=[]
 def raw(self,n):r=self.b[self.p:self.p+n];self.p+=n;assert len(r)==n;return r
 def byte(self):return self.raw(1)[0]
 def uint(self):
  v=0
  for i in range(10):
   x=self.byte();v=(v<<7)|(x&127)
   if x&128:return v
  raise ValueError('varuint')
 def string(self):
  n=self.uint();return self.raw(n-1).decode('utf8','replace') if n else ''
 def proto(self,name='root'):
  start=self.p;src=self.string();line=self.uint();last=self.uint();params=self.raw(3);nc=self.uint();code=list(struct.unpack('<%dI'%nc,self.raw(nc*4)));const=[]
  for i in range(self.uint()):
   t=self.byte()
   if t in (0,1,17):v=None if t==0 else t==17
   elif t==3:v=struct.unpack('<Q',self.raw(8))[0]
   elif t==19:v=struct.unpack('<d',self.raw(8))[0]
   elif t in (4,20):v=self.string()
   else:raise ValueError((t,self.p))
   const.append(v)
  nu=self.uint();self.raw(nu*3);children=[]
  for i in range(self.uint()):children.append(self.proto(name+'/'+str(i)))
  self.raw(self.uint());absline=self.uint()
  for _ in range(absline):self.uint();self.uint()
  for _ in range(self.uint()):self.string();self.uint();self.uint()
  ups=[self.string() for _ in range(self.uint())]
  ob={'name':name,'source':src,'start':start,'end':self.p,'line':line,'const':const,'code':code,'children':children};self.protos.append(ob);return ob
if __name__=='__main__':
 for tag,p in [('event','script-events/event_0.out'),('main','script-main/main_0.out')]:
  l=Lua(Path('companions/za-validation/'+p).read_bytes());r=l.proto();print(tag,len(l.b),l.p,'protos',len(l.protos));Path('companions/za-validation/'+tag+'-lua.json').write_text(json.dumps(l.protos,ensure_ascii=True))
