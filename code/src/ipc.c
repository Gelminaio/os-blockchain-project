#define _POSIX_C_SOURCE 200809L

#include "include/ipc.h"
#include "include/config.h"
#include "include/errors.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h> // serve per usare read/write
#include <fcntl.h> // per i flag O_NONBLOCK, O_RDWR, O_WRONLY
#include <sys/stat.h> // per mkfifo, mkdir, e i permessi
#include <errno.h>
#include <signal.h> 

volatile sig_atomic_t g_abort_mining = 0;
volatile sig_atomic_t g_should_stop = 0;


//costruttore di indirizzi
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

// viene chiamata dal padre prima di creare i figli con fork()
int ipc_create_all(int n_nodi, int n_miner, int *fds_out){
    
    char percorso[256];
    int current_fd_index = 0;

    // CREAZIONE CARTELLA
    if(mkdir(FIFO_DIR, 0755) == -1 && errno != EEXIST){
        perror("Errore critico su mkdir");
        return IPC_ERROR;
    }

    // CICLO NODI:
    for(int i = 0; i < n_nodi; i++){

        
        if(ipc_fifo_path(percorso, sizeof(percorso), ROLE_NODE, i) != 0){
            return IPC_ERROR;
        }

        // creazione fifo
        if(mkfifo(percorso, 0644) == -1 && errno != EEXIST){
            perror("Errore mkfifo nodo");
            return IPC_ERROR;
        }

        // Apro la FIFO
        int fd = open(percorso, O_RDWR);
        if(fd == -1){
            perror("Errore open O_RDWR nodo");
            return IPC_ERROR;
        }

        // salvo descrittore nell'array
        fds_out[current_fd_index++] = fd;
    }

    // CICLO MINER
    for(int i = 0; i < n_miner; i++){
        
        if(ipc_fifo_path(percorso, sizeof(percorso), ROLE_MINER, i) != 0){
            return IPC_ERROR;
        }

        if(mkfifo(percorso, 0644) == -1 && errno != EEXIST){
            perror("Errore mkfifo miner");
            return IPC_ERROR;
        }

        // Apro la FIFO
        int fd = open(percorso, O_RDWR);
        if(fd == -1){
            perror("Errore open O_RDWR miner");
            return IPC_ERROR;
        }

        // salvo descrittore nell'array
        fds_out[current_fd_index++] = fd;
    }

    // FIFO PADRE
    if(ipc_fifo_path(percorso, sizeof(percorso), ROLE_PARENT, 0) != 0){
            return IPC_ERROR;
    }

    if(mkfifo(percorso, 0644) == -1 && errno != EEXIST){
        perror("Errore mkfifo padre");
        return IPC_ERROR;
    }

    // Apro la FIFO
    int fd = open(percorso, O_RDWR);
    if(fd == -1){
        perror("Errore open O_RDWR padre");
        return IPC_ERROR;
    }

    // salvo descrittore nell'array
    fds_out[current_fd_index++] = fd;
    return OK;

}

// apertura della propria casella dove si leggono i propri messaggi
int ipc_open_inbox(role_t role, int idx, int nonblock, int *fd){
    
    char percorso[256];
    
    // RICAVO LA STRINGA DAL PERCORSO
    if(ipc_fifo_path(percorso, sizeof(percorso), role, idx) != OK){
        return IPC_ERROR;
    }

    //PREPARO I FLAG PARTENDO DALLA LETTURA/SCRITTURA
    int flags = O_RDWR;
    
    //APRO LA FIFO
    int temp_fd = open(percorso, flags);
    if(temp_fd == -1){
        perror("Errore open_inbox");
        return IPC_ERROR;
    } 

    //SALVO E RITORNO
    *fd = temp_fd;
    return OK;

}

int ipc_open_sender(role_t role, int idx,int *fd){
    char percorso[256];
    
    // RICAVO LA STRINGA DAL PERCORSO
    if(ipc_fifo_path(percorso, sizeof(percorso), role, idx) != OK){
        return IPC_ERROR;
    }

    int temp_fd = open(percorso, O_WRONLY| O_NONBLOCK);
    if(temp_fd == -1){
        perror("Errore open_sender");
        return IPC_ERROR;
    }

    //SALVO E RITORNO
    *fd = temp_fd;
    return OK;
}

// lettura e scrittura di messaggi
int ipc_send(int fd, const msg_t *m){
    
    ssize_t res = write(fd, m, sizeof *m);
    if(res == sizeof(msg_t)){
        return OK;
    } 

    if( res == -1 && errno == EAGAIN){
        return IPC_ERROR;
    }else{
        perror("Errore in ipc_send (write)");
        return IPC_ERROR;
    }
}

int ipc_recv(int fd, msg_t *m){
    size_t counter = 0; // contatore dei byte raccolti

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
        return OK;
    }
}

int ipc_recv_nb(int fd, msg_t *m){
    size_t counter = 0; // contatore dei byte raccolti

    while(counter < sizeof(msg_t)){
        ssize_t res = read(fd, (char*) m + counter, sizeof(msg_t) - counter);
        if(res > 0){
            counter += res;

        }else if(res == -1 &&  errno == EINTR && res == 0){
            return IPC_EMPTY;
        }else{
            return IPC_ERROR;
        }
        return OK;
    }
}

int ipc_recv_timeout(int fd, msg_t *m, int secondi){
   int elapsed_ms = 0;
   int max_ms = secondi * 1000;

    while(elapsed_ms < max_ms){
        int res = ipc_recv_nb(fd, m);

        if(res == 0){
            return 0;
        }else if(res == 1){
            usleep(POLL_NAP_MS *1000);
            elapsed_ms += POLL_NAP_MS;
        }else if( res == 2){
            return 2;
        }else{
            return -1;
        }
   }
    return 3;
}

void handle_stop(int signum) {
    g_should_stop = 1;
}

void handle_usr1(int signum) {
    g_abort_mining = 1;
}


// gestione dei segnali POSIX
void ipc_install_handlers(role_t role){
    struct sigaction act;
    act.sa_handler = handle_stop;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    
    //tutti i ruoli installano SIGINT e  SIGTERM
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);

    //solo i miner installano SIGUR1
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
    return 0;
}

int ipc_unlink_all(int n_nodi, int n_miner){
    char path[256];

    //cancella la FIFO dai nodi
    for(int i = 0; i < n_nodi; i++){
        ipc_fifo_path(path, sizeof(path), ROLE_NODE, i);
        unlink(path);
    }

    //cancella la FIFO dai miner
    for(int i = 0; i < n_nodi; i++){
        ipc_fifo_path(path, sizeof(path), ROLE_MINER, i);
        unlink(path);
    }
    //cancella la FIFO  del padre
    ipc_fifo_path(path, sizeof(path), ROLE_PARENT, 0);
        unlink(path);
    
        //rimozione cartella
        rmdir(FIFO_DIR);
    
        return 0;
}
