import random, sys
# gen_fight.py SEED START END [p2]: seeded random fighting input, one line per press
seed, start, end = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]); p2 = len(sys.argv) > 4
r = random.Random(seed)
dirs = [[], ['up'], ['down'], ['left'], ['right'], ['up', 'left'], ['up', 'right'], ['down', 'left'], ['down', 'right']]
btns = [['punch'], ['kick'], ['guard'], ['punch', 'guard'], ['kick', 'guard'], ['punch', 'kick'], []]
for player in ([''] + (['2'] if p2 else [])):
    f = start + r.randrange(0, 20)
    while f < end:
        length = r.choice([3, 5, 8, 12, 20, 30])
        keys = [k + player for k in r.choice(dirs) + r.choice(btns)]
        if keys:
            print(f"{f}-{f + length} " + ' '.join(k + '=1' for k in keys))
        f += length + r.randrange(2, 25)
