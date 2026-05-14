# OS Blockchain Simulation

![C](https://img.shields.io/badge/C-%2300599C.svg?style=for-the-badge&logo=c&logoColor=white)
![Bash](https://img.shields.io/badge/Bash-%23121011.svg?style=for-the-badge&logo=gnu-bash&logoColor=white)
![Ubuntu](https://img.shields.io/badge/Ubuntu_24.04-E95420?style=for-the-badge&logo=ubuntu&logoColor=white)

A multi-process blockchain simulation developed for the Operating Systems course (A.Y. 2025/2026) within the Bachelor's Degree in Information, Communications and Electronics Engineering at the University of Trento.

## Authors

- [Pietro Gelmini](https://github.com/Gelminaio)
- [Andrei Marious Smedoiu](https://github.com/Andr3pny)
- [Nicola Fait](https://github.com/NicolaFait05)

## Overview

The system simulates a blockchain network composed of three concurrent process types that cooperate through inter-process communication:

- **Nodes** maintain a local copy of the chain, validate incoming blocks, gossip them to peers, and persist the chain to disk.
- **Miners** assemble candidate blocks from pending transactions and simulate a proof-of-work loop, broadcasting successfully mined blocks to all nodes.
- **Clients** generate transactions at a configurable frequency and submit them to miners.

The project explores the core challenges of UNIX systems programming: process orchestration, concurrent IPC, signal-based control flow, and consistency across independent processes that share no memory.

### Key design choices

- **Inter-process communication**: POSIX message queues for the data plane (transactions, blocks, gossip).
- **Control flow**: POSIX signals for the control plane (`SIGUSR1` to abort mining, `SIGSTOP`/`SIGCONT` for pause/resume, `SIGTERM` for graceful shutdown).
- **Consensus**: deterministic tiebreak rule on block hash to guarantee convergence across all nodes without coordination.
- **Cryptography**: SHA-256 via OpenSSL's `libcrypto`.

Detailed design rationale is provided in `report.pdf`.

## Repository structure

```
.
├── report.pdf              Design report (5 pages, project deliverable)
├── README.md
├── LICENSE
└── code/
    ├── Makefile            Build system: build, clean, run targets
    ├── blockchain.sh       Bash entry point: --verify, --hash, --merkle
    ├── include/            C public headers (errors, config, common,
    │                       crypto, block, transaction, ipc, logging, cli)
    ├── src/                C source files: shared modules and the four
    │                       executable entry points (main, node, miner, client)
    ├── scripts/            Bash implementations of the blockchain.sh
    │                       subcommands and shared error codes
    ├── bin/                Executables produced by `make build`
    └── logs/               Per-process runtime logs (role-PID.log)
```

## Build and run

The project targets Ubuntu 24.04 and requires `gcc`, `make`, `libcrypto` (OpenSSL), and POSIX message queue support (`librt`).

### Build

```bash
cd code
make build
```

This compiles the shared modules and produces four executables under `bin/`:

- `blockchain` — the bootstrapping program and CLI host
- `node`, `miner`, `client` — the role-specific child programs

### Run a default scenario

```bash
make run
```

Or with custom arguments:

```bash
make run ARGS="3 5 10 1 12"
```

The arguments match the bootstrapping program's signature:

```
./bin/blockchain <num_nodes> <num_miners> <num_clients> \
                 [transaction_frequency] [difficulty] [initial_state.csv]
```

Once the system is running, the CLI accepts the following commands:

- `submit "Alice pays Bob 10 coins"` — submit a transaction
- `request blockchain [--index <i> | --hash <h>]` — query the current chain
- `request block --index <i>` or `--hash <h>` — query a single block
- `save blockchain <filename>` — persist the chain to a CSV file
- `pause` / `resume` / `stop` — control the lifecycle of all child processes

### Standalone blockchain utilities

```bash
./blockchain.sh --hash <block_hex>
./blockchain.sh --merkle "<tx1>::<tx2>::<tx3>"
./blockchain.sh --verify <state.csv>
```

### Clean up

```bash
make clean
```

Removes all compiled artifacts, log files, and any leftover IPC resources (POSIX message queues under `/dev/mqueue/`).

## License

See `LICENSE` for licensing terms.