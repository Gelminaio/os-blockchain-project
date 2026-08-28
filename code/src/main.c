//parent process: prepares the bootstrap chain and the fifos, spawns nodes,
//miners and clients, then runs the CLI and shuts everything down
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "errors.h"
#include "config.h"
#include "common.h"
#include "logging.h"
#include "ipc.h"
#include "block.h"
#include "cli.h"
#include "crypto.h"
#include "transaction.h"

static proc_t g_procs[MAX_NODES + MAX_MINERS + MAX_CLIENTS];
static size_t g_nprocs = 0;

static int parse_args(int argc, char *argv[], params_t *p) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <num_nodes> <num_miners> <num_clients> [tx_freq] [difficulty] [csv_path]\n", argv[0]);
        return ARGS_ERROR;
    }

    p->num_nodes = atoi(argv[1]);
    p->num_miners = atoi(argv[2]);
    p->num_clients = atoi(argv[3]);
    p->transaction_frequency = (argc > 4) ? atoi(argv[4]) : DEFAULT_TX_FREQ;
    p->difficulty = (argc > 5) ? atoi(argv[5]) : DEFAULT_DIFFICULTY;

    //the upper bounds matter as much as the lower ones: the fds array and
    //g_procs are sized on the config.h limits, so going over them used to
    //overflow the stack and crash after the children had already been forked
    if (p->num_nodes < 1 || p->num_nodes > MAX_NODES) {
        fprintf(stderr, "Error: nodes must be between 1 and %d.\n", MAX_NODES);
        return ARGS_ERROR;
    }
    if (p->num_miners < 1 || p->num_miners > MAX_MINERS) {
        fprintf(stderr, "Error: miners must be between 1 and %d.\n", MAX_MINERS);
        return ARGS_ERROR;
    }
    if (p->num_clients < 0 || p->num_clients > MAX_CLIENTS) {
        fprintf(stderr, "Error: clients must be between 0 and %d.\n", MAX_CLIENTS);
        return ARGS_ERROR;
    }

    //with 0 the children die on a division by zero and the prompt would show
    //a system that looks alive but is not
    if (p->transaction_frequency < 1) {
        fprintf(stderr, "Error: the transaction frequency must be at least 1.\n");
        return ARGS_ERROR;
    }
    if (p->difficulty < 1) {
        fprintf(stderr, "Error: the difficulty must be at least 1.\n");
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

    if (log_init("main") != OK) {
        fprintf(stderr, "Cannot open the log file\n");
        return FILE_ERROR;
    }

    if (transaction_init() != OK) {
        log_msg(LOG_ERROR, "Cannot initialize the transactions");
        log_close();
        return SYS_ERROR;
    }

    struct sigaction sa_chld, sa_stop;
    memset(&sa_chld, 0, sizeof(sa_chld));
    sa_chld.sa_handler = reap_children;
    //SA_NOCLDSTOP: without it the kernel also sends SIGCHLD when a child is
    //stopped with SIGSTOP or resumed with SIGCONT, and that interrupts the
    //CLI. reap_children only collects children that have terminated.
    //SA_RESTART: so the fgets of the CLI restarts instead of returning NULL
    //when a child dies, which the CLI would read as end of input.
    //Only this handler: sa_stop below and the ones in ipc_install_handlers
    //must keep sa_flags 0, or the sleep of the miner would not be
    //interrupted by SIGUSR1 any more and the mining abort would stop working.
    sa_chld.sa_flags = SA_NOCLDSTOP | SA_RESTART;
    sigaction(SIGCHLD, &sa_chld, NULL);

    memset(&sa_stop, 0, sizeof(sa_stop));
    sa_stop.sa_handler = handle_stop;
    sigaction(SIGINT, &sa_stop, NULL);
    sigaction(SIGTERM, &sa_stop, NULL);

    chain_t chain;
    chain_init(&chain);
    const char *initial_csv = (argc > 6) ? argv[6] : NULL;
    char log_buf[MAX_PATH_LEN + 128];

    //a path that simply does not exist is not an error: the run starts from a
    //new genesis block, like when no csv is passed at all. Every other failure
    //(unreadable file, bad header, malformed or inconsistent blocks) must still
    //stop the run, so we only skip the load on ENOENT and let csv_load report
    //anything else
    if (initial_csv != NULL && access(initial_csv, F_OK) != 0 && errno == ENOENT) {
        snprintf(log_buf, sizeof(log_buf), "Initial CSV %s does not exist: starting from a new genesis block", initial_csv);
        log_msg(LOG_INFO, log_buf);
        printf("%s\n", log_buf);
        initial_csv = NULL;
    }

    if (initial_csv != NULL) {
        int load_res = csv_load(initial_csv, &chain);
        if (load_res != OK) {
            fprintf(stderr, "Error loading the initial CSV\n");
            chain_free(&chain);
            transaction_cleanup();
            log_close();
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
        if (merkle_root(genesis_txs, 1, b.merkle_root) != OK) {
            log_msg(LOG_ERROR, "Error computing the merkle root of the genesis block");
            chain_free(&chain);
            transaction_cleanup();
            log_close();
            return INVALID_BLOCK;
        }

        //chain_init leaves v == NULL and cap == 0: only chain_append allocates
        if (chain_append(&chain, &b) != APPEND_OK) {
            log_msg(LOG_ERROR, "Cannot create the genesis block");
            chain_free(&chain);
            transaction_cleanup();
            log_close();
            return INVALID_BLOCK;
        }
    }

    if (csv_save(BOOTSTRAP_CSV, &chain) != OK) {
        log_msg(LOG_ERROR, "Error saving the bootstrap chain");
        chain_free(&chain);
        transaction_cleanup();
        log_close();
        return FILE_ERROR;
    }


    int fds[MAX_NODES + MAX_MINERS + 1];
    if (ipc_create_all(p.num_nodes, p.num_miners, fds) != OK) {
        log_msg(LOG_ERROR, "Error creating the fifos");
        chain_free(&chain);
        transaction_cleanup();
        log_close();
        return IPC_ERROR;
    }





    char buf_nodes[16], buf_miner[16], buf_diff[16], buf_freq[16];
    snprintf(buf_nodes, sizeof(buf_nodes), "%d", p.num_nodes);
    snprintf(buf_miner, sizeof(buf_miner), "%d", p.num_miners);
    snprintf(buf_diff, sizeof(buf_diff), "%d", p.difficulty);
    snprintf(buf_freq, sizeof(buf_freq), "%d", p.transaction_frequency);

    for (int i = 0; i < p.num_miners; i++) {
        char buf_idx[16];
        snprintf(buf_idx, sizeof(buf_idx), "%d", i);
        char *args[] = { "miner", buf_idx, buf_nodes, buf_miner, buf_diff, BOOTSTRAP_CSV, NULL };

        pid_t pid = spawn_child("./miner", args);
        if (pid > 0) {
            g_procs[g_nprocs++] = (proc_t){ pid, ROLE_MINER, i, 1 };
        }
    }

    for (int i = 0; i < p.num_nodes; i++) {
        char buf_idx[16];
        snprintf(buf_idx, sizeof(buf_idx), "%d", i);

        char **args = malloc((6 + p.num_miners + 1) * sizeof(char*));
        args[0] = "node"; args[1] = buf_idx; args[2] = buf_nodes;
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
        //the clients get the miner pids for the same reason the nodes do: it is
        //the only way for them to notice that a miner is gone and stop sending
        //transactions into a fifo nobody reads anymore
        char **args = malloc((4 + p.num_miners + 1) * sizeof(char*));
        args[0] = "client"; args[1] = buf_idx;
        args[2] = buf_miner; args[3] = buf_freq;

        int arg_idx = 4;
        for (size_t m = 0; m < g_nprocs; m++) {
            if (g_procs[m].role == ROLE_MINER) {
                args[arg_idx] = malloc(16);
                snprintf(args[arg_idx++], 16, "%d", g_procs[m].pid);
            }
        }
        args[arg_idx] = NULL;

        pid_t pid = spawn_child("./client", args);
        for (int m = 4; m < arg_idx; m++) free(args[m]);
        free(args);

        if (pid > 0) {
            g_procs[g_nprocs++] = (proc_t){ pid, ROLE_CLIENT, i, 1 };
        }
    }


    int cli_res = cli_run(g_procs, g_nprocs, &p);
    if (cli_res != OK) {
        log_msg(LOG_ERROR, "The CLI ended with an error");
    }

    shutdown_all(g_procs, g_nprocs);
    ipc_unlink_all(p.num_nodes, p.num_miners);

    chain_free(&chain);
    transaction_cleanup();
    log_close();
    return cli_res;
}