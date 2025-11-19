#ifndef INPUT_H
#define INPUT_H

/* Reads a line from stdin with TAB autocompletion support and inline editing.
   The provided prompt is rendered on every refresh. Returned string is
   dynamically allocated (caller must free it). */
char* read_input(const char *prompt);

#endif // INPUT_H
