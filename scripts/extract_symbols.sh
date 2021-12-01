#!/bin/bash

#---------------------------------------------------------------------
# This script extracts exported symbols from a specified shared library
# or list of libraries advertised by OSv dynamic linker and intersects
# them against the symbols exported by OSv kernel.
#
# It is intended to be used in two ways:
# 1) Extract and intersect common exported symbols AND save them under
#    ./exported_symbols/$ARCH.
# 2) Extract and intersect common exported symbols AND verify if any
#    symbols are missing or extra when comparing to the contents of
#    files found in ./exported_symbols/$ARCH which would have been
#    generated by above.
#---------------------------------------------------------------------

argv0=${0##*/}
usage() {
cat <<-EOF
Extract exported symbols from a specified shared library
or list of libraries advertised by OSv dynamic linker and intersect
them against the symbols exported by OSv kernel.

Usage: ${argv0} [--verify] | <library path or name> [--verify]
EOF
exit ${1:-0}
}

MACHINE=$(uname -m)
if [ "${MACHINE}" == "x86_64" ]; then
  ARCH="x64"
else
  ARCH="aarch64"
fi

extract_symbols_from_elf()
{
  local ELF_PATH=$1
  nm -C --dynamic --defined-only --extern-only ${ELF_PATH} | grep -vP '\d{16,16} A' | grep -v GLIBC_PRIVATE | cut -c 20- | cut --delimiter=@ -f 1 | sort | uniq
}

extract_and_output_symbols_from_elf()
{
  local ELF_PATH=$1
  local ELF_SYMBOLS_OUTPUT=$2
  local SYMBOLS_FILTER=$3

  echo "Extracting exported symbols from $ELF_PATH .."
  if [ "$SYMBOLS_FILTER" != "" ]; then
    extract_symbols_from_elf ${ELF_PATH} | grep -v "$SYMBOLS_FILTER" >${ELF_SYMBOLS_OUTPUT}
  else
    extract_symbols_from_elf ${ELF_PATH} >${ELF_SYMBOLS_OUTPUT}
  fi
}

THIS_SCRIPT_PATH="$(readlink -f "$0")"
OSV_HOME=$(readlink -f $(dirname "$THIS_SCRIPT_PATH")/..)
OSV_KERNEL_SYMBOLS_FILE="/tmp/_osv_kernel.symbols"

extract_lib_symbols_in_osv_kernel()
{
  local LIB_NAME_OR_PATH=$1
  local SYMBOLS_FILTER=$2

  local LIB_NAME=$(basename $LIB_NAME_OR_PATH)
  local LIB_PATH=$(gcc -print-file-name=$LIB_NAME_OR_PATH)
  local ALL_SYMBOLS_OUT="/tmp/_${LIB_NAME}_all.symbols"
  local OSV_SYMBOLS_FILE="$OSV_HOME/exported_symbols/$ARCH/osv_${LIB_NAME}.symbols"
  local OSV_SYMBOLS_OUT="$TARGET_DIR/osv_${LIB_NAME}.symbols"

  echo "--- Library $LIB_NAME ---"
  extract_and_output_symbols_from_elf $LIB_PATH "$ALL_SYMBOLS_OUT" "$SYMBOLS_FILTER"
  comm -12 $OSV_KERNEL_SYMBOLS_FILE "$ALL_SYMBOLS_OUT" > $OSV_SYMBOLS_OUT
  echo "Extracted the symbols found in OSv kernel to $OSV_SYMBOLS_OUT !"

  if [[ $VERIFY == true ]]; then
    local MISSING_SYMBOLS_FILE="/tmp/osv_${LIB_NAME}.symbols.missing"
    local EXTRA_SYMBOLS_FILE="/tmp/osv_${LIB_NAME}.symbols.extra"

    comm -23 $OSV_SYMBOLS_FILE "$OSV_SYMBOLS_OUT" > $MISSING_SYMBOLS_FILE
    if [[ "$(cat $MISSING_SYMBOLS_FILE)" != "" ]]; then
      echo "!!! Found missing symbols:"
      cat $MISSING_SYMBOLS_FILE
    fi

    comm -13 $OSV_SYMBOLS_FILE "$OSV_SYMBOLS_OUT" > $EXTRA_SYMBOLS_FILE
    if [[ "$(cat $EXTRA_SYMBOLS_FILE)" != "" ]]; then
      echo "!!! Found extra symbols:"
      cat $EXTRA_SYMBOLS_FILE
    fi
  fi
}

if [[ "$1" == '--help' || "$#" > 2 ]]; then
  usage
fi

VERIFY=false
TARGET_DIR="$OSV_HOME/exported_symbols/$ARCH"

if [[ "$1" == '--verify' || "$2" == '--verify' ]]; then
  VERIFY=true
  TARGET_DIR="/tmp/exported_symbols/$ARCH"
fi

mkdir -p $TARGET_DIR
extract_and_output_symbols_from_elf $OSV_HOME/build/release.$ARCH/kernel-stripped.elf $OSV_KERNEL_SYMBOLS_FILE

if [[ "$1" != "" && "$1" != '--verify' ]]; then
  LIB_PATH="$1"
  extract_lib_symbols_in_osv_kernel $LIB_PATH "^mtx_\|^xdr"
else
  extract_lib_symbols_in_osv_kernel "libresolv.so.2"
  extract_lib_symbols_in_osv_kernel "libc.so.6" "^xdr"
  extract_lib_symbols_in_osv_kernel "libm.so.6"
  if [ "$ARCH" == "x64" ]; then
    extract_lib_symbols_in_osv_kernel "ld-linux-x86-64.so.2"
  else
    extract_lib_symbols_in_osv_kernel "ld-linux-aarch64.so.1"
  fi
  extract_lib_symbols_in_osv_kernel "libpthread.so.0" "^mtx_"
  extract_lib_symbols_in_osv_kernel "libdl.so.2"
  extract_lib_symbols_in_osv_kernel "librt.so.1"
  extract_lib_symbols_in_osv_kernel "libcrypt.so.1"
  extract_lib_symbols_in_osv_kernel "libaio.so.1"
  extract_lib_symbols_in_osv_kernel "libxenstore.so.3.0"
  extract_lib_symbols_in_osv_kernel "libutil.so"
fi