#ifndef IPC_H
#define IPC_H

#include <stddef.h>
#include <sys/types.h>
#include <signal.h>
#include "common.h"



//FLAG GLOBALI MODIFIATI DAI GESTORI DEI SEGNALI
extern volatile sig_atomic_t g_abort_mining;
extern volatile sig_atomic_t g_should_stop;

// costruzione percorsi e setup iniziale/finale
int ipc_fifo_path(char *out, size_t cap, role_t role, int idx);
int ipc_create_all(int n_nodi, int n_miner, int *fds_out);
int ipc_unlink_all(int n_nodi, int n_miner);

// apertura delle comunicazioni
int ipc_open_inbox(role_t role, int idx, int nonblock, int *fd);
int ipc_open_sender(role_t role, int idx, int nonblock, int *fd);


// lettura e scrittura di messaggi
int ipc_send(int fd, const msg_t *m);
int ipc_recv(int fd, msg_t *m);
int ipc_recv_nb(int fd, msg_t *m);
int ipc_recv_timeout(int fd, msg_t *m, int secondi);

// gestione dei segnali POSIX
void ipc_install_handlers(role_t role);
int ipc_signal_pids(const pid_t *pids, size_t n, int sig);



#endif