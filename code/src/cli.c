//the parent's interactive command line
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include "common.h"
#include "errors.h"
#include "logging.h"
#include "ipc.h"
#include "transaction.h"
#include "block.h"

static const char* role_to_string(role_t role) {
    switch (role) {
        case ROLE_NODE:
            return "node";
        case ROLE_MINER:
            return "miner";
        case ROLE_CLIENT:
            return "client";
        default:
            return "unknown";
    }
}

//a send to a dead miner works but the transaction is lost, so we look for one that is still alive
static int next_alive_miner(const proc_t *tab, size_t n, size_t *cursor, int num_miners) {
    for (int k = 0; k < num_miners; k++) {
        int cand = (int)((*cursor + (size_t)k) % (size_t)num_miners);
        for (size_t i = 0; i < n; i++) {
            if (tab[i].role == ROLE_MINER && tab[i].idx == cand && tab[i].alive) {
                *cursor = (size_t)cand + 1;
                return cand;
            }
        }
    }
    return -1;
}

//the requests went only to node 0, so a dead node 0 meant just a timeout: we look for one that is alive
static int first_alive_node(const proc_t *tab, size_t n, int num_nodes) {
    for (int cand = 0; cand < num_nodes; cand++) {
        for (size_t i = 0; i < n; i++) {
            if (tab[i].role == ROLE_NODE && tab[i].idx == cand && tab[i].alive) {
                return cand;
            }
        }
    }
    return -1;
}

