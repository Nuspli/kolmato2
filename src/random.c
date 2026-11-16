#include "random.h"

unsigned int random_num = 1415926535;

unsigned int rand_int() {
    // pseudo random unsigned integer
	unsigned int x = random_num;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	random_num = x;
	return x;
}

u64 rand64() {
    u64 u1, u2, u3, u4;
    
    u1 = (u64)(rand_int()) & 0xFFFF;
    u2 = (u64)(rand_int()) & 0xFFFF;
    u3 = (u64)(rand_int()) & 0xFFFF;
    u4 = (u64)(rand_int()) & 0xFFFF;
    
    return u1 | (u2 << 16) | (u3 << 32) | (u4 << 48);
}

u64 rand64_few_bits() {
    return rand64() & rand64() & rand64();
}