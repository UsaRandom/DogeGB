from collections import deque
K=[(0,10,'1',0,0),(2,10,'2',0,0),(4,10,'3',0,0),(6,10,'4',0,0),(8,10,'5',0,0),(10,10,'6',0,0),(12,10,'7',0,0),(14,10,'8',0,0),(16,10,'9',0,0),
(0,12,'Q',1,1),(2,12,'W',1,1),(4,12,'E',1,1),(6,12,'R',1,1),(8,12,'T',1,1),(10,12,'Y',1,1),(12,12,'U',1,1),(14,12,'I',1,1),(16,12,'O',1,1),(18,12,'P',1,1),
(1,14,'A',2,1),(3,14,'S',2,1),(5,14,'D',2,1),(7,14,'F',2,1),(9,14,'G',2,1),(11,14,'H',2,1),(13,14,'J',2,1),(15,14,'K',2,1),(17,14,'L',2,1),
(0,16,None,3,0),(3,16,'Z',3,1),(5,16,'X',3,1),(7,16,'C',3,1),(9,16,'V',3,1),(11,16,'B',3,1),(13,16,'N',3,1),(15,16,'M',3,1)]
N=len(K)
def hidden(i,sh):
    c=K[i][2]
    if c=='L': return sh
    if c in('I','O'): return not sh
    return False
def first(row,sh): return next(i for i in range(N) if K[i][3]==row and not hidden(i,sh))
def last(row,sh): return [i for i in range(N) if K[i][3]==row and not hidden(i,sh)][-1]
def closest(row,x,sh):
    best=first(row,sh); bd=255
    for i in range(N):
        if K[i][3]!=row or hidden(i,sh): continue
        d=abs(K[i][0]-x)
        if d<bd or (d==bd and K[i][0]>K[best][0]): bd=d; best=i
    return best
def step(fr,d,sh):
    row=K[fr][3]; i=fr
    while True:
        if d<0:
            if i==0 or K[i-1][3]!=row: return last(row,sh)
            i-=1
        else:
            if i+1>=N or K[i+1][3]!=row: return first(row,sh)
            i+=1
        if not hidden(i,sh): return i
def move(sel,b,sh):
    row=K[sel][3]
    if b=='left': return step(sel,-1,sh)
    if b=='right': return step(sel,1,sh)
    if b=='up': return None if row==0 else closest(row-1,K[sel][0],sh)
    if b=='down': return closest(row+1,K[sel][0],sh) if row<3 else sel
def path(sel,tgt,sh):
    q=deque([(sel,[])]); seen={sel}
    while q:
        s,p=q.popleft()
        if s==tgt: return p
        for b in('left','right','up','down'):
            n=move(s,b,sh)
            if n is None or n in seen: continue
            seen.add(n); q.append((n,p+[b]))
def target(c):
    if c.isdigit(): return next(i for i in range(N) if K[i][2]==c), None
    return next(i for i in range(N) if K[i][2]==c.upper()), c.islower()
class Typer:
    def __init__(s,g): s.g=g; s.sel=9; s.sh=False
    def p(s,b): s.g.press(b,4,10)
    def type(s,c):
        t,need=target(c)
        if need is not None and need!=s.sh:
            s.p('select'); s.sh=need
            if hidden(s.sel,s.sh): s.sel=closest(K[s.sel][3],K[s.sel][0],s.sh)
        for b in path(s.sel,t,s.sh): s.p(b); s.sel=move(s.sel,b,s.sh)
        s.p('a')
def _shown(s,gi):
    r=s.g.rows()
    line=[l for l in r if '_' in l or True][6]
    x0=1 if gi%2==0 else 11
    return line[x0:x0+8]
def type_group(s,grp,gi):
    for attempt in range(4):
        for c in grp[:7]: s.type(c)
        s.g.tick(10)
        got=_shown(s,gi)[:7]
        if got==grp[:7]: break
        print('retype group',gi,repr(got),grp,flush=True)
        k=len(got.rstrip('_').replace('_',''))
        for _ in range(k): s.p('b')
        s.g.tick(10)
        # ensure group cleared
    s.type(grp[7])
Typer.type_group=type_group; Typer._shown=_shown
