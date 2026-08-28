//client process: generates random transactions and sends them to the miners
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include "common.h"
#include "transaction.h"
#include "errors.h"
#include "config.h"
#include "ipc.h"
#include "logging.h"

//kill(pid, 0) delivers no signal, it only reports whether the pid still
//exists. The parent keeps every fifo open for the whole run, so a write to a
//dead miner keeps succeeding until the buffer fills and the transactions are
//lost without a trace: the pid is the only way to tell a dead miner from a
//merely busy one.
static int miner_is_alive(pid_t pid) {
    return kill(pid, 0) == 0 || errno == EPERM;
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <idx> <num_miner> <tx_freq> <miner_pid>...\n", argv[0]);
        return ARGS_ERROR;
    }
    int idx = atoi(argv[1]);
    int num_miner = atoi(argv[2]);
    int freq = atoi(argv[3]);

    if (num_miner != argc - 4 || num_miner < 1 || num_miner > MAX_MINERS) {
        fprintf(stderr, "Usage: %s <idx> <num_miner> <tx_freq> <miner_pid>...\n", argv[0]);
        return ARGS_ERROR;
    }

    pid_t miner_pids[MAX_MINERS];
    int miner_alive[MAX_MINERS];
    for (int j = 0; j < num_miner; j++) {
        miner_pids[j] = atoi(argv[j + 4]);
        if (miner_pids[j] <= 0) {
            fprintf(stderr, "PID is not valid: %s\n", argv[j + 4]);
            return ARGS_ERROR;
        }
        miner_alive[j] = 1;
    }

    log_init("client");
    ipc_install_handlers(ROLE_CLIENT);
    srandom(getpid() ^ time(NULL));
    transaction_init();
    int fd[MAX_MINERS];
    for (int j = 0; j < num_miner; j++) {
        ipc_open_sender(ROLE_MINER, j, &fd[j]);
    }

    struct timespec ts;
    if (freq == 1) {
        ts.tv_sec = 1;
        ts.tv_nsec = 0;
    } else {
        ts.tv_sec = 0;
        ts.tv_nsec = 1000000000L / freq;
    }

    int counter = 0;
    int no_miner_logged = 0; //the "no miner left" message is worth logging once, not once per transaction
    char tx[MAX_TX_LEN];
    char log_buf[256];

    while (!g_should_stop) {
        if (nanosleep(&ts, NULL) == -1) {
            if (errno == EINTR) {
                continue;
            }
        }
        transaction_generate_random(tx, sizeof(tx));
        if (transaction_is_valid(tx) != OK) {
            snprintf(log_buf, sizeof(log_buf), "Generated an invalid transaction, skipped: %s", tx);
            log_msg(LOG_WARNING, log_buf);
            continue;
        }

        msg_t m;
        memset(&m, 0, sizeof(m));
        m.type = MSG_TX;
        m.sender_role = ROLE_CLIENT;
        m.sender_idx = idx;
        snprintf(m.payload, sizeof(m.payload), "%s", tx);
        m.payload_len = strlen(tx);
        //round robin over the survivors only: a miner that died is skipped from
        //now on, otherwise every transaction addressed to it would be lost
        int miner_target = -1;
        for (int j = 0; j < num_miner; j++) {
            int cand = (counter + j) % num_miner;
            if (!miner_alive[cand]) {
                continue;
            }
            if (miner_is_alive(miner_pids[cand])) {
                miner_target = cand;
                break;
            }
            miner_alive[cand] = 0;
            snprintf(log_buf, sizeof(log_buf), "Miner %d (PID %d) is gone, it will not be addressed anymore", cand, (int)miner_pids[cand]);
            log_msg(LOG_WARNING, log_buf);
        }
        counter++;

        if (miner_target < 0) {
            if (!no_miner_logged) {
                log_msg(LOG_ERROR, "No miner is alive anymore, the generated transactions are dropped");
                no_miner_logged = 1;
            }
            continue;
        }

        if (ipc_send(fd[miner_target], &m) != OK) {
            if (errno == EAGAIN) {
                snprintf(log_buf, sizeof(log_buf), "Miner %d inbox full, sending %s failed (EAGAIN)", miner_target, tx);
                log_msg(LOG_WARNING, log_buf);
            }
        } else {
            snprintf(log_buf, sizeof(log_buf), "Sent to miner %d: %s", miner_target, tx);
            log_msg(LOG_INFO, log_buf);
        }
    }
    transaction_cleanup();
    for (int j = 0; j < num_miner; j++) {
        close(fd[j]);
    }

    snprintf(log_buf, sizeof(log_buf), "Client %d terminated successfully", idx);
    log_msg(LOG_INFO, log_buf);

    log_close();
    return 0;
}