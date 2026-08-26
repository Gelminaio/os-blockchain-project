#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "config.h"
#include "errors.h"
#include "block.h"
#include "crypto.h"
#include "transaction.h"
#include "logging.h"

static int join_txs(const block_t *b, char *out, size_t cap) {
    //if there's no space
    if(cap == 0) {
        return -1;
    }

    //chain without transactions case
    if (b->tx_count == 0) {
        out[0] = '\0';
        return 0;
    }

    int written = snprintf(out, cap, "%s", b->txs[0]); //written are bytes that should be written
    if (written < 0 || (size_t)written >= cap) {
        return -1;
    }
    size_t offset = (size_t) written; //size_t, non-negative integer

    for(size_t i = 1; i < b->tx_count; i++) {
        written = snprintf(out + offset, cap - offset, "::%s", b->txs[i]);
        if (written < 0 || (size_t)written >= (cap - offset)) {
            return -1; 
        }
        offset += (size_t) written;
    }
    return (int)offset;
}

static int is_hex_lowercase(const char *str, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return 0; //invalid character found
        }
    }
    return 1;
}

static int block_is_valid(const block_t *b, int is_genesis) {
    if (b->tx_count < 1 || b->tx_count > MAX_TX_PER_BLOCK) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Number of transactions (%zu) out of block range %lu", b->tx_count, (unsigned long)b->index);
        log_msg(LOG_WARNING, msg);
        return INVALID_BLOCK;
    }

    //check of every transaction (skipped if genesis block)
    if (!is_genesis) {
        for (size_t i = 0; i < b->tx_count; i++) {
            if (transaction_is_valid(b->txs[i]) != OK) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Transaction %zu not correctly formatted %lu", i, (unsigned long)b->index);
                log_msg(LOG_WARNING, msg);
                return INVALID_BLOCK;
            }
        }
    }

    //merkle root check
    const char *tx_ptrs[MAX_TX_PER_BLOCK];
    for (size_t i = 0; i < b->tx_count; i++) {
        tx_ptrs[i] = b->txs[i];
    }

    char calc_merkle[65];
    if (merkle_root(tx_ptrs, b->tx_count, calc_merkle) != OK) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Error during merkle root calculation (block %lu)", (unsigned long)b->index);
        log_msg(LOG_WARNING, msg);
        return INVALID_BLOCK;
    }

    if (strcmp(calc_merkle, b->merkle_root) != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Merkle root mismatch (block %lu). Calculated: %s, Declared: %s", (unsigned long)b->index, calc_merkle, b->merkle_root);
        log_msg(LOG_WARNING, msg);
        return INVALID_BLOCK;
    }

    return OK;
}

static int chain_push(chain_t *c, const block_t *b) {
    if (c->len >= c->cap) {
        size_t new_cap;
        if(c->cap==0) {
            new_cap = 1;
        }
        else {
            new_cap = c->cap * 2;
        }
        block_t *new_v = (block_t *)realloc(c->v, new_cap * sizeof(block_t)); //realloc syntax: int *ptr2 = realloc(ptr1, size);
        if (new_v == NULL) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Realloc failed");
            log_msg(LOG_ERROR, msg);
            return SYS_ERROR;
        }
        c->v = new_v;
        c->cap = new_cap;
    }
    c->v[c->len] = *b;
    c->len++;
    return OK;
}

int block_serialize(const block_t *b, char *out, size_t cap) {
    char transactions[MAX_SERIAL_LEN];
    int tx_written = join_txs(b, transactions, sizeof(transactions));
    if (tx_written < 0) {
        return SYS_ERROR; //if join_tx returns -1
    }

    hex16_t index_hex, timestamp_hex, nonce_hex;
    u64_to_hex16(b->index, index_hex);
    u64_to_hex16(b->timestamp, timestamp_hex);
    u64_to_hex16(b->nonce, nonce_hex);

    int written = snprintf(out, cap, "%s%s%s%s%s%s", index_hex, timestamp_hex, b->prev_hash, b->merkle_root, nonce_hex, transactions);
    
    if (written < 0 || (size_t)written >= cap) {
        return SYS_ERROR;
    }
    return OK;
}

