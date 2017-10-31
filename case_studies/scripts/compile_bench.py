#!/usr/bin/env python3

import sys, subprocess, time

VERSIONS = ['c11', 'rmc']

TESTS = [
    ('epoch', 'epoch_%(type)s.o'),
    ('ms_queue epoch', 'ms_queue-ec11-%(type)s-test.o'),
    ('ms_queue freelist', 'ms_queue-fc11-%(type)s2-test.o'),
    ('tstack epoch', 'tstack-ec11-%(type)s-test.o'),
    ('tstack freelist', 'tstack-fc11-%(type)s2-test.o'),
    ('rculist', 'rculist_user-ec11-%(type)s-test.o'),
    ('ringbuf', 'ringbuf-%(type)s-test.o'),
    ('qspinlock', 'seqlock-lock-%(type)s-test.o'),
]


def main(argv):
    for (name, obj) in TESTS:
        times = []
        for v in VERSIONS:
            real_obj = 'build/' + obj % {'type': v}
            subprocess.call(["rm", "-f", real_obj])
            start = time.time()
            subprocess.call(['make', real_obj])
            end = time.time()
            times += [end-start]
        row = [name] + [str(t) for t in times]
        print(",".join(row))


if __name__ == '__main__':
    sys.exit(main(sys.argv))
