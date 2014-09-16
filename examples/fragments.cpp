#include <atomic>

extern "C" {
#if 0
}
#endif

int load_acquire(std::atomic<int> *x) {
	return x->load(std::memory_order_acquire);
}

void load_acquire_loop(std::atomic<int> *x) {
	while (x->load(std::memory_order_acquire) == 0);
}

int load_consume(std::atomic<int> *x) {
	return x->load(std::memory_order_consume);
}

void store_release(std::atomic<int> *x) {
	x->store(1, std::memory_order_release);
}

int load_sc(std::atomic<int> *x) {
	return x->load(std::memory_order_seq_cst);
}
void store_sc(std::atomic<int> *x) {
	x->store(1, std::memory_order_seq_cst);
}

int double_sc(std::atomic<int> *x, std::atomic<int> *y) {
	int z = x->load(std::memory_order_seq_cst);
	y->store(1, std::memory_order_seq_cst);
	return z;
}


int main() { return 0; }
}
