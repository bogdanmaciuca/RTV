#!/bin/sh

if ! command -v pyftsubset >/dev/null 2>&1; then
    echo "must install fonttools: run 'pip install fonttools'"
    exit 1
fi

filename=$(basename "$1")
base="${filename%.*}"
ext="${filename##*.}"
mkdir -p ./res

pyftsubset "$1" \
    --unicodes="U+0020-007E" \
    --layout-features="" \
    --output-file="./res/${base}-ASCII.${ext}"
