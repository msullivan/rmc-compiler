#!/usr/bin/env python3

from collections import namedtuple
import sys, subprocess

VERSIONS = ['c11', 'rmc', 'sc']
TestGroup = namedtuple('TestGroup', ['name', 'subtests', 'groups', 'params'])

###
# Code for executing the stuff

def get_binaries(test, groups):
    binaries = set(x for g in groups for x in test.groups[g])
    return ['-'.join([test.name] + list(b) + ['test']) for b in binaries]
def run(test, groups, runs):
    binaries = get_binaries(test, groups)
    for name, test_args in test.subtests.items():
        cmd = (['./scripts/bench.sh', str(runs), name, test_args % test.params]
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
        'fixed_queue': [('e'+t, 'c11') for t in VERSIONS+['c11simp']],
        'rmc_only': [('ermc', 'rmc'), ('frmc', 'rmc2')],
    }
    gs['fixed_lib'] = gs['fixed_epoch'] + gs['fixed_freelist']
    gs['matched_lib'] = gs['matched_epoch'] + gs['matched_freelist']
    params = {'size': 10000000}

    return TestGroup(name, subtests, gs, params)

ms_queue_test = data_struct_test('ms_queue')
tstack_test = data_struct_test('tstack')




def main(args):
    test = data_struct_test(args[1])
    print(test)
    run(test, ['fixed_epoch', 'fixed_freelist'], 1)

if __name__ == '__main__':
    sys.exit(main(sys.argv))
