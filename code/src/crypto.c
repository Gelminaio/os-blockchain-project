#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "crypto.h"
#include "config.h"
#include "errors.h"

/* =========================================================================
    
    >SHA-256 IMPLEMENTAZIONE<

    BASATO SULLO STANDARD FIPS 180-4 DEL NIST

==========================================================================*/

/*funzione rotazione*/
static uint32_t rotr(uint32_t x, int n){
    return (x >> n) | (x << (32 - n));
}
/*funzioni logiche base Ch e Maj */
static uint32_t ch(uint32_t x, uint32_t y, uint32_t z){
    return (x & y) ^ (~x & z);
}

static uint32_t maj(uint32_t x, uint32_t y, uint32_t z){
    return (x & y) ^ (x & z) ^ (y & z);
}

/* 4 funzioni sigma
   SIGMA--> servono nella compressione che usano la rotazione
   sigma--> servono nella preparazione delle parole e finiscono con uno scorrimento
*/
static uint32_t SIGMA0_maj(uint32_t x){
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

static uint32_t SIGMA1_maj(uint32_t x){
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

static uint32_t sigma0_maj(uint32_t x){
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

static uint32_t sigma1_maj(uint32_t x){
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

/*Trasforma un numero in una stringa esadecimale*/
void u64_to_hex16(uint64_t v, char *out[17]){
     snprintf(out,17,"%016llx",(unsigned long long)v);
}

/*Da una stringa ad un numero*/
int hex16_to_u64(const char *in, uint64_t *out){
   
    // CONTROLLA CHE LA STRINGA SIA LUNGA 16 
    if(strlen(in) != 16){
        return INVALID_BLOCK;
    }
    
    // CONTROLLA CHE LA STRINGA SIA COMPOSTA SOLO DA CARATTERI ESADICIMALI
    for(int i = 0; i < 16; i++){

        if(!isxdigit(in[i])){
            return INVALID_BLOCK;
        }

    }
    //CONVERTO LA STRINGA E SALVO IN OUT
    *out = (uint64_t)strtoull(in, NULL, 16);
    return OK;
}

/*Valori iniziali dello stato da FIPS 180-4 */
static const uint32_t H_INIT[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

/*Costanti di round da FIPS 180-4 */
static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};


/* ========================================================================
    PADDING E PREPAZIONE DEL MESSAGGIO PER OTTENERE 512 BIT (64 BYTE) 
 ==========================================================================*/

/*calcola l'hash SHA-256 di qualsiasi dato*/
int sha256_hex(const void *data, size_t len, char out[65]){
   
    uint32_t H[8];
    for(int i = 0; i < 8; i++){
        H[i] = H_INIT[i];
    }

    uint64_t msg_len_bits = (uint64_t)len * 8; // calcolo quanto era lungo il messggio in bit
    size_t padded_len = len + 1 + 8; // aggiungo 1 byte per il bit di padding e 8 byte per la lunghezza del messaggio

    //Se non è multiplo di 64, lo allungo fino al prossimo multiplo esatto
    if (padded_len % 64 != 0) {
        padded_len += 64 - (padded_len % 64);
    }

    // creo padden_msg e  alloclo la memoria aggiungendo gli 0
    uint8_t *padded_msg = (uint8_t *)calloc(padded_len, sizeof(uint8_t));
    if(padded_msg == NULL){
        return INVALID_BLOCK; //se il pc non ha RAM, errore
    }

    // mettto i dati originali all'inizio del nuvo array
    memcpy(padded_msg, data, len);

    // aggiungo il bit di padding 
    padded_msg[len] = 0x80; // 0x80 è 10000000

    uint32_t W[64]; // array di 64 parole da 32 bit

    // mettoo la lunghezza in bit negli ultimi 8 byte alla fine
    for (int i = 0; i < 8; i++) {
        padded_msg[padded_len - 1 - i] = (uint8_t)(msg_len_bits >> (i * 8));
    }

    for(int i = 0; i < 16; i++){
        // copio i primi 16 blocchi di 32 bit dal messaggio
        W[i] = ((uint32_t)(padded_msg[i * 4] << 24) | (uint32_t)(padded_msg[i * 4 + 1] << 16) | (uint32_t)(padded_msg[i * 4 + 2] << 8) | (uint32_t)(padded_msg[i * 4 + 3]));
    }

    /* GEMINI CONSIGLIA COSì CON L'OFFSET, MA NON SONO D'ACCORDO
    
    IL CAPOTRENO (OFFSET) CHE SCORRE I VAGONI DA 64 BYTE
    for (size_t offset = 0; offset < padded_len; offset += 64) {
        uint32_t W[64]; 

        for(int i = 0; i < 16; i++){
            W[i] = ((uint32_t)(padded_msg[offset + i * 4]     << 24) | 
                    (uint32_t)(padded_msg[offset + i * 4 + 1] << 16) | 
                    (uint32_t)(padded_msg[offset + i * 4 + 2] << 8)  | 
                    (uint32_t)(padded_msg[offset + i * 4 + 3]));
        }
    */

    // Genero le 48 parole rimanenti usando le funzioni sigma
    for(int i = 16; i < 64; i++){
        W[i] = sigma1_maj(W[i - 2]) + W[i - 7] + sigma0_maj(W[i - 15]) + W[i - 16];
    }
    
    // inizializzo le variabili di stato con i valori iniziali
    uint32_t a = H[0];
    uint32_t b = H[1];
    uint32_t c = H[2];
    uint32_t d = H[3];
    uint32_t e = H[4];
    uint32_t f = H[5];
    uint32_t g = H[6];
    uint32_t h = H[7];

    // CICLO DI COMPRESSIONE
    for(int i = 0; i < 64; i++){
        uint32_t T1 = h + SIGMA1_maj(e) + ch(e, f, g) + K[i] + W[i];
        uint32_t T2 = SIGMA0_maj(a) + maj(a, b, c);

        // AGGIORNAMENTO DELLE VARIABILI DI STATO
        h = g;
        g = f;
        f = e;
        e = d + T1;
        d = c;
        c = b;
        b = a;
        a = T1 + T2;

        // AGGIORNAMENTO HASH INTERMEDIO
    

    }
    H[0] += a;
    H[1] += b;
    H[2] += c;
    H[3] += d;
    H[4] += e;
    H[5] += f;
    H[6] += g;
    H[7] += h;
     /* ========================================================================
         CONVERSIONE DELL'HASH IN STRINGA ESADECIMALE
      ==========================================================================
     */ 
        
     for(int i = 0; i < 8; i++){
        snprintf(out + (i * 8), 9, "%08x", H[i]);
    }

    free(padded_msg); // libero la memoria allocata per il messaggio
     return OK; // ritorno OK
    
}
    

/*calcola la radice di Merkle di un blocco di transizioni*/
int merkle_root(const char *const *txs, size_t n, char out[65]){

    // CASO LIMITE 0 TRANSAZIONI
    if(n == 0){
        return sha256_hex("", 0, out);
   
    }

    // ALLOCAZIONE DINAMICA MEMORIA --> MALLOC
    char (*Livello_read)[65] = malloc(n * sizeof(*Livello_read));
    char (*Livello_write)[65] = malloc(n * sizeof(*Livello_write));

    // CONTROLLO SE IL PC HA RAMM, SENNò ESCO CON ERRORE
    if(Livello_read == NULL || Livello_write == NULL){
        free(Livello_read);
        free(Livello_write);
        return INVALID_BLOCK;
    }
    
    // CREAZIONE DUE PUNTATORI PER SCAMBIARE I TAVOLI ALLA FINE DI OGNI LIVELLO
    char(*tavolo_lettura)[65] = Livello_read;
    char(*tavolo_scrittura)[65] = Livello_write;

    // FASE INIZIALE: CALCOLO HASH DELLE FOGLIE
    for(size_t i = 0; i < n; i++){
        sha256_hex(txs[i], strlen(txs[i]), tavolo_lettura[i]);
    }

    // CASO DIPARI: PREPARO STRINGA VUOTA
    char hash_vuoto[65];
    sha256_hex("", 0, hash_vuoto);

    size_t livello_corrente = n; // numero di foglie

    while(livello_corrente > 1){
        size_t next_level = 0;

        for(size_t i = 0; i < livello_corrente; i += 2){
            
            char combined_buffer[129]; // due hash concatenati + terminatore nullo
            
            if(i + 1 < livello_corrente){
            
                sprintf(combined_buffer, "%s%s", tavolo_lettura[i], tavolo_lettura[i + 1]);

            }else{
                    sprintf(combined_buffer, "%s%s", tavolo_lettura[i], hash_vuoto);

                }

            // calcolo hash combinato per il livello succesivo
            sha256_hex(combined_buffer, 128, tavolo_scrittura[next_level]);
            next_level++;
        }
            

        livello_corrente = next_level;
        char(*temp_ptr)[65] = tavolo_lettura;
        tavolo_lettura = tavolo_scrittura;
        tavolo_scrittura = temp_ptr;

    }

     strcpy(out, tavolo_lettura[0]); // copio la radice di Merkle nell'output
    
    //deallocazione risorse dianamiche
    free(Livello_read);
    free(Livello_write);
    return OK; // ritorno OK

}


    