import sys,csv,numpy as np
from PIL import Image
d=sys.argv[1]; o=int(sys.argv[2]); tmin=float(sys.argv[3]) if len(sys.argv)>3 else -50; tmax=float(sys.argv[4]) if len(sys.argv)>4 else 3000
rows=[r for r in csv.reader(open(d+'/frames.tsv'),delimiter='\t')][1:]
fr=[(int(r[2]),float(r[3])) for r in rows if int(r[0])==o]
def load(i):
    return np.asarray(Image.open(f'{d}/o{o}_f{i:03d}.png').convert('L'),dtype=np.float32)
last=load(fr[-1][0])
# titlebar: rows at top whose mean differs from the page's first rows; find first row where the column-variance pattern changes -> use 28 px heuristic by scanning grey chrome
T=0
for y in range(0,60):
    if abs(last[y].mean()-last[0].mean())>25: T=y; break
F=last[T:]
H,W=F.shape
ds=4
Fi=Image.fromarray(F.astype(np.uint8))
cache={}
def scaled(s):
    if s in cache: return cache[s]
    w=int(round(W*s/ds)); h=int(round(H*s/ds))
    a=np.asarray(Fi.resize((w,h),Image.BILINEAR),dtype=np.float32); cache[s]=a; return a
out=[]
prev=None
for i,ms in fr:
    if ms<tmin or ms>tmax: continue
    X=load(i)[T:]
    Xd=np.asarray(Image.fromarray(X.astype(np.uint8)).resize((W//ds,H//ds),Image.BILINEAR),dtype=np.float32)
    std=Xd.std()
    if std<6: out.append((i,ms,None,std,0)); continue
    best=(1e9,None)
    for s in np.arange(0.5,2.01,0.01):
        s=round(float(s),2); A=scaled(s)
        h=min(A.shape[0],Xd.shape[0]); w=min(A.shape[1],Xd.shape[1])
        e=np.abs(A[:h,:w]-Xd[:h,:w]).mean()
        if e<best[0]: best=(e,s)
    out.append((i,ms,best[1],std,best[0]))
for i,ms,s,std,e in out:
    tag = 'blank(std %.1f, mean %.0f)'%(std,0) if s is None else 'scale %.2f  err %.1f'%(s,e)
    print('%4d %8.1f ms  %s'%(i,ms,tag))
