CC = gcc
CFLAGS = -Wall -Wextra -pedantic -Iinclude

# moduli che servono a tutti e quattro gli eseguibili
COMMON = src/crypto.o src/block.o src/transaction.o src/ipc.o src/logging.o

# se cambia un header vanno ricompilati gli oggetti
HEADERS = include/errors.h include/config.h include/common.h \
          include/crypto.h include/block.h include/transaction.h \
          include/ipc.h include/logging.h include/cli.h

OBJS = $(COMMON) src/main.o src/cli.o src/node.o src/miner.o src/client.o
BINS = blockchain node miner client

# scenario di default, si cambia con: make run ARGS="1 1 1"
ARGS ?= 3 2 3

build: $(BINS)

blockchain: src/main.o src/cli.o $(COMMON)
	$(CC) $(CFLAGS) -o $@ $^

node: src/node.o $(COMMON)
	$(CC) $(CFLAGS) -o $@ $^

miner: src/miner.o $(COMMON)
	$(CC) $(CFLAGS) -o $@ $^

client: src/client.o $(COMMON)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

run: build
	./blockchain $(ARGS)

clean:
	rm -f $(OBJS) $(BINS)
	rm -rf logs fifo
	rm -f bootstrap.csv chain-node-*.csv