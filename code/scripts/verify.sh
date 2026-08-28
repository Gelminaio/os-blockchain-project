#!/bin/bash
# checks a chain csv: fields, merkle roots, indexes and links

source "$(dirname "$0")/errors.sh"
source "$(dirname "$0")/merkle.sh"

file="$1"

[[ -f "$file" ]] || { echo "File not found" >&2; exit "$FILE_ERROR"; }
[[ -s "$file" ]] || { echo "Empty file" >&2; exit "$CSV_PARSE_ERROR"; }

{
    IFS= read -r header
    header="${header%$'\r'}"

    if [[ "$header" != "index,timestamp,prev_hash,merkle_root,nonce,transactions" ]]; then
        echo "Malformed CSV header" >&2
        exit "$CSV_PARSE_ERROR"
    fi

    line_no=1
    blocks=0
    first_error=0
    prev_computed=""
    prev_idx_dec=-1

    while IFS=',' read -r idx ts prev mroot nonce txs; do
        [[ -z "$idx" ]] && continue
        txs="${txs%$'\r'}"
        (( line_no++ ))
        (( blocks++ ))

        if  [[ ! "$idx" =~ ^[0-9a-f]{16}$ ]] || \
            [[ ! "$ts" =~ ^[0-9a-f]{16}$ ]] || \
            [[ ! "$nonce" =~ ^[0-9a-f]{16}$ ]] || \
            [[ ! "$prev" =~ ^[0-9a-f]{64}$ ]] || \
            [[ ! "$mroot" =~ ^[0-9a-f]{64}$ ]]; then
            # the row cannot be read, no point in checking the rest of it
            echo "line $line_no: malformed field" >&2
            exit "$CSV_PARSE_ERROR"
        fi

        # the merkle root
        expected=$(merkle_root_of "$txs")
        if [[ "$expected" != "$mroot" ]]; then
            echo "line $line_no: invalid merkle root" >&2
            [[ $first_error -eq 0 ]] && first_error="$INVALID_BLOCK"
        fi

        # the indexes
        if [[ $blocks -eq 1 ]]; then
            if [[ "$idx" != "0000000000000000" ]]; then
                echo "line $line_no: wrong index" >&2
                [[ $first_error -eq 0 ]] && first_error="$CHAIN_MISMATCH"
            fi
            prev_idx_dec=0
        else
            idx_dec=$((16#$idx))
            if [[ $idx_dec -ne $((prev_idx_dec + 1)) ]]; then
                echo "line $line_no: wrong index" >&2
                [[ $first_error -eq 0 ]] && first_error="$CHAIN_MISMATCH"
            fi
            prev_idx_dec=$idx_dec
        fi

        # the link to the previous block
        if [[ $blocks -gt 1 ]]; then
            if [[ "$prev" != "$prev_computed" ]]; then
                echo "line $line_no: broken link" >&2
                [[ $first_error -eq 0 ]] && first_error="$CHAIN_MISMATCH"
            fi
        fi

        prev_computed=$(printf '%s%s%s%s%s%s' "$idx" "$ts" "$prev" "$mroot" "$nonce" "$txs" | sha256sum | cut -d' ' -f1 )

    done

    # exit
    if [[ $blocks -eq 0 ]]; then
        echo "OK: empty chain (0 blocks)"
        exit "$OK"
    fi

    if [[ $first_error -ne 0 ]]; then
        exit "$first_error"
    fi

    echo "OK: $blocks blocks"
    exit "$OK"

} < "$file"
