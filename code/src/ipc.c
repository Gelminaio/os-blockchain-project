#define _POSIX_C_SOURCE 200809L

#include "ipc.h"
#include "config.h"
#include "errors.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h> // for read/write
#include <fcntl.h> // for the O_NONBLOCK, O_RDWR, O_WRONLY flags
#include <sys/stat.h> // for mkfifo, mkdir and the permissions
#include <errno.h>
#include <signal.h>
#include <time.h> // for nanosleep

volatile sig_atomic_t g_abort_mining = 0;
volatile sig_atomic_t g_should_stop = 0;


//builds the fifo paths
int ipc_fifo_path(char *out, size_t cap, role_t role, int idx){
   
    int res;

    switch(role){
        case ROLE_PARENT:
            res = snprintf(out, cap, "%s/parent", FIFO_DIR);
            break;
        case ROLE_NODE:
            res = snprintf(out, cap, "%s/node-%d", FIFO_DIR, idx);
            break;
        case ROLE_MINER:
            res = snprintf(out, cap, "%s/miner-%d", FIFO_DIR, idx);
            break;
        default:
            return ARGS_ERROR;
    }

    if(res < 0 || (size_t)res >= cap){
        return IPC_ERROR;

    }else{
        return OK;
    }
}

// called by the parent before creating the children with fork()
int ipc_create_all(int n_nodi, int n_miner, int *fds_out){
    
    char percorso[256];
    int current_fd_index = 0;

    // create the directory
    if(mkdir(FIFO_DIR, 0755) == -1 && errno != EEXIST){
        perror("mkdir failed");
        return IPC_ERROR;
    }

    // nodes loop
    for(int i = 0; i < n_nodi; i++){

        
        if(ipc_fifo_path(percorso, sizeof(percorso), ROLE_NODE, i) != OK){
            return IPC_ERROR;
        }

        // create the fifo
        if(mkfifo(percorso, 0644) == -1 && errno != EEXIST){
            perror("mkfifo failed for node");
            return IPC_ERROR;
        }

        // open the fifo
        int fd = open(percorso, O_RDWR);
        if(fd == -1){
            perror("open O_RDWR failed for node");
            return IPC_ERROR;
        }

        // save the descriptor in the array
        fds_out[current_fd_index++] = fd;
    }

    // miners loop
    for(int i = 0; i < n_miner; i++){
        
        if(ipc_fifo_path(percorso, sizeof(percorso), ROLE_MINER, i) != OK){
            return IPC_ERROR;
        }

        if(mkfifo(percorso, 0644) == -1 && errno != EEXIST){
            perror("mkfifo failed for miner");
            return IPC_ERROR;
        }

        // open the fifo
        int fd = open(percorso, O_RDWR);
        if(fd == -1){
            perror("open O_RDWR failed for miner");
            return IPC_ERROR;
        }

        // save the descriptor in the array
        fds_out[current_fd_index++] = fd;
    }

    // parent fifo
    if(ipc_fifo_path(percorso, sizeof(percorso), ROLE_PARENT, 0) != OK){
            return IPC_ERROR;
    }

    if(mkfifo(percorso, 0644) == -1 && errno != EEXIST){
        perror("mkfifo failed for parent");
        return IPC_ERROR;
    }

    // open the fifo
    int fd = open(percorso, O_RDWR);
    if(fd == -1){
        perror("open O_RDWR failed for parent");
        return IPC_ERROR;
    }

    // save the descriptor in the array
    fds_out[current_fd_index++] = fd;
    return OK;

}

// opens your own inbox, where you read your messages
int ipc_open_inbox(role_t role, int idx, int nonblock, int *fd){
    
    char percorso[256];
    
    //build the path string
    if(ipc_fifo_path(percorso, sizeof(percorso), role, idx) != OK){
        return IPC_ERROR;
    }

    //start the flags from read/write
    int flags = O_RDWR;

    //the miner checks the inbox without stopping: add O_NONBLOCK
    if(nonblock != 0){
        flags |= O_NONBLOCK;
    }
    
    //open the fifo
    int temp_fd = open(percorso, flags);
    if(temp_fd == -1){
        perror("open_inbox failed");
        return IPC_ERROR;
    } 

    //save and return
    *fd = temp_fd;
    return OK;

}

