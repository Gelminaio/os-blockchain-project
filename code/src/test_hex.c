#include <stdio.h>
#include <stdint.h>
#include "crypto.h"

int main() {
    char buffer[17];
    uint64_t numero;
    
    printf("--- TEST ANDATA ---\n");
    u64_to_hex16(4919, buffer);
    printf("Mi aspetto 0000000000001337. Ho ottenuto: %s\n", buffer);

    printf("\n--- TEST RITORNO ---\n");
    int esito1 = hex16_to_u64("0000000000001337", &numero);
    printf("Esito (0=OK): %d, Numero riconvertito: %llu\n", esito1, (unsigned long long)numero);

    printf("\n--- TEST STRINGA CON LETTERA SBAGLIATA ---\n");
    int esito2 = hex16_to_u64("12zz000000000000", &numero);
    printf("Esito mi aspetto errore (-1): %d\n", esito2);

    return 0;
}