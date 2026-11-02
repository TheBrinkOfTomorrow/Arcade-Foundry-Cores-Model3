import random, sys
# gen_sw.py SEED START END: seeded random analog stick sweeps with triggers and event buttons
seed, start, end = map(int, sys.argv[1:4]); r = random.Random(seed)
f = start
while f < end:
    n = r.choice([10, 20, 40, 60])
    keys = [f"stickx={r.choice([0, 48, 96, 128, 160, 208, 255])}", f"sticky={r.choice([0, 48, 96, 128, 160, 208, 255])}"]
    keys += r.choice([[], ['trigger1=1'], ['trigger1=1'], ['trigger2=1'], ['trigger1=1', 'trigger2=1'], ['event1=1'], ['event2=1']])
    print(f"{f}-{f + n} " + ' '.join(keys))
    f += n + r.randrange(1, 10)
