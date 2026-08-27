//writes timestamped lines to logs/<role>-<pid>.log
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include "config.h"
#include "errors.h"
#include "logging.h"

static int fd = -1;
static char log_role[20];
static pid_t pid;

int log_init(const char *role) {
    int result = mkdir(LOG_DIR, 0755); //755 -> read/write/execute for the creator, read/execute for the others

    if (result != 0 && errno != EEXIST) { //if there is an error and the error is not "folder already exists"
        perror("mkdir logs");
        return FILE_ERROR;
    }

    snprintf(log_role, sizeof(log_role), "%s", role); //copy string role in log_role (global variable)

    char filepath[256]; //256 maximum file path length (abundant)
    pid = getpid();
    int pid_int = (int) pid;
    snprintf(filepath, sizeof(filepath), "%s/%s-%d.log", LOG_DIR, role, pid_int);
    fd = open(filepath, O_WRONLY | O_CREAT | O_APPEND, 0644); //path: logs/<role>-<PID>.log
    if (fd < 0) {
        perror("open log file");
        return FILE_ERROR;
    }
    return OK;
}

void log_msg(log_level_t level, const char *msg) {
    if (fd < 0) return; //if log_msg called before log_init
    time_t t = time(NULL);
    struct tm *tmp = localtime(&t);
    char timestamp[50];
    char line[1024];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tmp);
    const char *lvl;
    switch (level) {
        case LOG_INFO:
            lvl = "INFO";
            break;
        case LOG_WARNING:
            lvl = "WARNING";
            break;
        case LOG_ERROR:
            lvl = "ERROR";
            break;
        default:
            lvl = "UNIDENTIFIED";
            break;
    }
    snprintf(line, sizeof(line), "[%s] [%s %d] %s: %s\n", timestamp, log_role, (int)pid, lvl, msg);
    if (write(fd, line, strlen(line)) == -1) {
        perror("write");
    }
}

void log_close(void) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}