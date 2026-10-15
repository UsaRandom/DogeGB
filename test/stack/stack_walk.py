#!/usr/bin/env python3
"""Measure stack headroom of a built ROM in the emulator.

Before each operation, the RAM between the end of the globals (s__HEAP)
and the current stack pointer is filled with 0xA5. Afterwards the
lowest byte that changed shows how deep the stack went. The headroom is
that byte's address minus the end of the globals. The run fails if any
flow has less than --min bytes, or if address generation does not reach
the wallet menu.

Flows: PIN setup, first-wallet address generation, receive QR, signing
(1 and 4 inputs; one durable-nonce transfer on SolGB), second-wallet
address generation, PIN entry after reboot, duress wipe.

Usage:
  python3 test/stack/stack_walk.py build/DogeGB.gb [--coin doge|sol] [--min 200] [--json out.json]
Needs: pyboy, numpy, node. Takes a few minutes per ROM.
"""
import argparse, io, json, os, random, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from gbh import GB, PIN, boot_to_menu, new_wallet, menu_select
from kb import Typer
from pyboy import PyBoy

ap = argparse.ArgumentParser()
ap.add_argument('rom'); ap.add_argument('--map')
ap.add_argument('--coin', choices=['doge', 'sol'], default='doge')
ap.add_argument('--min', type=int, default=200)
ap.add_argument('--json')
a = ap.parse_args()
mapf = a.map or a.rom[:-3] + '.map'
codec = os.path.join(HERE, '..', '..', 'docs', 'codec.js')
T0 = time.time(); R = {}; steps = []

def step(name, fn):
    t = time.time(); v = fn(); dt = time.time() - t
    steps.append((name, v, dt)); print(f'{name:28s} {v!s:>6} B  {dt:6.1f} s', flush=True)

def to_wallet_menu(g):
    for _ in range(5):
        if 'Receive' in g.text(): return
        g.press('b', 8, 90)

g = GB(a.rom, mapf); g.rng = random.Random(1)
g.tick(200)
g.wait_for(lambda: any(k in g.text() for k in ('Create PIN', 'Empty')), 6000, 30)

def pin_set():
    g.canary_fill(); boot_to_menu(g); return g.margin()
step('pin_set', pin_set)

def addrgen(slot):
    def f():
        new_wallet(g, slot, canary=True)
        if 'Receive' not in g.text(): raise SystemExit('FAIL: address generation did not finish:\n' + g.text())
        return g.margin()
    return f
step('addrgen_first', addrgen(0))

def qr():
    g.canary_fill(); menu_select(g, 'Receive'); g.tick(120); m = g.margin(); g.press('b', 8, 90); return m
step('receive_qr', qr)

def sign(n):
    def f():
        mode = 'sol' if a.coin == 'sol' else 'doge'
        groups = json.loads(subprocess.run(['node', os.path.join(HERE, 'payment.mjs'), codec, mode, str(n)],
                                           check=True, capture_output=True, text=True).stdout)
        to_wallet_menu(g); menu_select(g, 'Sign Tx'); g.tick(30); g.press('a', 8, 120)
        t = Typer(g)
        for i, grp in enumerate(groups): t.type_group(grp, i)
        if not g.wait_for('Review', 1200, 30): raise SystemExit('FAIL: no review screen:\n' + g.text())
        g.tick(30); g.canary_fill()
        g.press('start', 8, 60); g.press('start', 8, 120)
        for _ in range(400):
            g.tick(600, False)
            if 'Signing' not in g.text(): break
        g.tick(120)
        for _ in range(8): g.press('right', 6, 60)   # page through the signature QRs
        m = g.margin()
        g.press('b', 8, 60); g.press('down', 6, 20); g.press('a', 8, 150)
        return m
    return f
if a.coin == 'sol':
    step('sign_sol_nonce_transfer', sign(1))
else:
    step('sign_1_input', sign(1)); step('sign_4_inputs', sign(4))

to_wallet_menu(g); g.press('b', 8, 120)
step('addrgen_second', addrgen(1))

def reboot():
    global g
    b = io.BytesIO(); g.pb.stop(ram_file=b)
    n = GB.__new__(GB); n.__dict__.update(g.__dict__)
    n.pb = PyBoy(a.rom, window='null', sound_emulated=False, log_level='ERROR', ram_file=io.BytesIO(b.getvalue()))
    n.pb.set_emulation_speed(0); g = n
    g.tick(200); g.wait_for('Enter PIN', 6000, 30)

def pin_enter():
    reboot(); g.canary_fill(); boot_to_menu(g); return g.margin()
step('pin_enter', pin_enter)

def duress():
    reboot(); g.canary_fill()
    for b in reversed(PIN): g.press(b, 8, 20)
    g.tick(60); g.press('start', 8, 200)
    g.wait_for(lambda: 'Empty' in g.text(), 20000, 60); g.tick(200)
    if 'Slot' in g.text() and 'Empty' not in g.text(): raise SystemExit('FAIL: duress wipe')
    return g.margin()
step('duress_wipe', duress)

worst = min(steps, key=lambda s: s[1])
ok = worst[1] >= a.min
print(f'worst: {worst[0]} {worst[1]} B (minimum {a.min} B), total {time.time()-T0:.0f} s: {"PASS" if ok else "FAIL"}')
if a.json:
    json.dump({'rom': a.rom, 'min': a.min, 'pass': ok, 'total_s': round(time.time()-T0, 1),
               'flows': {n: {'headroom': v, 'seconds': round(t, 1)} for n, v, t in steps}}, open(a.json, 'w'), indent=1)
sys.exit(0 if ok else 1)
