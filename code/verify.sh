#!/bin/bash

source scripts/errors.sh
source scripts/merkle.sh ""

file="$1"

[[ -f "$file" ]] || { echo "File non trovato" >&2; exit "$FILE_ERROR"; }
[[ -s "$file" ]] || { echo "File vuoto" >&2; exit "$CSV_PARSE_ERROR"; }

{
    IFS= read -r header
    header="${header%$'\r'}"

    if [[ "$header" != "index,timestamp,prev_hash,merkle_root,nonce,transactions" ]]; then
        echo "Intestazione CSV malformata" >&2
        exit "$CSV_PARSE_ERROR"
    fi

    riga=1
    blocchi=0
    primo_errore=0
    hash_precedente=""
    idx_dec_precedente=-1

    while IFS=',' read -r idx ts prev mroot nonce txs; do
        [[ -z "$idx" ]] && continue
        txs="${txs%$'\r'}"
        (( riga++ ))
        (( blocchi++ ))

        if  [[ ! "$idx" =~ ^[0-9a-f]{16}$ ]] || \
            [[ ! "$ts" =~ ^[0-9a-f]{16}$ ]] || \
            [[ ! "$nonce" =~ ^[0-9a-f]{16}$ ]] || \
            [[ ! "$prev" =~ ^[0-9a-f]{64}$ ]] || \
            [[ ! "$mroot" =~ ^[0-9a-f]{64}$ ]]; then
            echo "riga $riga: campo malformato" >&2
            [[ $primo_errore -eq 0 ]] && primo_errore="$INVALID_BLOCK"

        fi

        # la radice di merkle
        atteso=$(merkle_root_of "$txs")
        if [[ "$atteso" != $mroot ]]; then
            echo "riga $riga: merkle root non valida" >&2
            [[ $primo_errore -eq 0 ]] && primo_errore="$INVALID_BLOCK"
        fi

        # gli indici
        if [[ $blocchi -eq 1 ]]; then
            if [[ "$idx" != "0000000000000000" ]]; then
                echo "riga $riga: indice errato" >&2
                [[ $primo_errore -eq 0 ]] && primo_errore="$INVALID_BLOCK"
            fi
            idx_dec_precedente=0
        else
            idx_dec=$((16#$idx))
            if [[ $idx_dec -ne $((idx_dec_precedente + 1)) ]]; then
            echo "riga $riga: indice errato" >&2
            [[ $primo_errore -eq 0 ]] && primo_errore="$INVALID_BLOCK"
            fi
            idx_dec_precedente=$idx_dec
        fi

        # il collegamento
        if [[ $blocchi -gt 1 ]]; then
            if [[ "$prev" != "$hash_precedente" ]]; then
            echo "riga $riga: collegamento rotto" >&2
            [[ $primo_errore -eq 0 ]] && primo_errore="$CHAIN_MISMATCH"
            fi
        fi

        hash_precedente=$(printf '%s%s%s%s%s%s' "$idx" "$ts" "$prev" "$mroot" "$nonce" "$txs" | sha256sum | cut -d' ' -f1 )

    done

    # uscita
    if [[ $blocchi -eq 0 ]]; then
        echo "OK: empty chain (0 blocks)"
        exit "$OK"
    fi

    if [[ $primo_errore -ne 0 ]]; then
        exit "$primo_errore"
    fi

    echo "OK: $blocchi blocks"
    exit "$OK"

} < "$file"



