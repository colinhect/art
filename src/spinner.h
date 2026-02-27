#ifndef SPINNER_H
#define SPINNER_H

/* Pulsing terminal spinner written to stderr.
 *
 * A single background thread lives for the duration of a request sequence.
 * An "enabled" flag (mutex + condvar) controls whether frames are drawn.
 * spinner_write_chunk() holds the mutex while writing to stdout so the
 * thread never interleaves with output.  Cursor position is tracked with
 * ANSI save/restore so the spinner always lands where the next character
 * will appear, even mid-line.
 *
 * All functions are no-ops when the thread has not been started.
 */

/* Create the background thread (not yet drawing). */
void spinner_start(void);

/* Signal thread to exit and join it; clears any visible frame. */
void spinner_stop(void);

/* Save cursor and enable drawing — call just before each model request. */
void spinner_turn_start(void);

/* Disable drawing and clear frame — call just after each model request,
   before tool processing, so tool output to stderr is unobstructed. */
void spinner_turn_end(void);

/* Clear any visible frame, write text to stdout, save new cursor position,
   and re-enable drawing.  Use this in place of a bare fputs() for each
   streamed chunk so the spinner reappears between slow chunks. */
void spinner_write_chunk(const char* text);

#endif
