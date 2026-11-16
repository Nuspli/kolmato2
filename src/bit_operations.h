#ifndef BIT_OPERATIONS_H
#define BIT_OPERATIONS_H

#include <stdint.h>
#include <stdbool.h>

#define bit_count(num) __builtin_popcountll(num)
#define lsb(num) __builtin_ctzll(num)
#define set_bit(num, index) ((num) |= (1ULL << (index)))
#define flip_bit(num, index) ((num) ^= (1ULL << (index)))
#define check_bit(num, index) ((num) & (1ULL << (index)))
#define bit(index) (1ULL << (index))

#endif