#ifndef BLOCK_H
#define BLOCK_H

#include "common.h"
#include "config.h"
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint64_t index;
    uint64_t timestamp;
    uint64_t nonce;
    hex64_t prev_hash;
    hex64_t merkle_root;
    char txs[MAX_TX_PER_BLOCK][MAX_TX_LEN];
    size_t tx_count;
} block_t;

#endif