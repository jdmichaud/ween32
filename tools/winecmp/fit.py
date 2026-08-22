"""Find, for each character, the advance in font units at which wine measures
what ween32 draws.  Each character is independent, so they all binary-search
at once: one wine run per step."""
import struct, subprocess, os

repo = '/home/jd/ween32'
prefix = os.environ['WINEPREFIX']
fontdst = prefix + '/drive_c/windows/Fonts/tahoma.ttf'
target = {}
for line in open('/tmp/adv.txt'):
    c, a = line.split(); target[int(c)] = int(a)

lo = {c: 0 for c in target}
hi = {c: 3000 for c in target}
cur = {}

def measure(units):
    with open('/tmp/adv_units.txt', 'w') as f:
        for c in sorted(units):
            f.write('%d %d\n' % (c, units[c]))
    subprocess.run(['python3', '/tmp/patchadv.py', repo + '/fonts/tahoma.ttf',
                    '/tmp/tahoma_gdi.ttf', '/tmp/adv_units.txt', '11'],
                   check=True, capture_output=True)
    subprocess.run(['cp', '/tmp/tahoma_gdi.ttf', fontdst], check=True)
    subprocess.run(['wineserver', '-k'], capture_output=True)
    subprocess.run(['sleep', '1'])
    subprocess.run(['wine', '/tmp/dumpadv.exe'], cwd='/tmp', capture_output=True)
    got = {}
    for line in open('/tmp/wineadv.txt'):
        c, a = line.split(); got[int(c)] = int(a)
    return got

for step in range(14):
    cur = {c: (lo[c] + hi[c]) // 2 for c in target}
    got = measure(cur)
    done = 0
    for c in target:
        g = got.get(c)
        if g is None:
            continue
        if g < target[c]:
            lo[c] = cur[c] + 1
        elif g > target[c]:
            hi[c] = cur[c] - 1
        else:
            lo[c] = hi[c] = cur[c]
            done += 1
    print('step %d: %d of %d characters match' % (step, done, len(target)))
    if done == len(target):
        break
    if all(lo[c] > hi[c] for c in target if lo[c] > hi[c]):
        pass
with open('/tmp/adv_units_final.txt', 'w') as f:
    for c in sorted(cur):
        f.write('%d %d\n' % (c, cur[c]))
