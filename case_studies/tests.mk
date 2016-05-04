THINGS=ms_queue seqlock rcu tstack rwlocks qspinlock parking condvar

MS_QUEUE_TESTS=ms_queue ms_queue-big
MS_QUEUE_EPOCH_TYPES=sc relacq rmc
MS_QUEUE_FREELIST_TYPES=sc2 rmc2
MS_QUEUE_TYPES=lock 2lock

TSTACK_EPOCH_TYPES=sc rmc
TSTACK_FREELIST_TYPES=sc2 rmc2
TSTACK_TYPES=lock

SEQLOCK_TYPES=sc noop c11 rmc

RWLOCKS_TESTS=seqlock-rwlock
RWLOCKS_TYPES=sc c11 rmc

QSPINLOCK_TESTS=seqlock-lock
QSPINLOCK_TYPES=sc rmc

CONDVAR_TYPES=sc rmc c11

RCU_EPOCH_TYPES=uh

EPOCH_TYPES=sc c11 rmc leak
FREELIST_TYPES=sc rmc leak

PARKING_TYPES=lol

#
EXTRA_LIB_SRCS=llvm-cl/CommandLine.cpp parking.cpp futex.cpp
