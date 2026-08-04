#ifndef COMMON_H
#define COMMON_H

#include "config.h"
#include <stdint.h>

typedef char hex64_t[HASH_HEX_LEN + 1];
typedef char hex16_t[HEX16_LEN + 1];
typedef enum { ROLE_PARENT, ROLE_NODE, ROLE_MINER, ROLE_CLIENT } role_t;
typedef enum { MSG_TX, MSG_BLOCK, MSG_BLOCK_REQUEST, MSG_CHAIN_REQUEST, MSG_REPLY } msg_type_t;
typedef struct {
    uint32_t type;
    uint32_t sender_role;
    uint32_t sender_idx;
    uint32_t seq;
    uint32_t last;
    uint32_t payload_len;
    char payload[MSG_PAYLOAD_MAX];
} msg_t;

#endif