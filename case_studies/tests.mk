THINGS=ms_queue seqlock rcu tstack rwlocks qspinlock parking condvar \
	rculist_user tune_work four_slot

MS_QUEUE_TESTS=ms_queue ms_queue-big
MS_QUEUE_EPOCH_TYPES=sc relacq rmc c11 barrier
MS_QUEUE_FREELIST_TYPES=sc2 rmc2 c112 relacq2
MS_QUEUE_TYPES=lock 2lock

TSTACK_EPOCH_TYPES=sc rmc c11
TSTACK_FREELIST_TYPES=sc2 rmc2 c112
TSTACK_TYPES=lock

SEQLOCK_TYPES=sc noop c11 rmc

FOUR_SLOT_TYPES=sc

RWLOCKS_TESTS=seqlock-rwlock
RWLOCKS_TYPES=sc c11 rmc

QSPINLOCK_TESTS=seqlock-lock
QSPINLOCK_TYPES=sc rmc relacq

CONDVAR_TYPES=sc rmc c11

RCU_EPOCH_TYPES=uh
TUNE_WORK_TYPES=uh

RCULIST_USER_EPOCH_TYPES=c11 rmc linux sc

EPOCH_TYPES=sc c11 rmc leak c11simp
FREELIST_TYPES=sc rmc c11 leak

PARKING_TYPES=lol

#
EXTRA_LIB_SRCS=llvm-cl/CommandLine.cpp parking.cpp futex.cpp
