CC = gcc
CFLAGS = -Wall -Wextra -pedantic -Iinclude

# modules required by all four executables
COMMON = src/crypto.o src/block.o src/transaction.o src/ipc.o src/logging.o

# if a header changes, the objects must be recompiled
HEADERS = include/errors.h include/config.h include/common.h \
          include/crypto.h include/block.h include/transaction.h \
          include/ipc.h include/logging.h include/cli.h

OBJS = $(COMMON) src/main.o src/cli.o src/node.o src/miner.o src/client.o
BINS = blockchain node miner client

# default values, can be changed with: make run ARGS="1 1 1"
ARGS ?= 3 2 3 # ?= means assign only if not already defined

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