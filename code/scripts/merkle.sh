#!/bin/bash

source "$(dirname "$0")/errors.sh"

merkle_root_of() {
    local txs="$1"

    if [[ -z "$txs" ]]; then
        return "$INVALID_TRANSACTION"
    fi

    local resto="$txs"
    local lista=""

    while [[ "$resto" == *::* ]]; do
        local tx="${resto%%::*}"
        resto="${resto#*::}"
        local foglia=$(printf '%s' "$tx" | sha256sum | cut -d' ' -f1)
        if [[ -z "$lista" ]]; then
            lista="$foglia"
        else
            lista="${lista}"$'\n'"$foglia"
        fi
    done

    local foglia_finale=$(printf '%s' "$resto" | sha256sum | cut -d' ' -f1)
    if [[ -z "$lista" ]]; then
        lista="$foglia_finale"
    else
        lista="${lista}"$'\n'"$foglia_finale"
    fi

    while (( $(printf '%s\n' "$lista" | wc -l) > 1 )); do
        local nuova_lista=""
        local num_hashes=$(printf '%s\n' "$lista" | wc -l)
        if (( num_hashes % 2 != 0 )); then
            lista="${lista}"$'\n'"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
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

                if [[ -z "$nuova_lista" ]]; then
                    nuova_lista="$pair_hash"
                else
                    nuova_lista="${nuova_lista}"$'\n'"$pair_hash"
                fi
                h1=""
                h2=""
            fi
        done <<< "$lista"
        lista="$nuova_lista"
    done
    printf '%s\n' "$lista"
}
if [[ $# -gt 0 && -n "$1" ]]; then
    merkle_root_of "$1"
fi