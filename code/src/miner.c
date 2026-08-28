//miner process: collects transactions in a mempool and mines new blocks
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

#include "crypto.h"
#include "config.h"
#include "errors.h"
#include "common.h"
#include "ipc.h"
#include "block.h"
#include "logging.h"
#include "transaction.h"

#define MEMPOOL_SIZE 1024
static char mempool[MEMPOOL_SIZE][MAX_TX_LEN];
static int mempool_count = 0;

static void mempool_add(const char *txs) {

    if (mempool_count < MEMPOOL_SIZE) {
        strncpy(mempool[mempool_count], txs, MAX_TX_LEN - 1);
        mempool[mempool_count][MAX_TX_LEN - 1] = '\0';
        mempool_count ++;
    } else {
        log_msg(LOG_WARNING, "Mempool full, transaction discarded");
    }
}

static void mempool_remove(const char *tx) {
    for (int i = 0; i < mempool_count; i++) {
        if (strcmp(mempool[i], tx) == 0) {
            for (int j = i; j < mempool_count - 1; j++) {
                strcpy(mempool[j], mempool[j+1]);
            }
            mempool_count--;
            break;
        }

    }
}

//blocks we mined and already sent, waiting to find out how they ended up. Their
//transactions are out of the mempool, so a block that loses the tie breaker
//would take them down with it: the clients send every transaction to a single
//miner, nobody else has a copy and nobody else would ever mine it again.
#define PENDING_MAX 8

typedef struct {
    uint64_t index;
    char hash[65];
    char txs[MAX_TX_PER_BLOCK][MAX_TX_LEN];
    size_t tx_count;
} pending_t;

static pending_t pending[PENDING_MAX];
static int pending_count = 0;

static void pending_requeue(const pending_t *p) {
    for (size_t i = 0; i < p->tx_count; i++) {
        mempool_add(p->txs[i]);
    }
}

static void pending_drop(int i) {
    for (int j = i; j < pending_count - 1; j++) {
        pending[j] = pending[j + 1];
    }
    pending_count--;
}

static void pending_add(const block_t *b, const char *hash) {
    if (pending_count == PENDING_MAX) {
        //the oldest one never came back from the nodes: give its transactions
        //another chance instead of forgetting about them
        pending_requeue(&pending[0]);
        pending_drop(0);
    }

    pending_t *p = &pending[pending_count++];
    p->index = b->index;
    snprintf(p->hash, sizeof(p->hash), "%s", hash);
    p->tx_count = b->tx_count;
    for (size_t i = 0; i < b->tx_count; i++) {
        snprintf(p->txs[i], MAX_TX_LEN, "%s", b->txs[i]);
    }
}

//asks the chain what it actually kept at the index of each of our candidates.
//A block of ours that was beaten gives its transactions back to the mempool;
//one that is sitting on the tip stays pending, because a block with an even
//lower hash can still replace it and we would lose track of it
static void pending_settle(const chain_t *chain) {
    const block_t *tip = chain_tip(chain);
    char log_buf[256];
    char kept_hash[65];
    int i = 0;

    if (tip == NULL) {
        return;
    }

    while (i < pending_count) {
        const block_t *kept = chain_find_index(chain, pending[i].index);

        if (kept == NULL || block_hash(kept, kept_hash) != OK) {
            i++; //that index is not decided yet, keep waiting
            continue;
        }

        if (strcmp(kept_hash, pending[i].hash) == 0) {
            //ours is the one in the chain, but only a block further ahead makes
            //it final: while it is the tip a lower hash could still replace it
            if (tip->index > pending[i].index) {
                pending_drop(i);
            } else {
                i++;
            }
            continue;
        }

        //beaten, and the tie breaker is deterministic on the lowest hash, so it
        //can never win it back: the transactions go home
        snprintf(log_buf, sizeof(log_buf), "Our block %llu lost the tie breaker: %zu transactions back in the mempool",
                 (unsigned long long)pending[i].index, pending[i].tx_count);
        log_msg(LOG_INFO, log_buf);
        pending_requeue(&pending[i]);
        pending_drop(i);
    }
}

