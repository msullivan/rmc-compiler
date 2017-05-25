#!/usr/bin/env python3

from collections import namedtuple
import sys, subprocess, math

VERSIONS = ['c11', 'rmc', 'sc']
TestGroup = namedtuple('TestGroup',
                       ['name', 'subtests', 'groups', 'params'])

###
# Code for executing the stuff

def get_binaries(test, groups):
    binaries = set(x for g in groups for x in test.groups[g])
    return ['-'.join([test.name] + list(b) + ['test']) for b in binaries]
def run(test, groups, run_mult):
    runs = str(int(math.ceil(run_mult * test.params['base_runs'])))
    binaries = get_binaries(test, groups)
    for name, test_args in test.subtests.items():
        cmd = (['./scripts/bench.sh', runs, name, test_args % test.params]
               + binaries)
        subprocess.call(cmd)

###
# Test configurations

def data_struct_test(name):
    subtests = {
        'mpmc': "-p 2 -c 2 -n %(size)d",
        'spsc': "-p 1 -c 1 -n %(size)d",
        'spmc': "-p 1 -c 2 -n %(size)d",
        'hammer': "-p 0 -c 0 -t 4 -n %(size)d"
    }
    gs = {
        'fixed_epoch': [('ec11', t) for t in VERSIONS],
        'fixed_freelist': [('fc11', t+'2') for t in VERSIONS],
        'matched_epoch': [('e'+t, t) for t in VERSIONS],
        'matched_freelist': [('f'+t, t+'2') for t in VERSIONS],
        'fixed_object': [('e'+t, 'c11') for t in VERSIONS+['c11simp']],
        'rmc_only': [('ermc', 'rmc'), ('frmc', 'rmc2')],
    }
    gs['fixed_lib'] = gs['fixed_epoch'] + gs['fixed_freelist']
    gs['matched_lib'] = gs['matched_epoch'] + gs['matched_freelist']
    params = {'size': 10000000, 'base_runs': 50}

    return TestGroup(name, subtests, gs, params)

TESTS = {}
def add_test(test):
    TESTS[test.name] = test


add_test(data_struct_test('ms_queue'))
add_test(data_struct_test('tstack'))

RCU_VERSIONS = VERSIONS+['linux']
add_test(TestGroup(
    'rcu',
    {
        '4x': "-p 0 -c 4 -n %(size)d",
        '2x': "-p 0 -c 2 -n %(size)d",
        'write_heavy_4x': "-p 0 -c 4 -n %(size)d -i 30",
        'write_heavy_2x': "-p 0 -c 2 -n %(size)d -i 30",
    },
    {
        'fixed_lib': [('ec11', t) for t in RCU_VERSIONS],
        'fixed_object': [('e'+t, 'linux') for t in VERSIONS],
        'matched_lib': [('e'+t, t) for t in RCU_VERSIONS],
        'rmc_only': [('ermc', 'rmc')],
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
        'rmc_only': [('rmc',)],
    },
    {'size': 100000000, 'base_runs': 20}
))



def main(args):
    run(TESTS[args[1]], args[2:], 1/50.)

if __name__ == '__main__':
    sys.exit(main(sys.argv))
