//transaction format: validation with a regex and random generation
#include "transaction.h"
#include "errors.h"
#include <string.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include "config.h"

#define PATTERN "^[A-Za-z0-9]+ pays [A-Za-z0-9]+ [1-9][0-9]* coins$"

static regex_t re;
static int initialized = 0;

int transaction_init(void) {
    if (initialized) {
        return OK;
    }

    if (regcomp(&re, PATTERN, REG_EXTENDED | REG_NOSUB) != 0) {
        return SYS_ERROR;
    }
    initialized = 1;
    return OK;
}

int transaction_is_valid(const char *s) {
    if (!initialized) {
        return SYS_ERROR;
    }

    if (strlen(s) >= MAX_TX_LEN) {
        return INVALID_TRANSACTION;
    }

    if (regexec(&re, s, 0, NULL, 0) == 0) {
        return OK;
    }

    return INVALID_TRANSACTION;
}

int transaction_generate_random(char *out, size_t cap) {
    static const char *names[] = {
        "Alice", "Bob", "Charlie", "Dave", "Eve", "Frank", "Grace", "Heidi", "Ivan", "Judy"
    };
    int n_names = sizeof(names) / sizeof(names[0]);
    int idx1 = random() % n_names;
    int idx2 = random() % n_names;

    if (idx1 == idx2) {
        idx2 = (idx1 + 1) % n_names;
    }

    const char *sender = names[idx1];
    const char *receiver = names[idx2];

    int amount = 1 + random() % 999;

    int written = snprintf(out, cap, "%s pays %s %d coins", sender, receiver, amount);

    if (written < 0 || (size_t)written >= cap) {
        return INVALID_TRANSACTION;
    }

    return OK;
}

void transaction_cleanup(void) {
    if (initialized) {
        regfree(&re);
        initialized = 0;
    }
}


