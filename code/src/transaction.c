//
// Created by faitn on 09/08/2026.
//
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

    if (regcomp(&re, PATTERN, REG_EXTENDED | REG_NOSUB) !=0) {
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

static void random_name(char *buf, size_t cap) {
    const char *alfabeto = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    size_t alfabeto_len = strlen(alfabeto);

    size_t len = 3+random()%6;

    if (len >= cap) {
        len = cap-1;
    }

    for (size_t i=0; i<len; i++) {
        buf[i] = alfabeto[random()%alfabeto_len];
    }

    buf[len] = '\0';
}

int transaction_generate_random(char *out, size_t cap) {
    static const char *nomi[] = {
        "Alice", "Bob", "Charlie", "Dave", "Eve", "Frank", "Grace", "Heidi", "Ivan", "Judy"
    };
    int n_nomi = sizeof(nomi)/sizeof(nomi[0]);
    int idx1 = random()%n_nomi;
    int idx2 = random()%n_nomi;

    if (idx1==idx2) {
        idx2 = (idx1+1)%n_nomi;
    }

    const char *mittente = nomi[idx1];
    const char *destinatario = nomi[idx2];

    int importo = 1+random()%999;

    int written = snprintf(out, cap,"%s pays %s %d coins", mittente, destinatario, importo);

    if (written<0 || (size_t)written>=cap) {
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


