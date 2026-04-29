# ⛓️ OS Blockchain Simulation

![C](https://img.shields.io/badge/c-%2300599C.svg?style=for-the-badge&logo=c&logoColor=white)
![Bash](https://img.shields.io/badge/bash-%23121011.svg?style=for-the-badge&logo=gnu-bash&logoColor=white)
![Ubuntu](https://img.shields.io/badge/Ubuntu-E95420?style=for-the-badge&logo=ubuntu&logoColor=white)

> A multi-process blockchain simulation developed for the Operating Systems course (A.Y. 2025/2026) within the Bachelor's Degree in Information, Communications and Electronics Engineering at the University of Trento.

## 👥 The Team
* [Pietro Gelmini](https://github.com/Gelminaio)
* [Andrei Marious Smedoiu](https://github.com/Andr3pny)
* [Nicola Fait](https://github.com/NicolaFait05)

## 🚀 Overview
This project simulates a fully functional blockchain system using distinct OS processes. It features:
* **Nodes**: Maintain the shared ledger and validate blocks.
* **Miners**: Compete to solve a simulated proof-of-work puzzle.
* **Clients**: Concurrently submit coin transfer transactions.

The core challenge involves managing **concurrency**, **Inter-Process Communication (IPC)**, and **state synchronization** across multiple independent processes in a UNIX-like environment.

## 📂 Repository Structure
* `/src` - C source files for Bootstrapper, Nodes, Miners, and Clients.
* `/include` - C header files.
* `/scripts` - Bash scripts for blockchain validation (`blockchain.sh`) and build automation (`build.sh`).
* `/docs` - Project design report.

## 🛠️ Build & Run
*(Detailed instructions will be added as development progresses)*

To compile the project using the provided Makefile:
```bash
make build