//drains everything pending in the inbox: transactions go into the mempool,
//blocks are appended to the chain. Called both between blocks and inside the
//attempts loop, so the fifo keeps being read while mining and the clients
//stop hitting EAGAIN. Returns 1 if the tip moved, meaning a candidate built
//on the old tip is now stale, 0 otherwise.
static int drain_inbox(int inbox, chain_t *chain) {
    msg_t msg;
    int tip_moved = 0;

    while (ipc_recv_nb(inbox, &msg) == OK) {
        if (msg.type == MSG_TX) {
            if (transaction_is_valid(msg.payload) == OK) {
                mempool_add(msg.payload);
            }
        } else if (msg.type == MSG_BLOCK) {
            block_t incoming;
            if (block_from_csv_row(msg.payload, &incoming) == OK) {
                //chain_append can realloc and move the array, so comparing the
                //pointers is not reliable: save the tip index before the append
                const block_t *old_tip = chain_tip(chain);
                int had_tip = (old_tip != NULL);
                uint64_t old_index = had_tip ? old_tip->index : 0;

                append_result_t res = chain_append(chain, &incoming);

                if (res == APPEND_OK || res == APPEND_REPLACED) {
                    const block_t *new_tip = chain_tip(chain);
                    //APPEND_REPLACED swaps the tip keeping the same index, so it
                    //changes the tip even if the index doesn't move
                    int tip_changed = (res == APPEND_REPLACED) || !had_tip ||
                                         (new_tip != NULL && new_tip->index != old_index);
                    if (new_tip != NULL && tip_changed) {
                        for (size_t i = 0; i < new_tip->tx_count; i++) {
                            mempool_remove(new_tip->txs[i]);
                        }

                        //the chain moved: see how our own candidates ended up
                        pending_settle(chain);
                        tip_moved = 1;
                    }
                }
            }
        }
    }
    return tip_moved;
}

