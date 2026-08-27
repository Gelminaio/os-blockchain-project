#ifndef IPC_H
#define IPC_H

#include <stddef.h>
#include <sys/types.h>
#include <signal.h>
#include "common.h"



//global flags modified by the signal handlers
extern volatile sig_atomic_t g_abort_mining;
extern volatile sig_atomic_t g_should_stop;

// path building and initial/final setup
int ipc_fifo_path(char *out, size_t cap, role_t role, int idx);
int ipc_create_all(int n_nodes, int n_miner, int *fds_out);
int ipc_unlink_all(int n_nodes, int n_miner);

// opening the communications
int ipc_open_inbox(role_t role, int idx, int nonblock, int *fd);

//a sender is always non blocking: the way the IPC is designed, whoever writes
//must never stop and wait for the receiver, so the fifo is always opened with
//O_NONBLOCK and there is no need for a parameter to choose it
int ipc_open_sender(role_t role, int idx, int *fd);


// reading and writing messages
int ipc_send(int fd, const msg_t *m);
int ipc_recv(int fd, msg_t *m);
int ipc_recv_nb(int fd, msg_t *m);
int ipc_recv_timeout(int fd, msg_t *m, int seconds);

// POSIX signal handling
void ipc_install_handlers(role_t role);
int ipc_signal_pids(const pid_t *pids, size_t n, int sig);



#endif