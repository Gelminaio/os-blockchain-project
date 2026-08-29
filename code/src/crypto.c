//sha-256 and merkle root, following the NIST FIPS 180-4 standard
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "crypto.h"
#include "config.h"
#include "errors.h"


//rotation function
static uint32_t rotr(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}
//the basic logic functions Ch and Maj
static uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}

static uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

//the 4 sigma functions: SIGMA only rotates, sigma prepares the words and ends with a shift
static uint32_t SIGMA0(uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

static uint32_t SIGMA1(uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

static uint32_t sigma0(uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

static uint32_t sigma1(uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

//turns a number into a hex string
void u64_to_hex16(uint64_t v, char out[17]) {
    snprintf(out, 17, "%016llx", (unsigned long long)v);
}

//from a string to a number
int hex16_to_u64(const char *in, uint64_t *out) {

    //check that the string is 16 long
    if (strlen(in) != 16) {
        return INVALID_BLOCK;
    }

    //check that the string only contains hex characters
    for (int i = 0; i < 16; i++) {
        if (!isxdigit(in[i])) {
            return INVALID_BLOCK;
        }
    }

    *out = (uint64_t)strtoull(in, NULL, 16);
    return OK;
}

//initial state values from FIPS 180-4
static const uint32_t H_INIT[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

//round constants from FIPS 180-4
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

//pads the message to a multiple of 64 bytes, then hashes it block by block
int sha256_hex(const void *data, size_t len, char out[65]) {
    uint32_t H[8];
    for (int i = 0; i < 8; i++) {
        H[i] = H_INIT[i];
    }

    uint64_t msg_len_bits = (uint64_t)len * 8; // how long the message was in bits
    size_t padded_len = len + 1 + 8; // 1 byte for the padding bit and 8 bytes for the message length

    //if it isn't a multiple of 64, stretch it to the next exact multiple
    if (padded_len % 64 != 0) {
        padded_len += 64 - (padded_len % 64);
    }

    //allocate padded_msg already filled with 0s
    uint8_t *padded_msg = (uint8_t *)calloc(padded_len, sizeof(uint8_t));
    if (padded_msg == NULL) {
        return SYS_ERROR; //if the pc has no RAM, error
    }

    //the original data goes at the start
    memcpy(padded_msg, data, len);

    padded_msg[len] = 0x80; //the padding bit, 0x80 is 10000000

    //the length in bits goes in the last 8 bytes
    for (int i = 0; i < 8; i++) {
        padded_msg[padded_len - 1 - i] = (uint8_t)(msg_len_bits >> (i * 8));
    }

    //one 64 byte block at a time
    for (size_t offset = 0; offset < padded_len; offset += 64) {

        uint32_t W[64]; // array of 64 words of 32 bits

        //the first 16 words come straight from the block
        for (int i = 0; i < 16; i++) {
            W[i] = ((((uint32_t)padded_msg[offset + i * 4])     << 24) |
                    (((uint32_t)padded_msg[offset + i * 4 + 1]) << 16) |
                    (((uint32_t)padded_msg[offset + i * 4 + 2]) <<  8) |
                     ((uint32_t)padded_msg[offset + i * 4 + 3]));
        }

        //the other 48 are generated with the sigma functions
        for (int i = 16; i < 64; i++) {
            W[i] = sigma1(W[i - 2]) + W[i - 7] + sigma0(W[i - 15]) + W[i - 16];
        }

        //the state starts from the current intermediate hash
        uint32_t a = H[0];
        uint32_t b = H[1];
        uint32_t c = H[2];
        uint32_t d = H[3];
        uint32_t e = H[4];
        uint32_t f = H[5];
        uint32_t g = H[6];
        uint32_t h = H[7];

        //compression loop
        for (int i = 0; i < 64; i++) {
            uint32_t T1 = h + SIGMA1(e) + ch(e, f, g) + K[i] + W[i];
            uint32_t T2 = SIGMA0(a) + maj(a, b, c);

            h = g;
            g = f;
            f = e;
            e = d + T1;
            d = c;
            c = b;
            b = a;
            a = T1 + T2;
        }

        //update the intermediate hash
        H[0] += a;
        H[1] += b;
        H[2] += c;
        H[3] += d;
        H[4] += e;
        H[5] += f;
        H[6] += g;
        H[7] += h;
    }

    //write the hash out as a hex string
    for (int i = 0; i < 8; i++) {
        snprintf(out + (i * 8), 9, "%08x", H[i]);
    }

    free(padded_msg);
    return OK;
}

//computes the Merkle root of a block of transactions
int merkle_root(const char *const *txs, size_t n, char out[65]) {
    //edge case: 0 transactions
    if (n == 0) {
        return INVALID_TRANSACTION;
    }

    //two levels, we swap them at the end of each round
    char (*level_a)[65] = malloc(n * sizeof(*level_a));
    char (*level_b)[65] = malloc(n * sizeof(*level_b));

    if (level_a == NULL || level_b == NULL) {
        free(level_a);
        free(level_b);
        return SYS_ERROR;
    }

    char(*read_level)[65] = level_a;
    char(*write_level)[65] = level_b;

    //first step: hash of the leaves
    for (size_t i = 0; i < n; i++) {
        int r = sha256_hex(txs[i], strlen(txs[i]), read_level[i]);
        if (r != OK) {
            free(level_a);
            free(level_b);
            return r;
        }
    }

    //odd number of nodes: the last one is paired with the empty hash
    char empty_hash[65];
    int r = sha256_hex("", 0, empty_hash);
    if (r != OK) {
        free(level_a);
        free(level_b);
        return r;
    }

    size_t current_count = n; // number of leaves

    while (current_count > 1) {
        size_t next_level = 0;

        for (size_t i = 0; i < current_count; i += 2) {

            char combined_buffer[129]; // two hashes joined + null terminator

            if (i + 1 < current_count) {

                snprintf(combined_buffer, sizeof(combined_buffer), "%s%s", read_level[i], read_level[i + 1]);

            } else {
                    snprintf(combined_buffer, sizeof(combined_buffer), "%s%s", read_level[i], empty_hash);

                }

            // combined hash for the next level
            r = sha256_hex(combined_buffer, 128, write_level[next_level]);
            if (r != OK) {
                free(level_a);
                free(level_b);
                return r;
            }
            next_level++;
        }


        current_count = next_level;
        char(*temp_ptr)[65] = read_level;
        read_level = write_level;
        write_level = temp_ptr;
    }

    strcpy(out, read_level[0]);

    free(level_a);
    free(level_b);
    return OK;
}


