import random, sys
# gen_gun.py SEED START END [p2]: seeded random aim sweeps with the trigger pulled,
# and now and then a shot off screen (gun at an edge) to reload
seed, start, end = map(int, sys.argv[1:4]); p2 = len(sys.argv) > 4
r = random.Random(seed)
for sfx in [''] + (['2'] if p2 else []):
    f = start + r.randrange(0, 10)
    while f < end:
        n = r.choice([5, 10, 20, 30])
        if r.random() < 0.08:
            print(f"{f}-{f + n} gunx{sfx}=0 guny{sfx}=0 lefttrigger{sfx}=1")
        else:
            x, y = r.randrange(16, 240), r.randrange(16, 240)
            t = f" lefttrigger{sfx}=1" if r.random() < 0.7 else ''
            print(f"{f}-{f + n} gunx{sfx}={x} guny{sfx}={y}{t}")
        f += n + r.randrange(1, 8)
