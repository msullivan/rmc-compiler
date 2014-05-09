
/* Lockfree ringbuffers: works if there is at most one writer,
 * at most one reader at a time. */
/* see https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/tree/Documentation/circular-buffers.txt
 * and https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/tree/Documentation/memory-barriers.txt
 * */

#define KBD_BUF_SIZE 1024

/**** Linux style atomic primitives, adapted from linux header files. ****/
/**** release/acquire/consume are like what C++ does */

/* Keep the compiler honest */
#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))

/* Optimization barrier */
/* The "volatile" is due to gcc bugs */
#define barrier() __asm__ __volatile__("":::"memory")

#if defined(i386) || defined(__x86_64)

#define smp_mb() __asm__ __volatile__("mfence":::"memory")
#define smp_rmb() __asm__ __volatile__("lfence":::"memory")
#define smp_wmb() __asm__ __volatile__("sfence" ::: "memory")

/* These are for x86 */
#define smp_store_release(p, v)                  \
    do {                                         \
        barrier();                               \
        ACCESS_ONCE(*p) = v;                     \
    } while (0)
#define smp_load_acquire(p)                      \
    ({                                           \
    typeof(*p) __v = ACCESS_ONCE(*p);            \
    barrier();                                   \
    __v;                                         \
    })
#define smp_load_consume(p)                      \
    ({                                           \
    typeof(*p) __v = ACCESS_ONCE(*p);            \
    barrier();                                   \
    __v;                                         \
    })

#elif defined(__arm__)

#define smp_mb() __asm__ __volatile__("dmb":::"memory")
#define smp_rmb smp_mb
#define smp_wmb smp_mb

/* These are for ARM */
#define smp_store_release(p, v)                  \
    do {                                         \
        smp_mb();                                \
        ACCESS_ONCE(*p) = v;                     \
    } while (0)
#define smp_load_acquire(p)                      \
    ({                                           \
    typeof(*p) __v = ACCESS_ONCE(*p);            \
    smp_mb();                                    \
    __v;                                         \
    })
#define smp_load_consume(p)                      \
    ({                                           \
    typeof(*p) __v = ACCESS_ONCE(*p);            \
    barrier();                                   \
    __v;                                         \
    })

#else
#error welp
#endif


/* Generic things */
typedef struct ring_buf_t {
    unsigned char buf[KBD_BUF_SIZE];
    unsigned front, back;
} ring_buf_t;

#define ring_inc(v) (((v) + 1) % KBD_BUF_SIZE)

/**** Something like my original ringbuf code from my 15-410 kernel **********/
/* Doesn't bother with any memory model stuff and so is actually wrong. */
int buf_enqueue_410(ring_buf_t *buf, unsigned char c)
{
    unsigned back = buf->back;
    unsigned front = buf->front;

    int enqueued = 0;
    if (ring_inc(back) != front) {
        buf->buf[back] = c;
        buf->back = ring_inc(back);
        enqueued = 1;
    }
    return enqueued;
}

int buf_dequeue_410(ring_buf_t *buf)
{
    unsigned front = buf->front;
    unsigned back = buf->back;

    int c = -1;
    if (front != back) {
        c = buf->buf[front];
        buf->front = ring_inc(front);
    }
    return c;
}

/******** Something that is at least correct wrt the compiler  **********/
int buf_enqueue_compiler_safe(ring_buf_t *buf, unsigned char c)
{
    unsigned back = buf->back;
    unsigned front = ACCESS_ONCE(buf->front);

    int enqueued = 0;
    if (ring_inc(back) != front) {
        ACCESS_ONCE(buf->buf[back]) = c;
        ACCESS_ONCE(buf->back) = ring_inc(back);
        enqueued = 1;
    }
    return enqueued;
}

int buf_dequeue_compiler_safe(ring_buf_t *buf)
{
    unsigned front = buf->front;
    unsigned back = ACCESS_ONCE(buf->back);

    int c = -1;
    if (front != back) {
        c = ACCESS_ONCE(buf->buf[front]);
        ACCESS_ONCE(buf->front) = ring_inc(front);
    }
    return c;
}

/*************************** Linux style ************************************/
/* Does the linux style one work *without* spinlocks under the assumption
 * that the same CPUs are calling enqueue and dequeue?? */

/* Some of the linux documentation here seems nonsensical to me. */

