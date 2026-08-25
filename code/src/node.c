#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include "common.h"
#include "config.h"
#include "errors.h"
#include "block.h"
#include "transaction.h"
#include "ipc.h"
#include "logging.h"

static chain_t g_chain;
static int g_idx;
static int g_num_nodes;
static int g_num_miners;
static pid_t g_miner_pids[MAX_MINERS];
static char g_bootstrap_csv[MAX_PATH_LEN];
static char g_snapshot_path[MAX_PATH_LEN];

static int g_inbox;
static int g_fd_node[MAX_NODES];
static int g_fd_miner[MAX_MINERS];
static int g_fd_parent;

static char g_log[512];

#define SEEN_MAX 64
static hex64_t g_seen[SEEN_MAX];
static size_t g_seen_pos = 0;
static size_t g_seen_count = 0;

static chain_t g_recovery;
static int g_recovering = 0;

static int parse_args(int argc, char *argv[]) {
    if(argc < 5) {
        return ARGS_ERROR;
    }
    g_idx = atoi(argv[1]);
    g_num_nodes = atoi(argv[2]);
    g_num_miners = atoi(argv[3]);

    if((g_num_miners != argc - 5) || (g_idx < 0) || (g_idx >= g_num_nodes) || (g_num_nodes < 1) || (g_num_nodes > MAX_NODES ) || (g_num_miners < 1) || (g_num_miners > MAX_MINERS)) {
        return ARGS_ERROR;
    }

    strncpy(g_bootstrap_csv, argv[4], MAX_PATH_LEN - 1);
    g_bootstrap_csv[MAX_PATH_LEN - 1] = '\0';

    for(int i = 0; i < g_num_miners; i++) {
        pid_t current_pid = atoi(argv[i+5]);
        if(current_pid <= 0) {
            fprintf(stderr, "PID is not valid: %s\n", argv[i+5]);
            return ARGS_ERROR;
        } 
        g_miner_pids[i] = current_pid; 
    }
    return OK;
}
static int seen_contains(const char *hash) {
    for(size_t i = 0; i < g_seen_count; i++) {
        if(strcmp(hash, g_seen[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static void seen_add(const char *hash) {
    snprintf(g_seen[g_seen_pos], HASH_HEX_LEN + 1, "%s", hash);
    g_seen_pos = (g_seen_pos + 1) % SEEN_MAX;
    if(g_seen_count < SEEN_MAX) {
        g_seen_count++; //increments if there is still space (otherwise overwrites a value)
    }
}

static void reply_to(const msg_t *req, msg_t *rep) {
    int fd;

    rep->sender_role = ROLE_NODE;
    rep->sender_idx  = g_idx;

    switch (req->sender_role) {
        case ROLE_PARENT:
            fd = g_fd_parent;
            break;
        case ROLE_NODE:
            if ((int)req->sender_idx == g_idx || (int)req->sender_idx >= g_num_nodes) {
                snprintf(g_log, sizeof(g_log), "Invalid node index in request: %u", req->sender_idx);
                log_msg(LOG_WARNING, g_log);
                return;
            }
            fd = g_fd_node[req->sender_idx];
            break;
        default:
            snprintf(g_log, sizeof(g_log), "Cannot reply to role: %u", req->sender_role);
            log_msg(LOG_WARNING, g_log);
            return;
    }

    if (ipc_send(fd, rep) != OK) {
        snprintf(g_log, sizeof(g_log), "Error sending the reply to role %u index %u", req->sender_role, req->sender_idx);
        log_msg(LOG_WARNING, g_log);
    }
}

static void handle_block(const msg_t *m) {
    block_t b;
    if(block_from_csv_row(m->payload, &b) != OK) {
        snprintf(g_log, sizeof(g_log), "Received malformed block from role %d idx %d", m->sender_role, m->sender_idx);
        log_msg(LOG_WARNING, g_log);
        return;
    }

    hex64_t h;
    if(block_hash(&b, h) != OK) {
        snprintf(g_log, sizeof(g_log), "Error computing the hash of the block received from role %d idx %d", m->sender_role, m->sender_idx);
        log_msg(LOG_WARNING, g_log);
        return;
    }

    if(seen_contains(h)) {
        snprintf(g_log, sizeof(g_log), "Block %llu already seen, ignored (Gossip break)", (unsigned long long)b.index);
        log_msg(LOG_WARNING, g_log);
        return;
    }

    append_result_t res = chain_append(&g_chain, &b);

    switch (res) {
        case APPEND_OK:
        case APPEND_REPLACED:
            //save on disk
            if (csv_save(g_snapshot_path, &g_chain) != OK) {
                log_msg(LOG_ERROR, "Error during chain save on disk");
                return; 
            }

            //add the hash to the seen
            seen_add(h);

            //gossip to the node colleagues
            for (int i = 0; i < g_num_nodes; i++) {
                if (i != g_idx) {
                    ipc_send(g_fd_node[i], m);
                }
            }

            //warn the miners (signal + message)
            for (int j = 0; j < g_num_miners; j++) {
                ipc_send(g_fd_miner[j], m);
            }
            ipc_signal_pids(g_miner_pids, (size_t)g_num_miners, SIGUSR1);
            
            snprintf(g_log, sizeof(g_log), "Accepted block %llu, saved and propagated.", (unsigned long long)b.index);
            log_msg(LOG_INFO, g_log);
            break;

        case APPEND_DUP:
            seen_add(h); //do not forward it anymore
            snprintf(g_log, sizeof(g_log), "Received a duplicate of block %llu", (unsigned long long)b.index);
            log_msg(LOG_WARNING, g_log);
            break;

        case APPEND_STALE:
            snprintf(g_log, sizeof(g_log), "Discarded old block %llu", (unsigned long long)b.index);
            log_msg(LOG_WARNING, g_log);
            break;

        case APPEND_AHEAD: {
            snprintf(g_log, sizeof(g_log), "Block ahead (CHAIN_MISMATCH). Requesting chain for recovery.");
            log_msg(LOG_WARNING, g_log);

            msg_t req;
            memset(&req, 0, sizeof(msg_t));
            req.type = MSG_CHAIN_REQUEST;
            req.sender_role = ROLE_NODE;
            req.sender_idx = g_idx;
            snprintf(req.payload, sizeof(req.payload), "ALL");
            req.payload_len = strlen(req.payload);

            //requesting from the sender, or from node 0 if it was a miner
            if(m->sender_role == ROLE_NODE) {
                ipc_send(g_fd_node[m->sender_idx], &req);
            } 
            else {
                ipc_send(g_fd_node[0], &req);
            }
            break;
        }

        case APPEND_INVALID:
            //the reason has already been logged by block.c
            log_msg(LOG_WARNING, "Discarded block with APPEND_INVALID");
            break;
    }
}

static void handle_chain_request(const msg_t *m) {
    msg_t rep;
    memset(&rep, 0, sizeof(msg_t));
    rep.type = MSG_REPLY;
    rep.seq = 0;
    rep.last = 0;

    size_t start_idx = 0;
    if(strncmp(m->payload, "INDEX ", 6) == 0) {
        start_idx = (size_t) strtoull(m->payload + 6, NULL, 10); 
    }
    else if(strncmp(m->payload, "HASH ", 5) == 0) {
        const block_t *b = chain_find_hash(&g_chain, m->payload + 5);
        if(b) {
            start_idx = b->index;
        } 
    } //ALL starts from 0
    
    //if chain is empty or the request is out of scale
    if (start_idx >= g_chain.len) {
        rep.last = 1;
        rep.payload_len = 0;
        reply_to(m, &rep);
        return;
    }

    char row_buf[MAX_CSV_ROW_LEN];
    rep.payload[0] = '\0';
    size_t curr_len = 0;

    for (size_t i = start_idx; i < g_chain.len; i++) {
        block_to_csv_row(&(g_chain.v[i]), row_buf, sizeof(row_buf));
        size_t row_len = strlen(row_buf);

        //check payload capacity
        if (curr_len + row_len + 1 >= MSG_PAYLOAD_MAX - 1) {
            //send the piece
            rep.payload_len = curr_len;
            reply_to(m, &rep);
            
            //prepare the next one
            rep.seq++;
            rep.payload[0] = '\0';
            curr_len = 0;
        }

        snprintf(rep.payload + curr_len, sizeof(rep.payload) - curr_len, "%s\n", row_buf);
        curr_len += (row_len + 1);
    }

    //send the last piece
    rep.last = 1;
    rep.payload_len = curr_len;
    reply_to(m, &rep);
}

static void handle_block_request(const msg_t *m) {
    msg_t rep;
    memset(&rep, 0, sizeof(msg_t));
    rep.type = MSG_REPLY;
    rep.seq = 0;
    rep.last = 1;
    
    const block_t *b = NULL;
    if(strncmp(m->payload, "INDEX ", 6) == 0) {
        uint64_t i = strtoull(m->payload + 6, NULL, 10);
        b = chain_find_index(&g_chain, i);
    }
    else if(strncmp(m->payload, "HASH ", 5) == 0) {
        b = chain_find_hash(&g_chain, m->payload + 5);
    }

    if (b == NULL) {
        snprintf(rep.payload, sizeof(rep.payload), "ERR:Block not found");
    } else {
        block_to_csv_row(b, rep.payload, sizeof(rep.payload));
    }
    rep.payload_len = strlen(rep.payload);
    reply_to(m, &rep);

    if (b == NULL) {
        snprintf(g_log, sizeof(g_log), "Block request from role %u index %u: block not found", m->sender_role, m->sender_idx);
    } else {
        snprintf(g_log, sizeof(g_log), "Block request from role %u index %u: block found", m->sender_role, m->sender_idx);
    }
    log_msg(LOG_INFO, g_log);
}

static void handle_reply(const msg_t *m) {
    if (!g_recovering) {
        if(m->seq == 0) {
            chain_init(&g_recovery);
            g_recovering = 1;
        } 
        else {
            return; //orphan fragment, ignored
        }
    }

    //parsing of received CSV rows (same schema as cli.c)
    const char *current = m->payload;
    while(current != NULL && *current != '\0') {
        const char *next_newline = strchr(current, '\n');
        char row[MAX_CSV_ROW_LEN];
        size_t len;
        if(next_newline != NULL) {
            len = (size_t)(next_newline - current);
        } 
        else {
            len = strlen(current);
        }

        if(len > 0 && len < MAX_CSV_ROW_LEN) {
            memcpy(row, current, len);
            row[len] = '\0';

            block_t b;
            if(block_from_csv_row(row, &b) == OK) {
                //during recovery we ignore errors, at the end we discard if invalid
                chain_append(&g_recovery, &b); 
            }
        }
        if(next_newline != NULL) {
            current = next_newline + 1;
        }
        else {
            break;
        }
    }

    if (m->last == 1) {
        //decision: is the chain strictly longer?
        if(g_recovery.len > g_chain.len) {
            chain_free(&g_chain);
            g_chain = g_recovery; //direct assignment
            
            csv_save(g_snapshot_path, &g_chain);
            snprintf(g_log, sizeof(g_log), "Recovery completed: adopted new chain of %zu blocks", g_chain.len);
            log_msg(LOG_INFO, g_log);
        } 
        else {
            chain_free(&g_recovery);
            snprintf(g_log, sizeof(g_log), "Recovery failed/rejected (length %zu vs mine %zu)", g_recovery.len, g_chain.len);
            log_msg(LOG_INFO, g_log);
        }
        g_recovering = 0;
    }
}

int main(int argc, char *argv[]) {
    int result = parse_args(argc, argv);
    if(result != OK) {
        return result;
    }
    snprintf(g_snapshot_path, sizeof(g_snapshot_path), "chain-node-%d.csv", g_idx);
    result = log_init("node");
    if(result != OK) {
        return result;
    }
    ipc_install_handlers(ROLE_NODE);
    result = transaction_init();
    if(result != OK) {
        log_close();
        return result;
    }
    chain_init(&g_chain);
    result = csv_load(g_bootstrap_csv, &g_chain);
    if(result != OK) {
        chain_free(&g_chain);
        transaction_cleanup();
        log_close();
        return result;
    }

    //channel opening
    if (ipc_open_inbox(ROLE_NODE, g_idx, 0, &g_inbox) != OK) {
        log_msg(LOG_ERROR, "Error opening the inbox");
        chain_free(&g_chain);
        transaction_cleanup();
        log_close();
        return IPC_ERROR;
    }
    for (int i = 0; i < g_num_nodes; i++) {
        if (i == g_idx) continue;
        if (ipc_open_sender(ROLE_NODE, i, &g_fd_node[i]) != OK) {
            chain_free(&g_chain);
            transaction_cleanup();
            log_close();
            return IPC_ERROR;
        }
    }
    for (int j = 0; j < g_num_miners; j++) {
        if (ipc_open_sender(ROLE_MINER, j, &g_fd_miner[j]) != OK) {
            chain_free(&g_chain);
            transaction_cleanup();
            log_close();
            return IPC_ERROR;
        }
    }
    if (ipc_open_sender(ROLE_PARENT, 0, &g_fd_parent) != OK) {
        chain_free(&g_chain);
        transaction_cleanup();
        log_close();
        return IPC_ERROR;
    }

    while (!g_should_stop) {
        msg_t m;
        int r = ipc_recv(g_inbox, &m);

        if (r != OK) {
            if (errno == EINTR) continue;
            log_msg(LOG_ERROR, "Error receiving the message");
            continue;
        }

        switch (m.type) {
            case MSG_BLOCK: 
                handle_block(&m);          
                break;
            case MSG_CHAIN_REQUEST:  
                handle_chain_request(&m);  
                break;
            case MSG_BLOCK_REQUEST:  
                handle_block_request(&m);  
                break;
            case MSG_REPLY:          
                handle_reply(&m);          
                break;
            default:                 
                snprintf(g_log, sizeof(g_log), "Unknown message type: %d", m.type);
                log_msg(LOG_WARNING, g_log);  
                break;
        }
    }

    //clean close
    csv_save(g_snapshot_path, &g_chain);
    
    close(g_inbox);
    close(g_fd_parent);
    for (int i = 0; i < g_num_nodes; i++) { 
        if (i != g_idx) {
            close(g_fd_node[i]);
        } 
    }
    for (int j = 0; j < g_num_miners; j++) { 
        close(g_fd_miner[j]); 
    }

    chain_free(&g_chain);
    transaction_cleanup();
    log_msg(LOG_INFO, "Node terminated successfully");
    log_close();

    return 0;
}