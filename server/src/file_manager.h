#pragma once

#include <unistd.h>
#include <string.h>

#define IO_FILE "/var/tmp/aesdsocketdata"

/*
* Writes line to a predefined file
* args: line to write (const char *)
*/
void append_line_to_file(const char *data);

/*
* Reads data from a file
* args: size of output (size_t *)
*/
char *read_file_to_buffer(size_t *out_size);
