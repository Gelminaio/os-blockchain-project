//
// Created by faitn on 26/08/2026.
//
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>
#include "errors.h"
#include "config.h"
#include "common.h"
#include "logging.h"
#include "ipc.h"
#include "block.h"
#include "cli.h"
#include "crypto.h"

static proc_t g_procs[MAX_NODES + MAX_MINERS + MAX_CLIENTS];
static size_t g_nprocs = 0;
/*volatile sig_atomic_t g_should_stop = 0;*/

static int parse_args(int argc, char *argv[], params_t *p) {
    if (argc < 4) {
        fprintf(stderr, "Uso: %s <num_nodes> <num_miners> <num_clients> [tx_freq] [difficulty] [csv_path]\n", argv[0]);
        return ARGS_ERROR;
    }

    p->num_nodes = atoi(argv[1]);
    p->num_miners = atoi(argv[2]);
    p->num_clients = atoi(argv[3]);
    p->transaction_frequency = (argc > 4) ? atoi(argv[4]) : DEFAULT_TX_FREQ;
    p->difficulty = (argc > 5) ? atoi(argv[5]) : DEFAULT_DIFFICULTY;

    if (p->num_nodes < 1 || p->num_miners < 1 || p->num_clients < 0) {
        fprintf(stderr, "Errore: i nodi e i miner devono essere almeno 1, i client almeno 0.\n");
        return ARGS_ERROR;
    }

    return OK;
}

static void handle_stop(int signum) {
    (void)signum;
    g_should_stop = 1;
}

static void reap_children(int signum) {
    (void)signum;
    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (size_t i = 0; i < g_nprocs; i++) {
            if (g_procs[i].pid == pid) {
                g_procs[i].alive = 0;
                break;
            }
        }
    }
}

static pid_t spawn_child(const char *path, char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    } else if (pid == 0) {
        execv(path, argv);
        perror("execv");
        _exit(SYS_ERROR);
    }
    return pid;
}

static void shutdown_all(proc_t *tab, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (tab[i].alive) kill(tab[i].pid, SIGTERM);
    }

    int waited = 0;
    while (waited < 30) {
        int all_dead = 1;
        for (size_t i = 0; i < n; i++) {
            if (tab[i].alive) all_dead = 0;
        }
        if (all_dead) break;

        waitpid(-1, NULL, WNOHANG);
        usleep(100000);
        waited++;
    }

    for (size_t i = 0; i < n; i++) {
        if (tab[i].alive) {
            kill(tab[i].pid, SIGKILL);
            waitpid(tab[i].pid, NULL, 0);
            tab[i].alive = 0;
        }
    }
}

int main(int argc, char *argv[]) {
    params_t p;
    if (parse_args(argc, argv, &p) != OK) {
        return ARGS_ERROR;
    }

    log_init("main");

    struct sigaction sa_chld, sa_stop;
    memset(&sa_chld, 0, sizeof(sa_chld));
    sa_chld.sa_handler = reap_children;
    sigaction(SIGCHLD, &sa_chld, NULL);

    memset(&sa_stop, 0, sizeof(sa_stop));
    sa_stop.sa_handler = handle_stop;
    sigaction(SIGINT, &sa_stop, NULL);
    sigaction(SIGTERM, &sa_stop, NULL);

    chain_t chain;
    chain_init(&chain);
    const char *initial_csv = (argc > 6) ? argv[6] : NULL;

    if (initial_csv != NULL) {
        int load_res = csv_load(initial_csv, &chain);
        if (load_res != OK) {
            fprintf(stderr, "Errore caricamento CSV iniziale\n");
            return load_res;
        }
    }

    if (chain.len == 0) {
        block_t b;
        memset(&b, 0, sizeof(b));
        b.index = 0;
        b.timestamp = time(NULL);
        memset(b.prev_hash, '0', 64);
        b.prev_hash[64] = '\0';
        snprintf(b.txs[0], MAX_TX_LEN, "Genesis block");
        b.tx_count = 1;
        b.nonce = 0;

        const char *genesis_txs[] = { b.txs[0] };
        merkle_root(genesis_txs, 1, b.merkle_root);

        chain.v[0] = b;
        chain.len = 1;
    }
    csv_save(BOOTSTRAP_CSV, &chain);


    int fds[MAX_NODES + MAX_MINERS + 1];
    ipc_create_all(p.num_nodes, p.num_miners, fds);





    char buf_nodi[16], buf_miner[16], buf_diff[16], buf_freq[16];
    snprintf(buf_nodi, sizeof(buf_nodi), "%d", p.num_nodes);
    snprintf(buf_miner, sizeof(buf_miner), "%d", p.num_miners);
    snprintf(buf_diff, sizeof(buf_diff), "%d", p.difficulty);
    snprintf(buf_freq, sizeof(buf_freq), "%d", p.transaction_frequency);

    for (int i = 0; i < p.num_miners; i++) {
        char buf_idx[16];
        snprintf(buf_idx, sizeof(buf_idx), "%d", i);
        char *args[] = { "miner", buf_idx, buf_nodi, buf_miner, buf_diff, BOOTSTRAP_CSV, NULL };

        pid_t pid = spawn_child("./miner", args);
        if (pid > 0) {
            g_procs[g_nprocs++] = (proc_t){ pid, ROLE_MINER, i, 1 };
        }
    }

    for (int i = 0; i < p.num_nodes; i++) {
        char buf_idx[16];
        snprintf(buf_idx, sizeof(buf_idx), "%d", i);

        char **args = malloc((6 + p.num_miners + 1) * sizeof(char*));
        args[0] = "node"; args[1] = buf_idx; args[2] = buf_nodi;
        args[3] = buf_miner; args[4] = BOOTSTRAP_CSV;

        int arg_idx = 5;
        for (size_t m = 0; m < g_nprocs; m++) {
            if (g_procs[m].role == ROLE_MINER) {
                args[arg_idx] = malloc(16);
                snprintf(args[arg_idx++], 16, "%d", g_procs[m].pid);
            }
        }
        args[arg_idx] = NULL;

        pid_t pid = spawn_child("./node", args);
        for (int m = 5; m < arg_idx; m++) free(args[m]);
        free(args);

        if (pid > 0) {
            g_procs[g_nprocs++] = (proc_t){ pid, ROLE_NODE, i, 1 };
        }
    }

    for (int i = 0; i < p.num_clients; i++) {
        char buf_idx[16];
        snprintf(buf_idx, sizeof(buf_idx), "%d", i);
        char *args[] = { "client", buf_idx, buf_miner, buf_freq, NULL };

        pid_t pid = spawn_child("./client", args);
        if (pid > 0) {
            g_procs[g_nprocs++] = (proc_t){ pid, ROLE_CLIENT, i, 1 };
        }
    }


    cli_run(g_procs, g_nprocs, &p);

    shutdown_all(g_procs, g_nprocs);
    ipc_unlink_all(p.num_nodes, p.num_miners);

    log_close();
    return 0;
}