int main(int argc, char *argv[]) {

    if (argc != 6) {
        fprintf(stderr, "Usage: %s <idx> <num_nodes> <num_miners> <difficulty> <bootstrap_csv>\n", argv[0]);
        return ARGS_ERROR;
    }

    int idx = atoi(argv[1]);
    int num_nodes = atoi(argv[2]);
    int num_miner = atoi(argv[3]);
    int difficulty = atoi(argv[4]);
    const char *bootstrap_csv = argv[5];
    (void)num_miner; //parsed for the command line, not used here

    //difficulty is used as "random() % difficulty": 0 would be a division by zero
    if (num_nodes < 1 || num_nodes > MAX_NODES || difficulty < 1) {
        fprintf(stderr, "Invalid arguments: num_nodes must be 1..%d, difficulty at least 1\n", MAX_NODES);
        return ARGS_ERROR;
    }

    if (log_init("miner") != OK) {
        fprintf(stderr, "Cannot open the log file\n");
        return FILE_ERROR;
    }
    ipc_install_handlers(ROLE_MINER);

    srandom(getpid() ^ time(NULL));

    if (transaction_init() != OK) {
        log_msg(LOG_ERROR, "Cannot initialize the transactions");
        log_close();
        return SYS_ERROR;
    }

    int inbox;

    if (ipc_open_inbox(ROLE_MINER, idx, 1, &inbox) != OK) {
        log_msg(LOG_ERROR, "Cannot open the miner inbox");
        transaction_cleanup();
        log_close();
        return IPC_ERROR;
    }

    int *node_fds = malloc(num_nodes *sizeof(int));
    if (node_fds == NULL) {
        log_msg(LOG_ERROR, "Cannot allocate the node descriptors");
        close(inbox);
        transaction_cleanup();
        log_close();
        return SYS_ERROR;
    }

    for (int i = 0; i < num_nodes; i++) {
        if (ipc_open_sender(ROLE_NODE, i, &node_fds[i]) != OK) {
            char log_buf[256];
            snprintf(log_buf, sizeof(log_buf), "Cannot open the FIFO to the node %d", i);
            log_msg(LOG_ERROR, log_buf);
            //close the descriptors already opened
            for (int k = 0; k < i; k++) {
                close(node_fds[k]);
            }
            free(node_fds);
            close(inbox);
            transaction_cleanup();
            log_close();
            return IPC_ERROR;
        }
    }

    chain_t chain;
    chain_init(&chain); //csv_load expects an initialized and empty chain
    if (csv_load(bootstrap_csv, &chain) != OK) {
        log_msg(LOG_ERROR, "Error loading the chain");
        chain_free(&chain);
        for (int i = 0; i < num_nodes; i++) {
            close(node_fds[i]);
        }
        free(node_fds);
        close(inbox);
        transaction_cleanup();
        log_close();
        return FILE_ERROR;
    }

    // main loop
    while (!g_should_stop) {
        //the candidate does not exist yet here, so a moved tip changes nothing
        (void)drain_inbox(inbox, &chain);

        //a SIGUSR1 that arrived while draining refers to a block we have just
        //read, so clear it here: otherwise the attempts loop below would exit
        //on its first round without ever rolling
        g_abort_mining = 0;

        if (mempool_count == 0) {
            struct timespec req ={1,  0};
            nanosleep(&req, NULL);
            if (g_abort_mining) g_abort_mining = 0;
            continue;
        }

        block_t candidate;
        memset(&candidate, 0, sizeof(candidate));

        const block_t *tip = chain_tip(&chain);
        if (tip == NULL) {
            log_msg(LOG_WARNING, "Empty chain, nothing to mine on");
            struct timespec req ={1,  0};
            nanosleep(&req, NULL);
            continue;
        }

        candidate.index = tip-> index + 1;
        if (block_hash(tip, candidate.prev_hash) != OK) {
            log_msg(LOG_WARNING, "Error computing the hash of the tip");
            continue;
        }
        candidate.timestamp = (uint64_t)time(NULL);
        candidate.nonce = 0;

        candidate.tx_count = (mempool_count > MAX_TX_PER_BLOCK) ? MAX_TX_PER_BLOCK : (size_t)mempool_count;
        const char *tx_ptrs[MAX_TX_PER_BLOCK];

        for (size_t i = 0; i < candidate.tx_count; i++) {
            strcpy(candidate.txs[i], mempool[i]);
            tx_ptrs[i] = candidate.txs[i];
        }

        if (merkle_root(tx_ptrs, candidate.tx_count, candidate.merkle_root) != OK) {
            log_msg(LOG_WARNING, "Error computing the merkle root of the candidate");
            continue;
        }

        int mined = 0;
        while ( !g_should_stop && !g_abort_mining) {
            int sleep_time = MINE_SLEEP_MIN + (random() % (MINE_SLEEP_MAX - MINE_SLEEP_MIN +1));
            sleep(sleep_time);
            if (g_should_stop) break;
            if (g_abort_mining) break;
            //keep reading while mining: incoming transactions land in the
            //mempool for the next block, and a block that moves the tip makes
            //this candidate stale without having to rely on SIGUSR1 alone
            if (drain_inbox(inbox, &chain)) break;

            if ((random() % difficulty) == 0) {
                mined = 1;
                break;
            } else {
                candidate.nonce++;
            }
        }

        if (g_abort_mining) {
            g_abort_mining = 0;
            continue;
        }

        if (mined && !g_should_stop) {
            char b_hash[65];
            if (block_hash(&candidate, b_hash) != OK) {
                log_msg(LOG_WARNING, "Error computing the hash of the mined block");
                continue;
            }

            char log_buf[256];
            snprintf(log_buf, sizeof(log_buf), "Found block %llu (hash: %s)", (unsigned long long)candidate.index, b_hash);

            log_msg(LOG_INFO, log_buf);

            // build the CSV message
            msg_t out_msg;
            memset(&out_msg, 0, sizeof(out_msg));
            out_msg.type = MSG_BLOCK;
            out_msg.sender_role = ROLE_MINER;
            out_msg.sender_idx = idx;
            if (block_to_csv_row(&candidate, out_msg.payload, sizeof(out_msg.payload)) != OK) {
                log_msg(LOG_WARNING, "Error serializing the mined block");
                continue;
            }
            out_msg.payload_len = strlen(out_msg.payload);

            for (int i = 0; i < num_nodes; i++) {
                ipc_send(node_fds[i], &out_msg);
            }

            //the transactions leave the mempool so the next candidate does not
            //mine them a second time, but this block can still lose a tie
            //breaker: keep it until the chain tells us how it ended
            pending_add(&candidate, b_hash);

            for (size_t i = 0; i < candidate.tx_count; i++) {
                mempool_remove(candidate.txs[i]);
            }
        }
    }

    //clean close
    chain_free(&chain);
    close(inbox);
    for (int i = 0; i < num_nodes; i++) {
        close(node_fds[i]);
    }
    free(node_fds);
    transaction_cleanup();
    log_msg(LOG_INFO, "Miner terminated successfully");
    log_close();
    return 0;

}
