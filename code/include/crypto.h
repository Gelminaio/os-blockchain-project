#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h> // serve per usare uint64_t
#include <stddef.h> // serve per usare size_t

/*calcola l'hash SHA-256 di qualsiasi dato*/
int sha256_hex(const void *data, size_t len, char out[65]);

/*Trasforma un numero in una stringa esadecimale*/
void u64_to_hex16(uint64_t value, char *out[17]);

/*Da una stringa ad un numero*/
int hex16_to_u64(const char *in, uint64_t *out);

/*calcola la radice di Merkle di un blocco di transizioni*/
int merkle_root(const char *const *txs, size_t n, char out[65]);

//.

#endif