#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

#include "include/crypto.h"
#include "include/config.h"
#include "include/errors.h"
#include "include/common.h"
#include "include/ipc.h"
#include "include/block.h"
#include "include/logging.h"
#include "include/transaction.h"

#define MEMPOOL_SIZE 64
static char mempool[MEMPOOL_SIZE][MAX_TX_LENGHT];
static char mempool_count = 0;

static void mempool_add(const char *txs){
        
    if(mempool_count < MEMPOOL_SIZE){
        strncpy(mempool[mempool_count], tx, MAX_TX_LENGHT - 1);
        mempool[mempool_count][MAX_TX_LENGHT - 1] = '\0';
        mempool_count ++;
    }else{
        log_msg(LOG_WARN, "Mempool piena", transazione scartata);
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
        fprintf(stderr, "Uso: %s <idx> <num_nodi> <num_miner> <difficulty> <bootstrap_csv>\n", argv[0]));
        return 1;
    }

    int idx = atoi(argv[1]);
    int num_nodi = atoi(argv[2]);
    int num_miner = atoi(argv[3]);
    int difficulty = atoi(argv[4]);
    const char *bootstrap_csv = argv[5];

    log_init("miner");
    ipc_install_handlers(ROLE_MINER);

    srandom(getpid() ^ time(NULL));
    transaction_init();
    int inbox;

    if(ipc_open_inbox(ROLE_MINER, idx, 1, &inbox) != OK){
        log_msg(LOG_ERROR, "Impossibile aprire inbox miner");
        return OK;
    }

    int *node_fds = malloc(num_nodi *sizeof(int));
    for(int i = 0; i < num_nodi; i++){
        ipc_open_sender(ROLE_NODE, i, &node_fds[i]);
    }

    chain_t chain;
    if(csv_load(bootstrap_csv, &chain) != OK){
        og_msg(LOG_ERROR, "Errore caricamento catena");
        return 1;
    }
    msg_t msg;

    // creo ciclo principale
    while(!g_should_stop){
        while(ipc_recv_nb(inbox, &msg) > 0){
            if(msg.type == MSG_TX){
                if(transaction_is_valid(msg.payload)){
                    mempool_add(msg.payload);
                }
            }else if (msg.type == MSG_BLOCK){
                block_t b_arrivato;
                if(block_from_csv_row(msg.payload, &b_arrivato) == OK){
                    const block_t *vecchia_punta = chain_tip(&chain);
                    if(chain_append(&chain, &b_arrivato) == APPEND_OK){
                        const block_t *nuova_punta = chain_tip(&chain);
                        if(nuova_punta != vecchia_punta){
                            for(int i = 0; i < nuova_punta->tx_count; i++){
                                mempool_remove(nuova_punta->transactions[i]);
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
        const block_t *tip = chain_tip(&chain);
        candidato.index = tip-> index + 1;
        block_hash(tip, candidato.prev_hash);
        candidato.timestamp = (uint64_t)time(NULL);
        candidato.nonce = 0;

        candidato.tx_count = mempool_count > MAX_TX_PER_BLOCK) ? MAX_TX_PER_BLOCK : mempool_count;
        const char *tx_ptrs[MAX_TX_PER_BLOCK]
        
        for(int i = 0; i < candidato.tx_count; i++){
            strcpy(candidato.transaztion[i], mempool[i]);
            tx_ptrs[i] = candidato.transactions[i];
        }
    
    }






























}




