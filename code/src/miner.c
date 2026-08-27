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

#define MEMPOOL_SIZE 64
static char mempool[MEMPOOL_SIZE][MAX_TX_LEN];
static int mempool_count = 0;

static void mempool_add(const char *txs){
        
    if(mempool_count < MEMPOOL_SIZE){
        strncpy(mempool[mempool_count], txs, MAX_TX_LEN - 1);
        mempool[mempool_count][MAX_TX_LEN - 1] = '\0';
        mempool_count ++;
    }else{
        log_msg(LOG_WARNING, "Mempool full, transaction discarded");
    }
}

static void mempool_remove(const char *tx){
    for(int i = 0; i < mempool_count; i++){
        if(strcmp(mempool[i], tx) == 0){
            for(int j = i; j < mempool_count - 1; j++){
                strcpy(mempool[j], mempool[j+1]);
            }
            mempool_count--;
            break;
        }

    }
}

// MAIN
int main(int argc, char *argv[]){

    if(argc != 6){
        fprintf(stderr, "Usage: %s <idx> <num_nodes> <num_miners> <difficulty> <bootstrap_csv>\n", argv[0]);
        return ARGS_ERROR;
    }

    int idx = atoi(argv[1]);
    int num_nodi = atoi(argv[2]);
    int num_miner = atoi(argv[3]);
    int difficulty = atoi(argv[4]);
    const char *bootstrap_csv = argv[5];
    (void)num_miner; //parsed for the command line, not used here

    //difficulty is used as "random() % difficulty": 0 would be a division by zero
    if(num_nodi < 1 || num_nodi > MAX_NODES || difficulty < 1){
        fprintf(stderr, "Invalid arguments: num_nodes must be 1..%d, difficulty at least 1\n", MAX_NODES);
        return ARGS_ERROR;
    }

    if(log_init("miner") != OK){
        fprintf(stderr, "Cannot open the log file\n");
        return FILE_ERROR;
    }
    ipc_install_handlers(ROLE_MINER);

    srandom(getpid() ^ time(NULL));

    if(transaction_init() != OK){
        log_msg(LOG_ERROR, "Cannot initialize the transactions");
        log_close();
        return SYS_ERROR;
    }

    int inbox;

    if(ipc_open_inbox(ROLE_MINER, idx, 1, &inbox) != OK){
        log_msg(LOG_ERROR, "Cannot open the miner inbox");
        transaction_cleanup();
        log_close();
        return IPC_ERROR;
    }

    int *node_fds = malloc(num_nodi *sizeof(int));
    if(node_fds == NULL){
        log_msg(LOG_ERROR, "Cannot allocate the node descriptors");
        close(inbox);
        transaction_cleanup();
        log_close();
        return SYS_ERROR;
    }

    for(int i = 0; i < num_nodi; i++){
        if(ipc_open_sender(ROLE_NODE, i, &node_fds[i]) != OK){
            char log_buf[256];
            snprintf(log_buf, sizeof(log_buf), "Cannot open the FIFO to the node %d", i);
            log_msg(LOG_ERROR, log_buf);
            //close the descriptors already opened
            for(int k = 0; k < i; k++){
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
    if(csv_load(bootstrap_csv, &chain) != OK){
        log_msg(LOG_ERROR, "Error loading the chain");
        chain_free(&chain);
        for(int i = 0; i < num_nodi; i++){
            close(node_fds[i]);
        }
        free(node_fds);
        close(inbox);
        transaction_cleanup();
        log_close();
        return FILE_ERROR;
    }
    msg_t msg;

    // main loop
    while(!g_should_stop){
        while(ipc_recv_nb(inbox, &msg) == OK){
            if(msg.type == MSG_TX){
                if(transaction_is_valid(msg.payload) == OK){
                    mempool_add(msg.payload);
                }
            }else if (msg.type == MSG_BLOCK){
                block_t b_arrivato;
                if(block_from_csv_row(msg.payload, &b_arrivato) == OK){
                    //chain_append can realloc and move the array, so comparing the
                    //pointers is not reliable: save the tip index before the append
                    const block_t *vecchia_punta = chain_tip(&chain);
                    int aveva_punta = (vecchia_punta != NULL);
                    uint64_t vecchio_indice = aveva_punta ? vecchia_punta->index : 0;

                    append_result_t res = chain_append(&chain, &b_arrivato);

                    if(res == APPEND_OK || res == APPEND_REPLACED){
                        const block_t *nuova_punta = chain_tip(&chain);
                        //APPEND_REPLACED swaps the tip keeping the same index, so it
                        //changes the tip even if the index doesn't move
                        int punta_cambiata = (res == APPEND_REPLACED) || !aveva_punta ||
                                             (nuova_punta != NULL && nuova_punta->index != vecchio_indice);
                        if(nuova_punta != NULL && punta_cambiata){
                            for(size_t i = 0; i < nuova_punta->tx_count; i++){
                                mempool_remove(nuova_punta->txs[i]);
                            }
                        }
                    }
                }
            }
        }
        
        if(mempool_count == 0){
            struct timespec req ={1,  0};
            nanosleep(&req, NULL);
            if(g_abort_mining) g_abort_mining = 0;
            continue;
        }

        block_t candidato;
        memset(&candidato, 0, sizeof(candidato));

        const block_t *tip = chain_tip(&chain);
        if(tip == NULL){
            log_msg(LOG_WARNING, "Empty chain, nothing to mine on");
            struct timespec req ={1,  0};
            nanosleep(&req, NULL);
            continue;
        }

        candidato.index = tip-> index + 1;
        if(block_hash(tip, candidato.prev_hash) != OK){
            log_msg(LOG_WARNING, "Error computing the hash of the tip");
            continue;
        }
        candidato.timestamp = (uint64_t)time(NULL);
        candidato.nonce = 0;

        candidato.tx_count = (mempool_count > MAX_TX_PER_BLOCK) ? MAX_TX_PER_BLOCK : (size_t)mempool_count;
        const char *tx_ptrs[MAX_TX_PER_BLOCK];

        for(size_t i = 0; i < candidato.tx_count; i++){
            strcpy(candidato.txs[i], mempool[i]);
            tx_ptrs[i] = candidato.txs[i];
        }

        if(merkle_root(tx_ptrs, candidato.tx_count, candidato.merkle_root) != OK){
            log_msg(LOG_WARNING, "Error computing the merkle root of the candidate");
            continue;
        }

        int minato = 0;
        while( !g_should_stop && !g_abort_mining){
            int sleep_time = MINE_SLEEP_MIN + (random() % (MINE_SLEEP_MAX - MINE_SLEEP_MIN +1));
            sleep(sleep_time);
            if(g_should_stop) break;
            if(g_abort_mining) break;

            if((random() % difficulty) == 0){
                minato = 1;
                break;
            }else{
                candidato.nonce++;
            }
        }

        if(g_abort_mining){
            g_abort_mining = 0;
            continue;
        }

        if(minato && !g_should_stop){
            char b_hash[65];
            if(block_hash(&candidato, b_hash) != OK){
                log_msg(LOG_WARNING, "Error computing the hash of the mined block");
                continue;
            }
    
            char log_buf[256];
            snprintf(log_buf, sizeof(log_buf), "Found block %llu (hash: %s)", (unsigned long long)candidato.index, b_hash);
        
            log_msg(LOG_INFO, log_buf);

            // build the CSV message
            msg_t out_msg;
            memset(&out_msg, 0, sizeof(out_msg));
            out_msg.type = MSG_BLOCK;
            out_msg.sender_role = ROLE_MINER;
            out_msg.sender_idx = idx;
            if(block_to_csv_row(&candidato, out_msg.payload, sizeof(out_msg.payload)) != OK){
                log_msg(LOG_WARNING, "Error serializing the mined block");
                continue;
            }
            out_msg.payload_len = strlen(out_msg.payload);
        
            for(int i = 0; i < num_nodi; i++){
                ipc_send(node_fds[i], &out_msg);
            }

            for(size_t i = 0; i < candidato.tx_count; i++){
                mempool_remove(candidato.txs[i]);
            }
        }
    }

    //clean close
    chain_free(&chain);
    close(inbox);
    for(int i = 0; i < num_nodi; i++){
        close(node_fds[i]);
    }
    free(node_fds);
    transaction_cleanup();
    log_msg(LOG_INFO, "Miner terminated successfully");
    log_close();
    return 0;

}