int ipc_open_sender(role_t role, int idx,int *fd){
    char percorso[256];
    
    //build the path string
    if(ipc_fifo_path(percorso, sizeof(percorso), role, idx) != OK){
        return IPC_ERROR;
    }

    int temp_fd = open(percorso, O_WRONLY| O_NONBLOCK);
    if(temp_fd == -1){
        perror("open_sender failed");
        return IPC_ERROR;
    }

    //save and return
    *fd = temp_fd;
    return OK;
}

// reading and writing messages
int ipc_send(int fd, const msg_t *m){
    
    ssize_t res = write(fd, m, sizeof *m);
    if(res == sizeof(msg_t)){
        return OK;
    } 

    if( res == -1 && errno == EAGAIN){
        return IPC_ERROR;
    }else{
        perror("ipc_send failed (write)");
        return IPC_ERROR;
    }
}

int ipc_recv(int fd, msg_t *m){
    size_t counter = 0; // counter of the collected bytes

    while(counter < sizeof(msg_t)){
        ssize_t res = read(fd, (char*) m + counter, sizeof(msg_t) - counter);
        if(res > 0){
            counter += res;
        }
        else if(res == -1 &&  errno == EINTR){
            return IPC_INTR;
        }else{
            return IPC_ERROR;
        }
    }
    return OK;
}

int ipc_recv_nb(int fd, msg_t *m){
    size_t counter = 0; // counter of the collected bytes

    while(counter < sizeof(msg_t)){
        ssize_t res = read(fd, (char*) m + counter, sizeof(msg_t) - counter);
        if(res > 0){
            counter += res;

        }else if(res == -1 && errno == EINTR){
            return IPC_INTR;
        }
        else if((res == 0 || (res == -1 && errno == EAGAIN)) && counter == 0){
            //empty inbox: no byte read in this round
            return IPC_EMPTY;
        }else{
            return IPC_ERROR;
        }
    }
    return OK;
}

int ipc_recv_timeout(int fd, msg_t *m, int secondi){
   int elapsed_ms = 0;
   int max_ms = secondi * 1000;

    while(elapsed_ms < max_ms){
        int res = ipc_recv_nb(fd, m);

        if(res == OK){
            return OK;
        }else if(res == IPC_EMPTY){
            struct timespec nap = { POLL_NAP_MS / 1000, (POLL_NAP_MS % 1000) * 1000000L };
            nanosleep(&nap, NULL);
            elapsed_ms += POLL_NAP_MS;
        }else if( res == IPC_INTR){
            return IPC_INTR;
        }else{
            return IPC_ERROR;
        }
   }
    return IPC_TIMEOUT;
}

//inside a signal handler you can only set a flag
static void handle_stop(int signum) {
    (void)signum;
    g_should_stop = 1;
}

static void handle_usr1(int signum) {
    (void)signum;
    g_abort_mining = 1;
}


// POSIX signal handling
void ipc_install_handlers(role_t role){
    struct sigaction act;
    act.sa_handler = handle_stop;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    
    //every role installs SIGINT and SIGTERM
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);

    //only the miners install SIGUSR1
    if(role == ROLE_MINER){
        struct sigaction act_usr1;
        act_usr1.sa_handler = handle_usr1;
        sigemptyset(&act_usr1.sa_mask);
        act_usr1.sa_flags = 0;

        sigaction(SIGUSR1, &act_usr1, NULL);
        
    }
}

int ipc_signal_pids(const pid_t *pids, size_t n, int sig){
    for(size_t i = 0; i < n;i++){
        kill(pids[i], sig);
    }
    return OK;
}

int ipc_unlink_all(int n_nodi, int n_miner){
    char path[256];

    //delete the nodes' fifos
    for(int i = 0; i < n_nodi; i++){
        ipc_fifo_path(path, sizeof(path), ROLE_NODE, i);
        unlink(path);
    }

    //delete the miners' fifos
    for(int i = 0; i < n_miner; i++){
        ipc_fifo_path(path, sizeof(path), ROLE_MINER, i);
        unlink(path);
    }
    //delete the parent's fifo
    ipc_fifo_path(path, sizeof(path), ROLE_PARENT, 0);
        unlink(path);
    
        //remove the directory
        rmdir(FIFO_DIR);

        return OK;
}