int cli_run(proc_t *tab, size_t n, const params_t *p) {
    if (n > MAX_NODES + MAX_MINERS + MAX_CLIENTS || tab == NULL || p == NULL) {
        return ARGS_ERROR;
    }

    char input[1024]; //maximum CLI input length (it shoud be abundant)

    int announced[MAX_NODES + MAX_MINERS + MAX_CLIENTS]; //array used to flag if a process's death has already been announced
    for (size_t i = 0; i<n; i++) {
        announced[i] = 0;
    }


    int fd_miner[MAX_MINERS];
    int fd_node[MAX_NODES];
    int fd_inbox;
    char log_buf[256];

    for (int j = 0; j < p->num_miners; j++) {
        if (ipc_open_sender(ROLE_MINER, j, &fd_miner[j]) != OK) {
            snprintf(log_buf, sizeof(log_buf), "Error opening FIFO to the miner %d", j);
            log_msg(LOG_ERROR, log_buf);
            for (int k = 0; k < j; k++) {
                close(fd_miner[k]);
            }
            return IPC_ERROR;
        }
    }

    for (int i = 0; i < p->num_nodes; i++) {
        if (ipc_open_sender(ROLE_NODE, i, &fd_node[i]) != OK) {
            snprintf(log_buf, sizeof(log_buf), "Error opening FIFO to the node %d", i);
            log_msg(LOG_ERROR, log_buf);
            for (int k = 0; k < p->num_miners; k++) {
                close(fd_miner[k]);
            }
            for (int k = 0; k < i; k++) {
                close(fd_node[k]);
            }
            return IPC_ERROR;
        }
    }

    if (ipc_open_inbox(ROLE_PARENT, 0, 0, &fd_inbox) != OK) {
        log_msg(LOG_ERROR, "Error opening the inbox FIFO of the parent");
        for (int k = 0; k < p->num_miners; k++) {
            close(fd_miner[k]);
        }
        for (int k = 0; k < p->num_nodes; k++) {
            close(fd_node[k]);
        }
        return IPC_ERROR;
    }

    size_t current_miner = 0;

    while (1) {
        for (size_t i = 0; i < n; i++) {
            if (!tab[i].alive && !announced[i]) {
                printf("\n[NOTICE] The %s %d (PID: %d) ended unexpectedly.\n", role_to_string(tab[i].role), tab[i].idx, tab[i].pid);
                announced[i] = 1;
            }
        }

        printf("> ");
        fflush(stdout);

        //if there is an error or user enters ctrl + D
        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("\nQuitting...\n");
            break;
        }

        input[strcspn(input, "\n")] = '\0';

        if (input[0] == '\0') {
            continue;
        }

        //command management

        if (strcmp(input, "stop") == 0) {
            break;
        }
        else if (strcmp(input, "help") == 0) {
            printf("Command list:\n");
            printf("- submit [transaction]\n");
            printf("- request blockchain [--index i | --hash h]\n");
            printf("- request block --index i/--hash h\n");
            printf("- save blockchain <file>\n");
            printf("- pause\n");
            printf("- resume\n");
            printf("- stop\n");
            printf("- help\n");
        }
        else if (strcmp(input, "pause") == 0) {
            pid_t pid_alive[MAX_NODES + MAX_MINERS + MAX_CLIENTS];
            size_t m = 0;

            for (size_t i = 0; i < n; i++) {
                if (tab[i].alive) {
                    pid_alive[m] = tab[i].pid;
                    m++;
                }
            }
            if (m > 0) {
                ipc_signal_pids(pid_alive, m, SIGSTOP);
            }
            printf("%zu processes have been stopped\n", m);
        }
        else if (strcmp(input, "resume") == 0) {
            pid_t pid_alive[MAX_NODES + MAX_MINERS + MAX_CLIENTS];
            size_t m = 0;

            for (size_t i = 0; i < n; i++) {
                if (tab[i].alive) {
                    pid_alive[m] = tab[i].pid;
                    m++;
                }
            }
            if (m > 0) {
                ipc_signal_pids(pid_alive, m, SIGCONT);
            }
            printf("%zu processes have been resumed\n", m);
        }
        else if (strncmp(input, "submit ", 7) == 0) {
            char *first_quote = strchr(input, '"');
            char *last_quote = strrchr(input, '"');

            if (first_quote == NULL || last_quote == NULL || first_quote == last_quote) {
                printf("Wrong usage. Expected format: submit \"<transaction>\"\n");
                continue;
            }

            size_t tx_len = last_quote - first_quote - 1;

            if (tx_len >= MAX_TX_LEN) {
                printf("Error: the transaction exceeds the maximum allowed length.\n");
                continue;
            }

            char tx[MAX_TX_LEN];
            memcpy(tx, first_quote+1, tx_len);
            tx[tx_len] = '\0';

            int result = transaction_is_valid(tx);
            if (result != OK) {
                printf("Invalid transaction (INVALID_TRANSACTION, code %d). Not sent.\n", INVALID_TRANSACTION);
                continue;
            }

            msg_t msg;
            memset(&msg, 0, sizeof(msg_t));
            msg.type = MSG_TX;
            msg.sender_role = ROLE_PARENT;
            msg.sender_idx = 0;
            snprintf(msg.payload, sizeof(msg.payload), "%s", tx);

            msg.payload_len = strlen(msg.payload);

            int miner_idx = next_alive_miner(tab, n, &current_miner, p->num_miners);
            if (miner_idx < 0) {
                printf("No miner is alive: the transaction was not sent.\n");
                continue;
            }
            if (ipc_send(fd_miner[miner_idx], &msg) != OK) {
                printf("IPC error: impossible to send the transaction to the miner %d.\n", miner_idx);
                continue;
            }
            printf("Valid transaction. Sent to the miner %d.\n", miner_idx);
        }
        else if (strncmp(input, "request blockchain", 18) == 0) {
            char filter[MSG_PAYLOAD_MAX] = "ALL";

            //args parsing (filter is optional)
            size_t len = strlen(input);
            if (len != 18) {
                if (len <= 19 || input[18] != ' ') {
                    printf("Wrong usage. Use: request blockchain [--index <i> | --hash <h>]\n");
                    continue;
                }
                char *arg = input + 19; //skip "request blockchain "
                if (strncmp(arg, "--index ", 8) == 0) {
                    snprintf(filter, sizeof(filter), "INDEX %s", arg + 8);
                } else if (strncmp(arg, "--hash ", 7) == 0) {
                    snprintf(filter, sizeof(filter), "HASH %s", arg + 7);
                } else {
                    printf("Unknown filter. Use: request blockchain [--index <i> | --hash <h>]\n");
                    continue;
                }
            }

            //message build
            msg_t msg;
            memset(&msg, 0, sizeof(msg_t));
            msg.type = MSG_CHAIN_REQUEST;
            msg.sender_role = ROLE_PARENT;
            msg.sender_idx = 0;
            snprintf(msg.payload, sizeof(msg.payload), "%s", filter);
            msg.payload_len = strlen(msg.payload);

            //send to block 0
            int node_idx = first_alive_node(tab, n, p->num_nodes);
            if (node_idx < 0) {
                printf("No node is alive: the request was not sent.\n");
                continue;
            }
            if (ipc_send(fd_node[node_idx], &msg) != OK) {
                printf("IPC error: impossible to send the request to node %d.\n", node_idx);
                continue;
            }

            //block reception (Timeout cycle)
            msg_t rep;
            while (1) {
                if (ipc_recv_timeout(fd_inbox, &rep, REPLY_TIMEOUT_S) != OK) {
                    printf("Error or IPC timeout: node %d didn't respond in time.\n", node_idx);
                    break;
                }

                if (strncmp(rep.payload, "ERR:", 4) == 0) {
                    printf("Error from node: %s\n", rep.payload + 4);
                    printf("[Error code: BLOCK_NOT_FOUND]\n");
                    break;
                }

                printf("%s\n", rep.payload);

                //last piece case
                if (rep.last == 1) {
                    break;
                }
            }
        }
        else if (strncmp(input, "request block", 13) == 0) {
            char filter[MSG_PAYLOAD_MAX] = "";

            //here the filter is mandatory
            if (strlen(input) > 14 && input[13] == ' ') {
                char *arg = input + 14; //skip "request block "
                if (strncmp(arg, "--index ", 8) == 0) {
                    snprintf(filter, sizeof(filter), "INDEX %s", arg + 8);
                } else if (strncmp(arg, "--hash ", 7) == 0) {
                    snprintf(filter, sizeof(filter), "HASH %s", arg + 7);
                } else {
                    printf("Unknown filter. Use: --index or --hash.\n");
                    continue;
                }
            } else {
                printf("Wrong use: request block --index <i> | --hash <h>\n");
                continue;
            }

            //message build
            msg_t msg;
            memset(&msg, 0, sizeof(msg_t));
            msg.type = MSG_BLOCK_REQUEST;
            msg.sender_role = ROLE_PARENT;
            msg.sender_idx = 0;
            snprintf(msg.payload, sizeof(msg.payload), "%s", filter);
            msg.payload_len = strlen(msg.payload);

            int node_idx = first_alive_node(tab, n, p->num_nodes);
            if (node_idx < 0) {
                printf("No node is alive: the request was not sent.\n");
                continue;
            }
            if (ipc_send(fd_node[node_idx], &msg) != OK) {
                printf("IPC error: impossible to send the request to node %d.\n", node_idx);
                continue;
            }

            //block reception (Timeout cycle) (here we expect only one cycle)
            msg_t rep;
            while (1) {
                if (ipc_recv_timeout(fd_inbox, &rep, REPLY_TIMEOUT_S) != OK) {
                    printf("Error or IPC timeout: node %d didn't respond in time.\n", node_idx);
                    break;
                }

                if (strncmp(rep.payload, "ERR:", 4) == 0) {
                    printf("Error from node: %s\n", rep.payload + 4);
                    printf("[Error code: BLOCK_NOT_FOUND]\n");
                    break;
                }

                printf("%s\n", rep.payload);

                if (rep.last == 1) {
                    break;
                }
            }
        }
        else if (strncmp(input, "save blockchain", 15) == 0) {
            char *file_name = NULL;
            //the file name is mandatory
            if (strlen(input) > 16 && input[15] == ' ') {
                file_name = input + 16; //skip "save blockchain "
                if (strlen(file_name) >= MAX_PATH_LEN) {
                    printf("File name is too long.\n");
                    continue;
                }
            } else {
                printf("Wrong use: save blockchain <file>\n");
                continue;
            }

            //message build
            msg_t msg;
            memset(&msg, 0, sizeof(msg_t));
            msg.type = MSG_CHAIN_REQUEST;
            msg.sender_role = ROLE_PARENT;
            msg.sender_idx = 0;
            snprintf(msg.payload, sizeof(msg.payload), "ALL");
            msg.payload_len = strlen(msg.payload);

            int node_idx = first_alive_node(tab, n, p->num_nodes);
            if (node_idx < 0) {
                printf("No node is alive: the request was not sent.\n");
                continue;
            }
            if (ipc_send(fd_node[node_idx], &msg) != OK) {
                printf("IPC error: impossible to send the request to node %d.\n", node_idx);
                continue;
            }

            chain_t chain;
            chain_init(&chain);
            int rx_error = 0;

            msg_t rep;
            while (1) {
                if (ipc_recv_timeout(fd_inbox, &rep, REPLY_TIMEOUT_S) != OK) {
                    printf("Error or IPC timeout: node %d didn't respond in time.\n", node_idx);
                    break;
                }

                if (strncmp(rep.payload, "ERR:", 4) == 0) {
                    printf("Error from node: %s\n", rep.payload + 4);
                    rx_error = 1;
                    break;
                }

                //row extraction (separated by \n)
                char *current = rep.payload;
                while (current != NULL && *current != '\0') {
                    char *next_newline = strchr(current, '\n');
                    char row[MAX_CSV_ROW_LEN];
                    size_t len;

                    //calculate current length
                    if (next_newline != NULL) {
                        len = next_newline - current;
                    } else {
                        len = strlen(current);
                    }

                    //prevent buffer overflow and prevent empty lines
                    if (len >= MAX_CSV_ROW_LEN) {
                        printf("[Warning] Ignored line because too long.\n");
                    } else if (len > 0) {
                        memcpy(row, current, len);
                        row[len] = '\0';

                        block_t b;

                        if (block_from_csv_row(row, &b) != OK) {
                            printf("[Warning] Line is not valid for one block, skipped: %s\n", row);
                        } else {
                            if (chain_append(&chain, &b) != APPEND_OK) {
                                printf("[Warning] Block with index %zu rejected from the chain (mismatch), skipped.\n", b.index);
                            }
                        }
                    }

                    if (next_newline != NULL) {
                        current = next_newline + 1; //skips \n
                    } else {
                        break; //the string is over
                    }
                }

                if (rep.last == 1) {
                    break;
                }
            }
            //file save
            if (!rx_error) {
                if (chain.len == 0) {
                    printf("No valid block received. Save cancelled.\n");
                } else {
                    int save_res = csv_save(file_name, &chain);
                    if (save_res != OK) {
                        printf("Error during the save (Code %d).\n", save_res);
                    } else {
                        printf("Saved %zu blocks to %s\n", chain.len, file_name);
                    }
                }
            }

            //memory cleaning
            chain_free(&chain);
        }
        else {
            printf("Unknown command. Enter 'help' for the command list.\n");
        }
    }

    for (int j = 0; j < p->num_miners; j++) {
        close(fd_miner[j]);
    }
    for (int j = 0; j < p->num_nodes; j++) {
        close(fd_node[j]);
    }
    close(fd_inbox);

    return OK;
}
