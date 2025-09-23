#ifndef LOCK_H
#define LOCK_H

#include <stdatomic.h>

static void rlock(atomic_int_fast64_t *lock) {
    int_fast64_t expected = 0;
    while (!atomic_compare_exchange_weak(lock, &expected, 1)) {
        expected = 0;  // 重置 expected
    }
}

static void runlock(atomic_int_fast64_t *lock) {
    atomic_store(lock, 0);
}

static void lock(atomic_int_fast64_t *lock) {
    int_fast64_t expected = 0;
    while (!atomic_compare_exchange_weak(lock, &expected, 2)) {
        expected = 0;
    }
}

static void unlock(atomic_int_fast64_t *lock) {
    atomic_store(lock, 0);
}
#endif // LOCK_H
