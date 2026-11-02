import random, sys
# gen_fish.py SEED START END: seeded random fishing: rod swings and pulls, reeling, casts, the lure stick
seed, start, end = map(int, sys.argv[1:4]); r = random.Random(seed)
f = start
while f < end:
    n = r.choice([10, 20, 40, 60, 90])
    keys = [f"rodx={r.choice([0, 64, 128, 192, 255])}", f"rody={r.choice([0, 64, 128, 192, 255])}", f"reel={r.choice([0, 0, 64, 160, 255])}"]
    keys += r.choice([[], [], ['cast=1'], ['left=1'], ['right=1'], ['up=1'], ['down=1']])
    print(f"{f}-{f + n} " + ' '.join(keys))
    f += n + r.randrange(1, 15)
