#ifndef SESSION_H
#define SESSION_H

/* Save session to ~/.artifice/sessions/YYYY-MM-DD-HHMMSS-uuuuuu.md
 * Returns path on success (caller frees), NULL on failure. */
char *session_save(const char *prompt, const char *system_prompt,
                   const char *model, const char *provider,
                   const char *response);

#endif
