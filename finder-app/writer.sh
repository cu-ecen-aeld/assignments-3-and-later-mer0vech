#!/bin/sh

if [ "$#" -ne 2 ]; then
  printf "Error: wrong parameter number provided (2 required).\n"
  printf "Usage: %s <path> <string>\n" "$0"
  exit 1
fi

FILE_PATH_PARAM=$1
STR_PARAM=$2
PATH_PARAM=$(dirname "$FILE_PATH_PARAM")

if [ -z "$STR_PARAM" ]; then
  printf "Error: Given string cannot be empty!\n"
  exit 1
fi

if ! mkdir -p "$PATH_PARAM"; then
  printf "Error: Directory at given path does not exist and cannot be created!\n"
  exit 1
fi

if ! printf "%s\n" "$STR_PARAM" > "$FILE_PATH_PARAM"; then
  printf "Error: Unable to write to file!\n"
  exit 1
fi