/*
 * I did some thinking about what could go wrong without spinlocks, and
 * convinced myself that the following simplified example was relevant:
 *
 * c = ACCESS_ONCE(clear);      | d = data;
 * if (c) { data = 1; }         | smp_store_release(&clear, 1);
 *
 * We want to know whether it is possible for d == 1 at the end.
 * This models a consumer thread reading data and then indicating
 * that the buffer is clear for more writing. The writing thread
 * checks that the buffer is clear and then writes.
 * This is possible in the C++ memory model (according to CppMem),
 * but seems pretty damn weird. It can't happen on POWER.
 * Maybe on Alpha? It seems generally pretty dubious.
 *
 * The "linux memory model" says that control dependencies can order
 * prior loads against later stores.
 *
 * I think this is sort of equivalent to the queue without spinlocks except
 * that the write would need to get speculated before *two* conditions...
 * That seems super weird.
 * Also, it's not totally clear to me that the RELEASE/ACQUIRE semantics
 * of dropping and taking a lock would save you by spec?
 * (By what spec??)
 * Also there are other barriers happening, so...
 *
 *
 * So also, linux doesn't actually seem to *use* smp_store_release and friends.
 * Cute.
 * See: https://github.com/torvalds/linux/commit/47933ad41a86a4a9b50bed7c9b9bd2ba242aac63
 *
 * The linux documentation call produce_item() and consume_item() functions
 * to do the actually accesses. I made them ACCESS_ONCE; I'm not totally sure
 * if that is necessary.
 *
 */

#define spin_lock(x) (void)0
#define spin_unlock(x) (void)0

int buf_enqueue_linux(ring_buf_t *buf, unsigned char c)
{
    spin_lock(&enqueue_lock);

    unsigned back = buf->back;
    /* Linux claims:
     * "The spin_unlock() and next spin_lock() provide needed ordering." */
    unsigned front = ACCESS_ONCE(buf->front);

    int enqueued = 0;
    if (ring_inc(back) != front) {
        ACCESS_ONCE(buf->buf[back]) = c;
        smp_store_release(&buf->back, ring_inc(back));
        enqueued = 1;
    }

    spin_unlock(&enqueue_lock);

    return enqueued;
}

int buf_dequeue_linux(ring_buf_t *buf)
{
    spin_lock(&dequeue_lock);

    unsigned front = buf->front;
    unsigned back = smp_load_acquire(&buf->back);

    int c = -1;
    if (front != back) {
        c = ACCESS_ONCE(buf->buf[front]);
        smp_store_release(&buf->front, ring_inc(front));
    }

    spin_unlock(&dequeue_lock);
    return c;
}

/********************* Linux/C++ style ************************************/
/* This is what I feel the linux thing should be?? */
int buf_enqueue_linux_mine(ring_buf_t *buf, unsigned char c)
{
    unsigned back = buf->back;
    unsigned front = smp_load_acquire(&buf->front);

    int enqueued = 0;
    if (ring_inc(back) != front) {
        ACCESS_ONCE(buf->buf[back]) = c;
        smp_store_release(&buf->back, ring_inc(back));
        enqueued = 1;
    }

    return enqueued;
}

int buf_dequeue_linux_mine(ring_buf_t *buf)
{
    unsigned front = buf->front;
    unsigned back = smp_load_acquire(&buf->back);

    int c = -1;
    if (front != back) {
        c = ACCESS_ONCE(buf->buf[front]);
        smp_store_release(&buf->front, ring_inc(front));
    }

    return c;
}

/******* This is what I could do if I was a "thoroughly bad man" *********/
/* Use consume without having an honest data dependency. */

/* Given values v and bs, produce a copy of v that is technically
 * data dependent on bs.
 * v can be a pointer or an integer, bs should be an integer.
 * More might be done to keep this from actually being optimized out!
 */
#define BULLSHIT_DEP(v, bs) ((v)+(bs^bs))

int buf_enqueue_linux_badman(ring_buf_t *buf, unsigned char c)
{
    unsigned back = buf->back;
    unsigned front = smp_load_consume(&buf->front);

    int enqueued = 0;
    if (ring_inc(back) != front) {
        ACCESS_ONCE(buf->buf[BULLSHIT_DEP(back, front)]) = c;
        smp_store_release(&buf->back, ring_inc(back));
        enqueued = 1;
    }

    return enqueued;
}

