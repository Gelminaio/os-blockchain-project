#ifndef CONFIG_H
#define CONFIG_H

#define MAX_NODES 16
#define MAX_MINERS 16
#define MAX_CLIENTS 16
#define MAX_TX_LEN 128 //max transaction length (127 + NUL (the terminator))
#define MAX_TX_PER_BLOCK 16 //max number of transactions per block (decided by us)
#define MSG_PAYLOAD_MAX 4064 // = 4096 - 32 bytes of msg_t header (4096 is the maximum limit where POSIX guarantees atomic write with FIFO)
#define MAX_SERIAL_LEN (HEX16_LEN * 3 + HASH_HEX_LEN * 2 + MAX_TX_PER_BLOCK * (MAX_TX_LEN - 1) + (MAX_TX_PER_BLOCK - 1) * 2 + 1) //max S(B) length, (16+16+64+64+16) + transactions separated by "::" and the terminator
#define MAX_CSV_ROW_LEN (MAX_SERIAL_LEN + 5) //csv line is S(B) more 5 commas that separate the 6 fields
#define FIFO_DIR "fifo"
#define FIFO_CAPACITY (1024 * 1024) //1 MiB = 256 messages of sizeof(msg_t); the kernel caps it at /proc/sys/fs/pipe-max-size
#define FIFO_CAPACITY_MIN 65536 //the Linux default, no point in going below it
#define MINE_SLEEP_MIN 1
#define MINE_SLEEP_MAX 5
#define DEFAULT_DIFFICULTY 12
#define DEFAULT_TX_FREQ 1
#define MAX_TX_FREQ 1000
#define REPLY_TIMEOUT_S 2
#define POLL_NAP_MS 50 //checks every 50ms
#define LOG_DIR "logs"
#define BOOTSTRAP_CSV "bootstrap.csv"
#define HASH_HEX_LEN 64
#define HEX16_LEN 16
#define MAX_PATH_LEN 256
#define CSV_HEADER "index,timestamp,prev_hash,merkle_root,nonce,transactions"

#endif