#!/usr/bin/env python3

from collections import namedtuple
import sys, subprocess, math
import argparse

VERSIONS = ['c11', 'rmc', 'sc']
TestGroup = namedtuple('TestGroup',
                       ['name', 'subtests', 'groups', 'params'])

###

def get_binaries(test, groups):
    binaries = set(x for g in groups for x in test.groups[g]
                   if g in test.groups)
    return ['-'.join([test.name] + list(b) + ['test']) for b in binaries]
def run_one(test, groups, run_mult, subtests, branch):
    runs = str(int(math.ceil(run_mult * test.params['base_runs'])))
    binaries = get_binaries(test, groups)
    for name, test_args in test.subtests.items():
        if subtests and name not in subtests: continue
        if branch:
            name += "-" + branch
        cmd = (['./scripts/bench.sh', runs, name, test_args % test.params]
               + binaries)
        subprocess.call(cmd)
def run(tests, groups, scale, subtests, branch):
    for test in tests:
        run_one(test, groups, scale, subtests, branch)

#
def run_branch(f, branch):
    current = subprocess.run(
        "git rev-parse --symbolic-full-name --abbrev-ref HEAD",
        check=True, shell=True, universal_newlines=True,
        stdout=subprocess.PIPE).stdout.strip()
    print(current)
    subprocess.run(['git', 'checkout', 'TEST_'+branch], check=True)
    res = f(branch)
    subprocess.run(['git', 'checkout', current], check=True)

def run_branches(f, branches):
    if not branches: f(None)
    else:
        for branch in branches: run_branch(f, branch)

###
# Test configurations

def tmatches(key, tup): return any(key in part for part in tup)
def fill_tests(gs):
    if 'fixed_lib' not in gs:
        gs['fixed_lib'] = gs.get('fixed_epoch', [])+gs.get('fixed_freelist', [])
    if 'matched_lib' not in gs:
        gs['matched_lib'] = (
            gs.get('matched_epoch', [])+gs.get('matched_freelist', []))
    if 'baseline' not in gs:
        gs['baseline'] = [x for x in gs['matched_lib'] if not tmatches('rmc',x)]
    if 'sensible' not in gs:
        gs['sensible'] = (
            gs.get('fixed_lib', [])+gs.get('matched_lib', [])+gs.get('fixed_object', []))
    if 'sensible_rmc' not in gs:
        gs['sensible_rmc'] = [x for x in gs['sensible'] if tmatches('rmc',x)]
    for v in VERSIONS:
        for thing in ['epoch', 'freelist', 'lib']:
            name = v + '_' + ('only' if thing == 'lib' else thing)
            if name not in gs:
                gs[name] = [x for x in gs.get('matched_'+thing,[])
                            if tmatches(v,x)]

    for test in set(x for l in gs.values() for x in l):
        key = "-".join(test)
        if key not in gs: gs[key] = [test]

def data_struct_test(name):
    subtests = {
        'mpmc': "-p 2 -c 2 -n %(size)d",
        'spsc': "-p 1 -c 1 -n %(size)d",
        'spmc': "-p 1 -c 2 -n %(size)d",
        'alt': "-p 0 -c 0 -t 4 -n %(size)d"
    }
    gs = {
        'fixed_epoch': [('ec11', t) for t in VERSIONS],
        'fixed_freelist': [('fc11', t+'2') for t in VERSIONS],
        'matched_epoch': [('e'+t, t) for t in VERSIONS],
        'matched_freelist': [('f'+t, t+'2') for t in VERSIONS],
        'fixed_object': [('e'+t, 'c11') for t in VERSIONS],
    }
    params = {'size': 10000000, 'base_runs': 50}

    return TestGroup(name, subtests, gs, params)

TESTS = {}
def add_test(test):
    fill_tests(test.groups)
    TESTS[test.name] = test


add_test(data_struct_test('ms_queue'))
add_test(data_struct_test('tstack'))

RCU_VERSIONS = VERSIONS+['linux']
add_test(TestGroup(
    'rculist_user',
    {
        '4x': "-p 0 -c 4 -n %(size)d",
        '2x': "-p 0 -c 2 -n %(size)d",
        'write_heavy_4x': "-p 0 -c 4 -n %(size)d -i 30",
        'write_heavy_2x': "-p 0 -c 2 -n %(size)d -i 30",
    },
    {
        'fixed_epoch': [('ec11', t) for t in RCU_VERSIONS],
        'fixed_object': [('e'+t, 'linux') for t in VERSIONS],
        'matched_epoch': [('e'+t, t) for t in VERSIONS],
    },
    {'size': 3000000, 'base_runs': 20}
))

add_test(TestGroup(
    'seqlock',
    {
        '4x': "-p 0 -c 4 -n %(size)d",
        '2x': "-p 0 -c 2 -n %(size)d",
    },
    {
        bs_key: [('rmc',), ('c11',)]
        for bs_key in ['fixed_lib', 'matched_lib', 'fixed_object']
    },
    {'size': 100000000, 'base_runs': 20}
))

add_test(TestGroup(
    'seqlock-lock',
    {
        '%dx' % i: "-p 0 -c %d -n %%(size)d" % i
        for i in range(1, 5)
    },
    {
        bs_key: [('rmc',), ('c11',),('sc',)]
        for bs_key in ['fixed_lib', 'matched_lib', 'fixed_object']
    },
    {'size': 10000000, 'base_runs': 50}
))

# unfortunately there aren't really variants here??
add_test(TestGroup(
    'ringbuf',
    {
        'ringbuf': "-n %(size)d"
    },
    {
        bs_key: [('rmc',), ('c11',),('sc',)]
        for bs_key in ['fixed_lib', 'matched_lib', 'fixed_object']
    },
    {'size': 100000000, 'base_runs': 50}
))



def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("-t", "--test", action='append')
    parser.add_argument("-g", "--group", action='append')
    parser.add_argument("-b", "--branch", action='append')
    parser.add_argument("-s", "--subtest", action='append')
    # bare groups are only run on the current branch,
    # not on branches given with -b
    parser.add_argument("-G", "--bare_group", action='append')
    parser.add_argument("-d", "--debug", action='store_true')
    parser.add_argument("--scale", type=float, default=1.0)
    args = parser.parse_args()

    if args.debug: print(TESTS)

    tests = [TESTS[t] for t in args.test]
    r = lambda branch: run(tests, args.group, args.scale, args.subtest, branch)
    run_branches(r, args.branch)

    if args.bare_group:
        run(tests, args.bare_group, args.scale, args.subtest, None)

if __name__ == '__main__':
    sys.exit(main(sys.argv))
