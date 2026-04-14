#!/bin/sh
set -eu

usage() {
    echo "usage: ./tcc <src> -o <out>" >&2
    exit 1
}

if [ "$#" -ne 3 ] || [ "$2" != "-o" ]; then
    usage
fi

ROOT=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
SRC=$1
OUT=$3

case "$SRC" in
    /*) ABS_SRC=$SRC ;;
    *) ABS_SRC=$PWD/$SRC ;;
esac

case "$OUT" in
    /*) ABS_OUT=$OUT ;;
    *) ABS_OUT=$PWD/$OUT ;;
esac

if [ ! -f "$ABS_SRC" ]; then
    echo "tcc: source not found: $SRC" >&2
    exit 1
fi

mkdir -p "$(dirname "$ABS_OUT")"

exec make -C "$ROOT/tiny_c_compiler" SRC="$ABS_SRC" APP_ELF_OUT="$ABS_OUT" export-elf
