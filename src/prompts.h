#ifndef PROMPTS_H
#define PROMPTS_H

/* List all .md files in ~/.artifice/prompts/ and ./.artifice/prompts/.
 * Returns NULL-terminated array of names (without .md extension).
 * Caller must free each string and the array itself. */
char **list_prompts(void);

/* Load prompt content by name. Returns malloc'd string or NULL. */
char *load_prompt(const char *name);

#endif
