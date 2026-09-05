# OS Blockchain Simulation
**Status:** complete - No further development is planned.
![C](https://img.shields.io/badge/C-%2300599C.svg?style=for-the-badge&logo=c&logoColor=white)
![Bash](https://img.shields.io/badge/Bash-%23121011.svg?style=for-the-badge&logo=gnu-bash&logoColor=white)
![Ubuntu](https://img.shields.io/badge/Ubuntu_24.04-E95420?style=for-the-badge&logo=ubuntu&logoColor=white)

A multi-process blockchain simulation developed for the Operating Systems course (A.Y. 2025/2026) within the Bachelor's Degree in Information, Communications and Electronics Engineering at the University of Trento. Graded 10/10.

## Authors

- [Pietro Gelmini](https://github.com/Gelminaio)
- [Andrei Marius Smedoiu](https://github.com/smedoiuandrei)
- [Nicola Fait](https://github.com/NicolaFait05)

## Overview

The system simulates a blockchain network of three concurrent process types that share no memory and cooperate only by exchanging messages:

- **Nodes** keep a local copy of the chain, validate incoming blocks, gossip them to peers, and persist the chain to disk.
- **Miners** assemble candidate blocks from pending transactions and run a simulated proof-of-work loop, broadcasting mined blocks to all nodes.
- **Clients** generate transactions at a configurable frequency and submit them to miners.

A parent process bootstraps the network and hosts an interactive command line.

### Key design choices

- **IPC**: one named pipe (FIFO) per process for the data plane. Messages are fixed-size records kept under `PIPE_BUF`, so every write is atomic and arrives whole even with multiple senders.
- **Control flow**: POSIX signals for the control plane — `SIGUSR1` to abort mining, `SIGSTOP`/`SIGCONT` for pause/resume, `SIGTERM` for graceful shutdown.
- **Consensus**: a deterministic tiebreak on the block hash, which makes all nodes converge on the same chain without any coordinator.
- **Cryptography**: SHA-256 implemented from scratch (FIPS 180-4), so the project builds on a clean Ubuntu with no external libraries.

Full design rationale is in `report.pdf`.

## Repository structure

```
.
├── report.pdf              Design report (5 pages, project deliverable)
├── README.md
├── LICENSE
└── code/
    ├── Makefile            Build system: build, clean, run
    ├── blockchain.sh       Bash entry point: --verify, --hash, --merkle
    ├── include/            C headers (errors, config, common, crypto,
    │                       block, transaction, ipc, logging, cli)
    ├── src/                Shared modules and the four program entry
    │                       points (main, node, miner, client)
    ├── scripts/            Bash implementations behind blockchain.sh,
    │                       plus the shared error codes
    ├── tests/              Sample chain files used to exercise --verify
    ├── fifo/               Named pipes, created at startup
    └── logs/               Per-process runtime logs (role-PID.log)
```

## Build and run

Targets Ubuntu 24.04. Requires only `gcc` and `make` — no external libraries.

```bash
cd code
make build
```

This produces four executables: `blockchain` (bootstrap program and CLI host) and `node`, `miner`, `client` (the role-specific children).

### Running

```bash
make run                      # default scenario
make run ARGS="3 5 10 1 12"   # custom arguments
```

Do not run the system from a folder that an IDE has open and is indexing.
The FIFOs live in `code/fifo/`, and an indexer that opens them for reading
(the VS Code C/C++ extension does) consumes the messages before the miners
can read them: the miners then sit on an empty mempool and never mine, with
no error anywhere. Run it from a plain terminal, or from a copy outside the
workspace.

The arguments match the bootstrap program's signature:

```
./blockchain <num_nodes> <num_miners> <num_clients> \
             [transaction_frequency] [difficulty] [initial_state.csv]
```

Once running, the CLI accepts:

- `submit "Alice pays Bob 10 coins"` — submit a transaction
- `request blockchain [--index <i> | --hash <h>]` — query the chain
- `request block --index <i>` or `--hash <h>` — query a single block
- `save blockchain <filename>` — persist the chain to a CSV file
- `pause` / `resume` / `stop` — control all child processes

### Standalone utilities

```bash
./blockchain.sh --hash <block_hex>
./blockchain.sh --merkle "<tx1>::<tx2>::<tx3>"
./blockchain.sh --verify <state.csv>
```

### Clean up

```bash
make clean
```

Removes compiled artifacts, logs, saved chain files, and the `fifo/` directory with any leftover named pipes.

## License

See `LICENSE` for licensing terms.