int block_hash(const block_t *b, char out[65]) {
    char sb[MAX_SERIAL_LEN];
    int result = block_serialize(b, sb, sizeof(sb));
    if(result != OK) {
        return result;
    }
    return sha256_hex(sb, strlen(sb), out); //method from crypto.c
}

int block_to_csv_row(const block_t *b, char *out, size_t cap) {
    char transactions[MAX_SERIAL_LEN];
    int tx_written = join_txs(b, transactions, sizeof(transactions));
    if (tx_written < 0) {
        return SYS_ERROR; //if join_tx returns -1
    }

    hex16_t index_hex, timestamp_hex, nonce_hex;
    u64_to_hex16(b->index, index_hex);
    u64_to_hex16(b->timestamp, timestamp_hex);
    u64_to_hex16(b->nonce, nonce_hex);

    int written = snprintf(out, cap, "%s,%s,%s,%s,%s,%s", index_hex, timestamp_hex, b->prev_hash, b->merkle_root, nonce_hex, transactions);
    
    if (written < 0 || (size_t)written >= cap) {
        return SYS_ERROR;
    }
    return OK;
}

int block_from_csv_row(const char *row, block_t *b) {
    memset(b, 0, sizeof *b);

    if(strlen(row) < 181 || row[16] != ',' || row[33] != ',' || row[98] != ',' || row[163] != ',' || row[180] != ',') {
        return CSV_PARSE_ERROR;
    }

    char buf16[17];

    //index (from 0 to 15)
    memcpy(buf16, row + 0, 16);
    buf16[16] = '\0';
    if (hex16_to_u64(buf16, &(b->index)) != OK) {
        return INVALID_BLOCK;
    }

    //timestamp (from 17 to 32)
    memcpy(buf16, row + 17, 16);
    buf16[16] = '\0';
    if (hex16_to_u64(buf16, &(b->timestamp)) != OK) {
        return INVALID_BLOCK;
    }

    //prev_hash (from 34 to 97)
    memcpy(b->prev_hash, row + 34, 64);
    b->prev_hash[64] = '\0';
    if (!is_hex_lowercase(b->prev_hash, 64)) {
        return INVALID_BLOCK;
    }

    //merkle_root (from 99 to 162)
    memcpy(b->merkle_root, row + 99, 64);
    b->merkle_root[64] = '\0';
    if (!is_hex_lowercase(b->merkle_root, 64)) {
        return INVALID_BLOCK;
    }

    //nonce (from 164 to 179)
    memcpy(buf16, row + 164, 16);
    buf16[16] = '\0';
    if (hex16_to_u64(buf16, &(b->nonce)) != OK) {
        return INVALID_BLOCK;
    }

    //transactions (from 181 to ...)
    b->tx_count = 0;
    const char *current_tx = row + 181;
    if (*current_tx == '\0') {
        return INVALID_BLOCK; //no transactions case
    }

    while(1) {
        //searching for the next "::"
        const char *next_separator = strstr(current_tx, "::");
        
        size_t tx_len; //transaction length = difference between next_separator and current_tx
        if (next_separator != NULL) {
            tx_len = (size_t)(next_separator - current_tx);
        } else {
            tx_len = strlen(current_tx); //last transaction case
        }

        //too many transactions check (invalid block)
        if (b->tx_count >= MAX_TX_PER_BLOCK) {
            return INVALID_BLOCK;
        }

        //string is too long
        if (tx_len >= MAX_TX_LEN) {
            return INVALID_BLOCK; 
        }

        memcpy(b->txs[b->tx_count], current_tx, tx_len);
        b->txs[b->tx_count][tx_len] = '\0';
        b->tx_count++;

        //check if the current one was the last transaction
        if (next_separator == NULL) {
            break;
        }
        //otherwise jump the two "::"
        current_tx = next_separator + 2;
    }
    return OK;
}

void chain_init(chain_t *c) {
    c->v = NULL;
    c->len = 0;
    c->cap=0;
}

