#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

/* strdup that returns NULL for NULL input (instead of crashing). */
char* xstrdup(const char* s);

/* Build a path under $HOME. Returns malloc'd string, or NULL if HOME unset. */
char* home_path(const char* suffix);

/* Read entire file into malloc'd buffer. Returns NULL on failure.
 * If out_len is non-NULL, receives the number of bytes read. */
char* read_file_contents(const char* path, size_t* out_len);

/* Free a NULL-terminated array of strings. */
void free_string_list(char** list);

#endif
