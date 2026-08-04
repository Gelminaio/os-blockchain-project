#ifndef CONFIG_H
#define CONFIG_H

#define MAX_NODES 16
#define MAX_MINERS 16
#define MAX_CLIENTS 16
#define MAX_TX_LEN 128 //max transaction length (127 + NUL (the terminator))
#define MAX_TX_PER_BLOCK 16 //max number of transactions per block (decided by us)
#define MSG_PAYLOAD_MAX 4064 // = 4096 - 32 bytes of msg_t header (4096 is the maximum limit where POSIX guarantees atomic write with FIFO)
#define FIFO_DIR "fifo"
#define MINE_SLEEP_MIN 1
#define MINE_SLEEP_MAX 5
#define DEFAULT_DIFFICULTY 12
#define DEFAULT_TX_FREQ 1
#define REPLY_TIMEOUT_S 2
#define POLL_NAP_MS 50 //checks every 50ms
#define LOG_DIR "logs"
#define BOOTSTRAP_CSV "bootstrap.csv"
#define HASH_HEX_LEN 64
#define HEX16_LEN 16

#endif