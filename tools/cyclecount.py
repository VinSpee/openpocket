#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A static cycle estimate of the loops in a pi32v2 disassembly (objdump -d --mcpu=r3), from the costs measured on the
pedal and written in docs/HARDWARE-HANDBOOK.md section 1: load 2, store 1.5, ALU 1, float add or multiply 2, float
division 4, taken branch 6.4, call and return 90. Two instructions issued together (the '#' pair) cost the larger of
the two. A conditional forward branch counts as taken half the time. Conversions between integer and float count as 2
(not in the handbook: an assumption). It leaves out the extra cycle when an instruction uses a value just loaded, the
slower fetch of integer instructions from RAM, and the stall between consecutive multiply-adds, so it may read low.
Prints, per function, each loop (found by its backward branch) with the cycles of one turn, its own instructions only,
and how many loops are nested in it. docs/PALETTE-REPORT.md uses it for an app's engine.
  objdump -d --mcpu=r3 OBJ.o > DISASM.txt        (the toolchain's objdump, in the container)
  python3 tools/cyclecount.py DISASM.txt [FUNCTION...]
"""
import re, sys
LINE = re.compile(r'^\s+([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)$')
def cost(ins):
    s = re.sub(r'<[^>]*>', '', ins).strip()
    if not s or s == '}': return 0.0, 'none'
    if s.startswith('call'): return 90.0, 'call'
    if re.match(r'^(if|ifs|iff)\b.*goto', s) or s.startswith('goto'): return 0.0, 'branch'   # handled by the caller
    if re.match(r'^(if|ifs|iff)\b.*\{', s): return 1.0, 'cond'
    if re.match(r'^[bhd]?\[', s) or re.match(r'^\{.*\} = \[', s):
        return (1.5, 'store') if not s.startswith('{') else (2.0, 'load')
    if re.search(r'= [bhd]?\[', s): return 2.0, 'load'
    if '(f)' in s: return (4.0, 'fdiv') if ' / ' in s else (2.0, 'fop')
    if re.search(r'itof|utof|ftoi', s): return 2.0, 'conv'
    if re.search(r'\*', s): return 2.0, 'mul'
    return 1.0, 'alu'
def main(path, only=None):
    funcs, cur = {}, None
    for ln in open(path):
        m = re.match(r'^(\w+):\s*$', ln)
        if m: cur = m.group(1); funcs[cur] = []; continue
        m = LINE.match(ln)
        if m and cur:
            addr, ins = int(m.group(1), 16), m.group(3)
            paired_second = ln.split('\t')[-1].startswith(' ') and bool(funcs[cur]) and funcs[cur][-1][1].rstrip().endswith('#')
            funcs[cur].append((addr, ins, paired_second))
    for name, body in funcs.items():
        if only and name not in only: continue
        loops = []
        for k, (addr, ins, _) in enumerate(body):
            m = re.search(r'goto (-\d+)', ins)
            if m:
                tgt = re.search(r'\+0x([0-9A-Fa-f]+) :', ins)
                start = int(tgt.group(1), 16) if tgt else addr + int(m.group(1))
                loops.append((start, addr))
        loops.sort(key=lambda l: (l[0], -l[1]))
        print(f'{name}: {len(body)} instructions, {len(loops)} loop(s)')
        for (s, e) in loops:
            inner = [(a, b) for (a, b) in loops if (a, b) != (s, e) and a >= s and b <= e]
            tot, kinds, i = 0.0, {}, 0
            items = [(a, ins, p) for (a, ins, p) in body if s <= a <= e and not any(ia <= a <= ib for ia, ib in inner)]
            while i < len(items):
                a, ins, p = items[i]; c, kd = cost(ins)
                if kd == 'branch':
                    back = a == e
                    c = 6.4 if back or ins.strip().startswith('goto') else 1.0 + 3.2
                    kd = 'loopbr' if back else 'br'
                if ins.rstrip().endswith('#') and i + 1 < len(items):      # issued together with the next one
                    c2, k2 = cost(items[i + 1][1]); kinds[k2] = kinds.get(k2, 0) + 1; c = max(c, c2); i += 1
                kinds[kd] = kinds.get(kd, 0) + 1; tot += c; i += 1
            ks = ' '.join(f'{k}{v}' for k, v in sorted(kinds.items()))
            print(f'   loop {s:#06x}..{e:#06x}: {tot:7.1f} cycles a turn (own code) [{ks}]' + (f'  contains {len(inner)} inner loop(s)' if inner else ''))
if __name__ == '__main__':
    main(sys.argv[1], set(sys.argv[2:]) or None)
