#!/bin/sh

if [ "$#" -ne 2 ]; then
  printf "Error: wrong parameter number provided (2 required).\n"
  printf "Usage: %s <path> <string>\n" "$0"
  exit 1
fi

PATH_PARAM=$1
STR_PARAM=$2

if [ ! -e "$PATH_PARAM" ]; then
  printf "Error: Path '%s' does not exist.\n" "$PATH_PARAM"
  exit 1
fi

if [ -z "$STR_PARAM" ]; then
  printf "Error: Search string cannot be empty.\n"
  exit 1
fi

FILES_COUNT=$(grep -rl "$STR_PARAM" "$PATH_PARAM" | wc -l)
LINES_COUNT=$(grep -r "$STR_PARAM" "$PATH_PARAM" | wc -l)

printf "The number of files are %d " "$FILES_COUNT"
printf "and the number of matching lines are %d.\n" "$LINES_COUNT"


