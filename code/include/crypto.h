#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h> // for uint64_t
#include <stddef.h> // for size_t

//computes the SHA-256 hash of any data
int sha256_hex(const void *data, size_t len, char out[65]);

//turns a number into a hex string
void u64_to_hex16(uint64_t value, char out[17]);

//from a string to a number
int hex16_to_u64(const char *in, uint64_t *out);

//computes the Merkle root of a block of transactions
int merkle_root(const char *const *txs, size_t n, char out[65]);

#endif
