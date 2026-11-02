import random, sys
# gen_vo.py SEED START END: seeded random twin-stick input (both levers, shots, turbos)
seed, start, end = map(int, sys.argv[1:4]); r = random.Random(seed)
dirs = [[], ['up'], ['down'], ['left'], ['right']]
f = start
while f < end:
    n = r.choice([5, 10, 20, 40, 60])
    keys = r.choice(dirs) + [k + '2' for k in r.choice(dirs)] + r.choice([[], [], ['shot1'], ['shot2'], ['shot1', 'shot2'], ['turbo1'], ['turbo2']])
    if keys:
        print(f"{f}-{f + n} " + ' '.join(k + '=1' for k in keys))
    f += n + r.randrange(2, 20)
