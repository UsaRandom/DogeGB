import numpy as np, random, re, io
from pyboy import PyBoy
class GB:
    def __init__(s,rom,mapf=None,seed=7):
        s.pb=PyBoy(rom,window="null",sound_emulated=False,log_level="ERROR"); s.pb.set_emulation_speed(0)
        s.F=0; random.seed(seed); s.rng=random.Random(seed)
        s.gend=None
        if mapf:
            t=open(mapf).read(); m=re.search(r'0000([0-9A-F]{4})  s__HEAP ',t); s.gend=int(m.group(1),16)
            s.sym={n:int(a,16) for a,n in re.findall(r'\b0000([0-9A-F]{4})  (_\w+)',t)}
    def tick(s,n=1,render=True):
        s.pb.tick(n,render); s.F+=n
    def press(s,b,hold=8,after=20):
        s.pb.button_press(b); s.tick(hold); s.pb.button_release(b); s.tick(after)
    def rows(s):
        m=s.pb.memory; base=0x9C00 if m[0xFF40]&8 else 0x9800
        return ["".join(chr(c+32) if 0<c<96 else ' ' for c in (m[base+y*32+x] for x in range(20))) for y in range(18)]
    def text(s): return "\n".join(r.rstrip() for r in s.rows() if r.strip())
    def has(s,t): return t in s.text()
    def wait_for(s,t,maxf=600000,step=60):
        g=0
        while g<maxf:
            if (callable(t) and t()) or (not callable(t) and s.has(t)): return True
            s.tick(step,False); g+=step
        return False
    def save(s): b=io.BytesIO(); s.pb.save_state(b); return b.getvalue()
    def load(s,data): s.pb.load_state(io.BytesIO(data))
    # canary
    def canary_fill(s,pat=0xA5):
        sp=s.pb.register_file.SP
        assert s.gend < sp <= 0xE000, hex(sp)
        for a in range(s.gend, sp-2): s.pb.memory[a]=pat
        s.pat=pat
    def canary_low(s):
        for a in range(s.gend,0xE000):
            if s.pb.memory[a]!=s.pat: return a
    def margin(s):
        return s.canary_low()-s.gend
    def scr(s): return np.array(s.pb.screen.ndarray[:,:,:3])
    def bonk(s):
        tb=s.sym['_target_b']; names=['a','b','up','down','left','right']
        s.tick(30); title=s.rows()[1]; n=0; w=-1; gone=0
        while True:
            s.tick(1)
            if s.F%30==0:
                if s.rows()[1]!=title: gone+=1
                else: gone=0
                if gone>=3: return n
            k=s.pb.memory[tb]
            if k<6:
                if w<0: w=s.rng.randint(6,28)
                w-=1
                if w==0:
                    b=names[k]; s.pb.button_press(b); s.tick(s.rng.randint(3,10)); s.pb.button_release(b); w=-1; n+=1
            else: w=-1
PIN=['up','right','a','b','left','down']
def boot_to_menu(g,pin=PIN,first=True):
    g.wait_for(lambda: 'PIN' in g.text() or 'Slot' in g.text() or 'Empty' in g.text(), 6000, 30)
    g.tick(60)
    t=g.text()
    if 'Create PIN' in t:
        g.press('a',8,90)
        for b in pin: g.press(b,8,20)
        g.tick(60); g.press('start',8,200)
        for b in pin: g.press(b,8,20)
        g.tick(60); g.press('start',8,200); g.tick(100)
    elif 'Enter PIN' in t:
        for b in pin: g.press(b,8,20)
        g.tick(60); g.press('start',8,200); g.tick(100)
def new_wallet(g,slot=0,canary=None):
    """from main menu: select slot, New Wallet, bonk, words, quiz, address gen. returns (words, margin_list)"""
    for i in range(slot): g.press('down',6,14)
    g.press('a',8,90); g.tick(60); g.press('a',8,120)
    n=g.bonk()
    g.wait_for('Backup Words',3000,30); g.tick(60)
    words=[l.split('.',1)[1].strip() for l in g.rows() if re.match(r'\s*\d+\.',l)][:12]
    g.wait_for('Create Wallet',3000,30)
    # cursor to Create Wallet is default? press a
    g.press('a',8,90)
    for t in range(2):
        g.tick(30); r=g.rows()
        t0=[i for i,l in enumerate(r) if '#' in l][0]; num=int(r[t0].split('#')[1])
        opts=[l.strip() for l in r[t0+3:t0+13]]
        idx=opts.index(words[num-1]); g.tick(60)
        for i in range(idx): g.press('down',6,14)
        g.tick(30)
        if t==1 and canary: g.canary_fill()
        g.press('a',8,90)
    g.tick(60)
    ok=g.wait_for(lambda: not g.has('Address Gen'),700000,600)
    g.tick(120)
    return words,n

def cursor_row(g):
    m=g.pb.memory
    for i in range(40):
        y=m[0xFE00+i*4]; x=m[0xFE00+i*4+1]
        if 16<=y<160 and 0<x<40: return (y-16)//8
def menu_select(g,label,off=0):
    r=g.rows(); tr=[i for i,l in enumerate(r) if label in l][0]+off
    for _ in range(30):
        c=cursor_row(g)
        if c==tr: break
        g.press('down' if (c is None or c<tr) else 'up',6,16)
    g.press('a',8,120)
