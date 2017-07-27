#include <rmc++.h>
#include <atomic>

#define BUF_SIZE (1024*1)

// RMC version
struct ring_buf {
    unsigned char buf[BUF_SIZE];
    rmc::atomic<unsigned> front, back;
};

int buf_enqueue(ring_buf *buf, unsigned char c)
{
    XEDGE(e_check, insert);
    VEDGE(insert, e_update);

    unsigned back = buf->back;
    unsigned front = L(e_check, buf->front);

    int enqueued = 0;
    if (back - BUF_SIZE != front) {
        L(insert, buf->buf[back % BUF_SIZE] = c);
        L(e_update, buf->back = back + 1);
        enqueued = 1;
    }
    return enqueued;
}

int buf_dequeue(ring_buf *buf)
{
    XEDGE(d_check, read);
    XEDGE(read, d_update);

    unsigned front = buf->front;
    unsigned back = L(d_check, buf->back);

    int c = -1;
    if (front != back) {
        L(read, c = buf->buf[front % BUF_SIZE]);
        L(d_update, buf->front = front + 1);
    }

    return c;
}

// C++11 version
struct ring_buf_c11 {
    unsigned char buf[BUF_SIZE];
    std::atomic<unsigned> front, back;
};

int buf_enqueue(ring_buf_c11 *buf, unsigned char c)
{
    unsigned back = buf->back.load(std::memory_order_relaxed);
    unsigned front = buf->front.load(std::memory_order_acquire);

    int enqueued = 0;
    if (back - BUF_SIZE != front) {
        buf->buf[back % BUF_SIZE] = c;
        buf->back.store(back + 1, std::memory_order_release);
        enqueued = 1;
    }
    return enqueued;
}

int buf_dequeue(ring_buf_c11 *buf)
{
    unsigned front = buf->front.load(std::memory_order_relaxed);
    unsigned back = buf->back.load(std::memory_order_acquire);

    int c = -1;
    if (front != back) {
        c = buf->buf[front % BUF_SIZE];
        buf->front.store(front + 1, std::memory_order_release);
    }

    return c;
}
