#ifndef LOGGING_H
#define LOGGING_H

typedef enum { LOG_INFO, LOG_WARNING, LOG_ERROR } log_level_t;

int log_init(const char *role);
void log_msg(log_level_t level, const char *msg);
void log_close(void);

#endif