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

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <idx> <num_miner> <tx_freq>\n", argv[0]);
        return ARGS_ERROR;
    }
    int idx = atoi(argv[1]);
    int num_miner = atoi(argv[2]);
    int freq = atoi(argv[3]);

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

    int contatore = 0;
    char tx[MAX_TX_LEN];
    char log_buf[256];

    while (!g_should_stop) {
        if (nanosleep(&ts, NULL) == -1) {
            if (errno == EINTR) {
                continue;
            }
        }
        transaction_generate_random(tx, sizeof(tx));
        transaction_is_valid(tx);
        msg_t m;
        memset(&m, 0, sizeof(m));
        m.type = MSG_TX;
        m.sender_role = ROLE_CLIENT;
        m.sender_idx = idx;
        snprintf(m.payload, sizeof(m.payload), "%s", tx);
        m.payload_len = strlen(tx);
        int miner_target = contatore % num_miner;
        contatore++;
        if (ipc_send(fd[miner_target], &m) != OK) {
            if (errno == EAGAIN) {
                snprintf(log_buf, sizeof(log_buf), "Miner %d full/dead, sending %s failed (EAGAIN)", miner_target, tx);
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