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
def run_one(test, groups, run_mult, branch):
    runs = str(int(math.ceil(run_mult * test.params['base_runs'])))
    binaries = get_binaries(test, groups)
    for name, test_args in test.subtests.items():
        if branch:
            name += "-" + branch
        cmd = (['./scripts/bench.sh', runs, name, test_args % test.params]
               + binaries)
        subprocess.call(cmd)
def run(tests, groups, scale, branch):
    for test in tests:
        run_one(test, groups, scale, branch)

#
def run_branch(f, branch):
    current = subprocess.run(
        "git rev-parse --symbolic-full-name --abbrev-ref HEAD",
        check=True, shell=True, universal_newlines=True,
        stdout=subprocess.PIPE).stdout.strip()
    print(current)
    subprocess.run(['git', 'checkout', 'TEST_'+branch], check=True)
    subprocess.run(['make', '-C', '..'], check=True)
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
    if 'rmc_only' not in gs:
        gs['rmc_only'] = [x for x in gs['matched_lib'] if tmatches('rmc',x)]
    if 'c11_only' not in gs:
        gs['c11_only'] = [x for x in gs['matched_lib'] if tmatches('c11',x)]

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
        'fixed_object': [('e'+t, 'c11') for t in VERSIONS+['c11simp']],
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
        'fixed_lib': [('rmc',), ('c11',)],
        'matched_lib': [('rmc',), ('c11',)],
    },
    {'size': 100000000, 'base_runs': 20}
))



def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("-t", "--test", action='append')
    parser.add_argument("-g", "--group", action='append')
    parser.add_argument("-b", "--branch", action='append')
    parser.add_argument("-d", "--debug", action='store_true')
    parser.add_argument("--scale", type=float, default=1.0)
    args = parser.parse_args()

    if args.debug: print(TESTS)

    tests = [TESTS[t] for t in args.test]
    r = lambda branch: run(tests, args.group, args.scale, branch)
    run_branches(r, args.branch)

if __name__ == '__main__':
    sys.exit(main(sys.argv))
