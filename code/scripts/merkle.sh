#!/bin/bash
# merkle root of a "tx1::tx2::..." string, same rules as crypto.c

source "$(dirname "$0")/errors.sh"

merkle_root_of() {
    local txs="$1"

    if [[ -z "$txs" ]]; then
        return "$INVALID_TRANSACTION"
    fi

    local rest="$txs"
    local list=""

    while [[ "$rest" == *::* ]]; do
        local tx="${rest%%::*}"
        rest="${rest#*::}"
        local leaf=$(printf '%s' "$tx" | sha256sum | cut -d' ' -f1)
        if [[ -z "$list" ]]; then
            list="$leaf"
        else
            list="${list}"$'\n'"$leaf"
        fi
    done

    local last_leaf=$(printf '%s' "$rest" | sha256sum | cut -d' ' -f1)
    if [[ -z "$list" ]]; then
        list="$last_leaf"
    else
        list="${list}"$'\n'"$last_leaf"
    fi

    while (( $(printf '%s\n' "$list" | wc -l) > 1 )); do
        local new_list=""
        local num_hashes=$(printf '%s\n' "$list" | wc -l)
        if (( num_hashes % 2 != 0 )); then
            list="${list}"$'\n'"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
        fi

        local h1=""
        local h2=""

        while IFS= read -r h; do
            if [[ -z "$h" ]]; then continue; fi

            if [[ -z "$h1" ]]; then
                h1="$h"
            else
                h2="$h"
                local pair_hash=$(printf '%s%s' "$h1" "$h2" | sha256sum | cut -d' ' -f1)

                if [[ -z "$new_list" ]]; then
                    new_list="$pair_hash"
                else
                    new_list="${new_list}"$'\n'"$pair_hash"
                fi
                h1=""
                h2=""
            fi
        done <<< "$list"
        list="$new_list"
    done
    printf '%s\n' "$list"
}
if [[ $# -gt 0 && -n "$1" ]]; then
    merkle_root_of "$1"
fi