void chain_free(chain_t *c) {
    if(c->v != NULL) {
        free(c->v);
    }
    chain_init(c); //resets the chain
}

const block_t *chain_tip(const chain_t *c) {
    if(c==NULL || c->len==0) {
        return NULL;
    }
    return &(c->v[c->len - 1]);
}

const block_t *chain_find_index(const chain_t *c, uint64_t i) {
    if(c==NULL || c->len<=i || c->v[i].index != i) {
        return NULL;
    }
    return &(c->v[i]);
}

const block_t *chain_find_hash(const chain_t *c, const char *h) {
    if(c==NULL || c->len==0 || h==NULL) {
        return NULL;
    }

    char current_hash[65];

    for(size_t i = 0; i < c->len; i++) {
        if(block_hash(&(c->v[i]), current_hash) == OK) {
            if(strcmp(current_hash, h) == 0) {
                return &(c->v[i]); //found, return the pointer to the block
            }
        }
    }
    return NULL;
}

append_result_t chain_append(chain_t *c, const block_t *b) {
    if (c == NULL || b == NULL) {
        return APPEND_INVALID;
    }

    //structural validation
    int is_genesis = (b->index == 0);
    if (block_is_valid(b, is_genesis) != OK) {
        return APPEND_INVALID;
    }

    char b_hash[65];
    if (block_hash(b, b_hash) != OK) {
        return APPEND_INVALID;
    }
    

    //CASE 1: duplicate block
    if (chain_find_hash(c, b_hash) != NULL) {
        return APPEND_DUP;
    }

    const block_t *tip = chain_tip(c);

    //CASE 2: empty chain
    if (tip == NULL) {
        if (is_genesis) {
            if (chain_push(c, b) != OK) return APPEND_INVALID; //realloc failed
            return APPEND_OK;
        } else {
            return APPEND_AHEAD; //non-genesis blocks rejected on empty chain
        }
    }

    //non-empty chain
    //chain tip's hash
    char tip_hash[65];
    if (block_hash(tip, tip_hash) != OK) {
        return APPEND_INVALID;
    }

    //CASE 3: normal situation
    if ((b->index == tip->index + 1) && (strcmp(b->prev_hash, tip_hash) == 0)) {
        if (chain_push(c, b) != OK) return APPEND_INVALID;
        return APPEND_OK;
    }

    //CASE 4: too far ahead
    if (b->index > tip->index + 1) {
        return APPEND_AHEAD;
    }

    //CASE 5: tie-breaker (same index and same father)
    if (b->index == tip->index && strcmp(b->prev_hash, tip->prev_hash) == 0) { 
        //alphabetical tie-breaker between the two hashes
        if (strcmp(b_hash, tip_hash) < 0) {
            //new block wins, replacement
            c->v[c->len - 1] = *b;
            return APPEND_REPLACED;
        } else {
            //new block lose
            char msg[256];
            snprintf(msg, sizeof(msg), "Concurrent block rejected. '%s' lost against the tip '%s'", b_hash, tip_hash);
            log_msg(LOG_WARNING, msg);
            return APPEND_INVALID;
        }
    }

    // (lower index, fork on older blocks, different fathers)
    return APPEND_STALE;
}