int buf_dequeue_linux_badman(ring_buf_t *buf)
{
    unsigned front = buf->front;
    unsigned back = smp_load_consume(&buf->back);

    int c = -1;
    if (front != back) {
        c = ACCESS_ONCE(buf->buf[BULLSHIT_DEP(front, back)]);
        smp_store_release(&buf->front, ring_inc(front));
    }

    return c;
}


/******************* This is the old linux version using barriers *************/
/* I dropped the locks but maybe they are needed? I haven't figured this out. */
int buf_enqueue_linux_old(ring_buf_t *buf, unsigned char c)
{
    unsigned back = buf->back;
    unsigned front = ACCESS_ONCE(buf->front);

    int enqueued = 0;
    if (ring_inc(back) != front) {
        ACCESS_ONCE(buf->buf[back]) = c;

        smp_wmb(); /* commit the item before incrementing the head */

        ACCESS_ONCE(buf->back) = ring_inc(back);
        enqueued = 1;
    }

    return enqueued;
}

int buf_dequeue_linux_old(ring_buf_t *buf)
{
    unsigned front = buf->front;
    unsigned back = ACCESS_ONCE(buf->back);

    int c = -1;
    if (front != back) {
        /* read index before reading contents at that index */
        smp_rmb();

        c = ACCESS_ONCE(buf->buf[front]);

        smp_mb(); /* finish reading descriptor before incrementing tail */
        ACCESS_ONCE(buf->front) = ring_inc(front);
    }

    return c;
}


/******************* try an rmc version..... ****************************/
#ifdef HAS_RMC
#error "no you don't"
#else

/* Dummy things to let us write the code */
#define XEDGE(x, y) do { } while (0)
#define VEDGE(x, y) do { } while (0)
#define L(label, stmt) stmt

#endif



/* The important things are:
 *
 * * We never read data that hasn't actually been written.
 * insert -vo-> e_update -vo/rf-> check -xo-> read
 * So insert -vt-> read, which means we read the right value.
 *
 * It is important that edges point into subsequent function calls
 * also: any buffer insert is visibility ordered before all subsequent
 * updates of back.
 *
 * * We never trash data:
 * The trashing data example gets kind of tricky, since
 * Imagine that we have a full buffer: back == 0, front == 1
 *
 * We annotate things with what the are reading or writing when
 * it is relevant.
 *
 * dequeue does:
 *   d_check0 -xo-> read0(R buf[1]) -xo-> d_update0(W front = 2)
 *
 *   d_check1 -xo-> read1(R buf[2]) -xo-> d_update1(W front = 3)
 *
 * enqueue does:
 *   e_check0(R front == 2) -xo-> insert0(W buf[0]) -vo-> e_update0
 *        \------------xo--------------v
 *   e_check1(R front == 3) -xo-> insert1(W buf[1]) -vo-> e_update1
 *
 * There are cross function call edges, but we only label the
 * e_check0 -xo-> insert1 edge, since it is the only relevant here.
 *
 * We have d_update0 -rf-> e_check0 and d_update1 -rf-> e_check1.
 * We want to make sure that the value dequeue is dequeueing doesn't
 * get stomped, so we want to *rule out* insert1 -rf-> read0.
 *
 * Combining xo and rf edges, we have the following sequence in trace order:
 * read0 -> d_update0 -> e_check0 -> insert1.
 * Having insert1 -rf-> read0 would cause a cycle in trace order.
 *
 */

int buf_enqueue_rmc(ring_buf_t *buf, unsigned char c)
{
    XEDGE(check, insert);
    VEDGE(insert, update);

    unsigned back = buf->back;
    L(check, unsigned front = buf->front);

    int enqueued = 0;
    if (ring_inc(back) != front) {
        L(insert, buf->buf[back] = c);
        L(update, buf->back = ring_inc(back));
        enqueued = 1;
    }
    return enqueued;
}

int buf_dequeue_rmc(ring_buf_t *buf)
{
    XEDGE(check, read);
    XEDGE(read, update);

    unsigned front = buf->front;
    L(check, unsigned back = buf->back);

    int c = -1;
    if (front != back) {
        L(read, c = buf->buf[front]);
        L(update, buf->front = ring_inc(front));
    }
    return c;
}
