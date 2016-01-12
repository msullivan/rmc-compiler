// Parallel Sieve of Eratsothenes test, inspired by the example in
// "Threads Cannot be Implemented as a Library".

// The main thing that I wanted to test here was how much gain there
// is from adding SC atomics to RMC. Answer: a lot,
// probably. MFENCEing on ever get() is like a 3x slowdown, and that
// is what SC would require without adding it to RMC.

#include <atomic>
#include <thread>
#include <iostream>
#include <vector>

#ifndef MODE
#define MODE UnsafeByte
#endif

const int kSqrt = 40000;
const int kMax = kSqrt*kSqrt;

// We allocate one big array and cast as necessary. Allocating an array
// for each possible scheme lead to linker errors.
uint8_t bigArray[kMax];
std::atomic<bool>* atomicArray =
    reinterpret_cast<std::atomic<bool> *>(bigArray);
std::atomic<uint8_t>* atomicIArray =
    reinterpret_cast<std::atomic<uint8_t> *>(bigArray);

//////////////////////////////////////////////// Byte oriented ones

////// SC C++11 atomics
// Looks like this is about a 50% regression against relaxed
struct SCByte {
    static std::atomic<bool>* bits;
    static void set(int idx) { bits[idx] = true; }
    static bool get(int idx) { return bits[idx]; }
};
std::atomic<bool>* SCByte::bits = atomicArray;

////// SC atomics if we had to MFENCE after loads also
// Ouch! This is like a 3x regression against the SC one!
struct SCBadByte {
    static std::atomic<bool>* bits;
    static void set(int idx) { bits[idx] = true; }
    static bool get(int idx) {
        // Both fence locations cause huge grief.
        //std::atomic_thread_fence(std::memory_order_seq_cst);
        bool r = bits[idx];
        std::atomic_thread_fence(std::memory_order_seq_cst);
        return r;
    }
};
std::atomic<bool>* SCBadByte::bits = atomicArray;

// What if we use xadd to do loads?
// Ok, even worse. Makes sense.
struct SCXaddByte {
    static std::atomic<uint8_t>* bits;
    static void set(int idx) { bits[idx] = true; }
    static bool get(int idx) { return bits[idx].fetch_add(0); }
};
std::atomic<uint8_t>* SCXaddByte::bits = atomicIArray;


////// Relaxed C++11 atomics
// Should perform about as well as UnsafeByte
struct RelaxedByte {
    static std::atomic<bool>* bits;
    static void set(int idx) {
        bits[idx].store(true, std::memory_order_relaxed);
    }
    static bool get(int idx) {
        return bits[idx].load(std::memory_order_relaxed);
    }
};
std::atomic<bool>* RelaxedByte::bits = atomicArray;

////// Unsafe accesses
struct UnsafeByte {
    static bool* bits;
    static void set(int idx) { bits[idx] = true; }
    static bool get(int idx) { return bits[idx]; }
};
bool* UnsafeByte::bits = reinterpret_cast<bool*>(bigArray);


//////////////////////////////////////////////// Bit oriented ones

const int kBits = 8;

int byte(int idx) { return idx/kBits; }
int mask(int idx) { return 1 << (idx % kBits); }
////

////// SC
struct SCBit {
    static std::atomic<uint8_t>* bits;
    static void set(int idx) { bits[byte(idx)] |= mask(idx); }
    static bool get(int idx) { return !!(bits[byte(idx)] & mask(idx)); }
};
std::atomic<uint8_t>* SCBit::bits = atomicIArray;

// This would be about the same perf as the SC version on x86 since
// the write still needs to be locked.
struct RelaxedBit {
    static std::atomic<uint8_t>* bits;
    static void set(int idx) {
        bits[byte(idx)].fetch_or(mask(idx), std::memory_order_relaxed);
    }
    static bool get(int idx) {
        return !!(bits[byte(idx)].load(std::memory_order_relaxed) & mask(idx));
    }
};
std::atomic<uint8_t>* RelaxedBit::bits = atomicIArray;


////// Unsafe accesses
struct UnsafeBit {
    static uint8_t* bits;
    static void set(int idx) { bits[byte(idx)] |= mask(idx); }
    static bool get(int idx) { return !!(bits[byte(idx)] & mask(idx)); }
};
uint8_t* UnsafeBit::bits = bigArray;


// The actual sieve, parameterized against an Array that tracks bits
template<class Array>
void sieve(int start) {
    for (int my_prime = start; my_prime < kSqrt; my_prime++) {
        if (!Array::get(my_prime)) {
            for (int multiple = my_prime; multiple < kMax;
                 multiple += my_prime) {
                if (!Array::get(multiple)) Array::set(multiple);
            }
        }
    }
}

template<class Array>
void test(int nthreads) {
    std::vector<std::thread> threads;

    // XXX: we should probably synchronize thread starting work and
    // just time the actual work.
    for (int i = 0; i < nthreads; i++) {
        threads.push_back(std::thread(sieve<Array>, 2));
    }

    for (auto & thread : threads) {
        thread.join();
    }

    std::cout << "86028157 should be prime: " << Array::get(86028157) << "\n";
    std::cout << "99999997 should be composite: " <<
        Array::get(99999997) << "\n";
}

int main(int argc, char** argv) {
    int nthreads = argc > 1 ? atoi(argv[1]) : 1;
    test<MODE>(nthreads);

    return 0;
}
