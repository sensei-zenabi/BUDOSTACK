#ifndef INPUT_H
#define INPUT_H

/* Reads a line from stdin with TAB autocompletion support.
   The returned string is dynamically allocated (caller must free it). */
char* read_input(void);

#endif // INPUT_H
