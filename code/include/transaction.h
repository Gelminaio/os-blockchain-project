//transaction format: validation and random generation

#ifndef OS_BLOCKCHAIN_PROJECT_TRANSACTION_H
#define OS_BLOCKCHAIN_PROJECT_TRANSACTION_H

#include <stddef.h>

int transaction_init(void);
int transaction_is_valid(const char *s);
int transaction_generate_random(char *out, size_t cap);
void transaction_cleanup(void);


#endif //OS_BLOCKCHAIN_PROJECT_TRANSACTION_H
