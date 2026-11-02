import random, sys
# gen_btn.py SEED START END BTN,BTN,... [SUFFIX]: seeded random joystick + buttons
seed, start, end = map(int, sys.argv[1:4]); names = sys.argv[4].split(','); suf = sys.argv[5] if len(sys.argv) > 5 else ''
r = random.Random(seed)
dirs = [[], ['up'], ['down'], ['left'], ['right'], ['up', 'left'], ['up', 'right'], ['down', 'left'], ['down', 'right']]
btns = [[]] + [[n] for n in names] + [[names[i], names[(i + 1) % len(names)]] for i in range(len(names))]
f = start + r.randrange(0, 10)
while f < end:
    n = r.choice([3, 5, 8, 12, 20, 30, 50])
    keys = [k + suf for k in r.choice(dirs) + r.choice(btns)]
    if keys:
        print(f"{f}-{f + n} " + ' '.join(k + '=1' for k in keys))
    f += n + r.randrange(2, 25)
