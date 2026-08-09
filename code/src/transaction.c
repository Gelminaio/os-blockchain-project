//
// Created by faitn on 09/08/2026.
//
#include "../include/transaction.h"
#include "../include/errors.h"
#include <string.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include "../include/config.h"

#define PATTERN "^[A-Za-z0-9]+ pays [A-Za-z0-9]+ [1-9][0-9]* coins$"

static regex_t re;
static int initialized = 0;

int transaction_init(void) {
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
    char mittente[16];
    char destinatario[16];

    random_name(mittente, sizeof(mittente));
    random_name(destinatario, sizeof(destinatario));

    int importo = 1+random()%999;

    int written = snprintf(out, cap,"%s pays %s %d coins", mittente, destinatario, importo);

    if (written<0 || (size_t)written>=cap) {
        return INVALID_TRANSACTION;
    }
}

void transaction_cleanup(void) {
    if (initialized) {
        regfree(&re);
        initialized = 0;
    }
}