int csv_load(const char *path, chain_t *out) {
    //chain MUST arrive initialized and empty from main.c
    char msg[256]; //buffer for logs

    FILE *fp = fopen(path, "r");
    if(fp==NULL) {
        snprintf(msg, sizeof(msg), "Cannot open CSV file %s for reading: %s", path, strerror(errno));
        log_msg(LOG_ERROR, msg);
        return FILE_ERROR; //file doesn't exist
    }

    char buf[MAX_CSV_ROW_LEN];

    //empty file case
    if(fgets(buf, sizeof(buf), fp) == NULL) {
        snprintf(msg, sizeof(msg), "CSV file %s exists but is empty.", path);
        log_msg(LOG_ERROR, msg);
        fclose(fp);
        return CSV_PARSE_ERROR;
    }

    //replace \n with \0
    buf[strcspn(buf, "\n")] = '\0';

    //wrong header case
    if(strcmp(buf, CSV_HEADER) != 0) {
        snprintf(msg, sizeof(msg), "Non-valid CSV header or wrong.");
        log_msg(LOG_ERROR, msg);
        fclose(fp);
        return CSV_PARSE_ERROR;
    }

    int line_number = 2; //line 1 is the header

    while(fgets(buf, sizeof(buf), fp) != NULL) {
        size_t len = strlen(buf);

        if(len > 0 && buf[len - 1] != '\n' && !feof(fp)) {
            snprintf(msg, sizeof(msg), "Line %d exceeds the buffer limit", line_number);
            log_msg(LOG_ERROR, msg);
            fclose(fp);
            return CSV_PARSE_ERROR;
        }

        buf[strcspn(buf, "\n")] = '\0';

        block_t b;
        if(block_from_csv_row(buf, &b) != OK) {
            snprintf(msg, sizeof(msg), "Line %d isn't correctly formatted", line_number);
            log_msg(LOG_ERROR, msg);
            fclose(fp);
            return CSV_PARSE_ERROR;
        }

        append_result_t res = chain_append(out, &b);


        switch (res) {
            case APPEND_OK:
                break;
            case APPEND_INVALID:
                //invalid block
                snprintf(msg, sizeof(msg), "The block at line %d is structurally invalid.", line_number);
                log_msg(LOG_ERROR, msg);
                fclose(fp);
                return INVALID_BLOCK;
                
            default:
                //all the other cases: APPEND_AHEAD, APPEND_STALE, APPEND_DUP, APPEND_REPLACED
                snprintf(msg, sizeof(msg), "Chain mismatch (code %d) at line %d.", res, line_number);
                log_msg(LOG_ERROR, msg);
                fclose(fp);
                return CHAIN_MISMATCH;
        }
        line_number++;
    }
    fclose(fp);

    snprintf(msg, sizeof(msg), "Loaded %zu blocks from %s", out->len, path);
    log_msg(LOG_INFO, msg);
    return OK;
}
int csv_save(const char *path, const chain_t *c) {
    if(c==NULL) {
        return FILE_ERROR;
    }

    char tmp_path[512];

    //atomic writing: firstly the file is named with .tmp, then populated and lastly renamed without .tmp
    int path_len = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    if (path_len < 0 || (size_t)path_len >= sizeof(tmp_path)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "File path too long (%s)", path);
        log_msg(LOG_ERROR, msg);
        return FILE_ERROR;
    }


    FILE *fp = fopen(tmp_path, "w");
    if(fp==NULL) {
        char msg[1024];
        snprintf(msg, sizeof(msg), "Cannot open temporary file %s for writing: %s", tmp_path, strerror(errno));
        log_msg(LOG_ERROR, msg);
        return FILE_ERROR;
    }

    //csv header
    if(fprintf(fp, "%s\n", CSV_HEADER) < 0) {
        fclose(fp);
        remove(tmp_path);
        return FILE_ERROR;
    }

    //write every block, one per line
    char row_buf[MAX_CSV_ROW_LEN];
    for(size_t i = 0; i < c->len; i++) {
        if(block_to_csv_row(&(c->v[i]), row_buf, sizeof(row_buf)) != OK) {
            fclose(fp);
            remove(tmp_path);
            return FILE_ERROR;
        }
        if(fprintf(fp, "%s\n", row_buf) < 0) {
            fclose(fp);
            remove(tmp_path);
            return FILE_ERROR;
        }
    }

    //if the disk is full
    if(fclose(fp) != 0) {
        remove(tmp_path);
        return FILE_ERROR;
    }

    //atomic writing: removes the final .tmp
    if(rename(tmp_path, path) != 0) {
        char msg[1024];
        snprintf(msg, sizeof(msg), "Cannot rename %s to %s: %s", tmp_path, path, strerror(errno));
        log_msg(LOG_ERROR, msg);
        remove(tmp_path);
        return FILE_ERROR;
    }

    char msg[256];
    snprintf(msg, sizeof(msg), "Saved %zu blocks to %s", c->len, path);
    log_msg(LOG_INFO, msg);
    return OK;
}