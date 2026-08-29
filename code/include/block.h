#ifndef BLOCK_H
#define BLOCK_H

#include "common.h"
#include "config.h"
#include <stdint.h>
#include <stddef.h>

//difference between uint64_t and size_t: uint64_t is always 64 bits, size_t can be 32 or 64 bits, it depends on the CPU
typedef struct {
    uint64_t index;
    uint64_t timestamp;
    uint64_t nonce;
    hex64_t prev_hash;
    hex64_t merkle_root;
    char txs[MAX_TX_PER_BLOCK][MAX_TX_LEN];
    size_t tx_count;
} block_t;

typedef struct {
    block_t *v;
    size_t len;
    size_t cap;
} chain_t;

//APPEND_LOST is a good block that lost a fork, APPEND_INVALID is a broken one: they must stay separate
typedef enum { APPEND_OK, APPEND_REPLACED, APPEND_DUP, APPEND_STALE, APPEND_AHEAD, APPEND_LOST, APPEND_INVALID } append_result_t;

void chain_init(chain_t *c);
void chain_free(chain_t *c);
int block_serialize(const block_t *b, char *out, size_t cap);
int block_hash(const block_t *b, char out[65]);
int block_to_csv_row(const block_t *b, char *out, size_t cap);
int block_from_csv_row(const char *row, block_t *b);
append_result_t chain_append(chain_t *c, const block_t *b);
const block_t *chain_tip(const chain_t *c);
const block_t *chain_find_index(const chain_t *c, uint64_t i);
const block_t *chain_find_hash(const chain_t *c, const char *h);
int csv_load(const char *path, chain_t *out);
int csv_save(const char *path, const chain_t *c);

#endif