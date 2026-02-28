#ifndef COPILOT_H
#define COPILOT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handles */
typedef struct copilot_client copilot_client_t;
typedef struct copilot_session copilot_session_t;

/* Event types matching copilot::SessionEventType */
typedef enum {
	COPILOT_EVENT_SESSION_START,
	COPILOT_EVENT_SESSION_RESUME,
	COPILOT_EVENT_SESSION_ERROR,
	COPILOT_EVENT_SESSION_IDLE,
	COPILOT_EVENT_ASSISTANT_MESSAGE,
	COPILOT_EVENT_ASSISTANT_MESSAGE_DELTA,
	COPILOT_EVENT_TOOL_EXECUTION_START,
	COPILOT_EVENT_TOOL_EXECUTION_COMPLETE,
	COPILOT_EVENT_UNKNOWN
} copilot_event_type;

/* Event callback */
typedef void (*copilot_event_cb)(copilot_event_type type,
                                 const char *data_json,
                                 void *userdata);

/* Tool callback: receives tool name + args JSON, returns result JSON (caller frees) */
typedef char *(*copilot_tool_cb)(const char *name,
                                 const char *args_json,
                                 void *userdata);

/* Model info (returned from copilot_list_models) */
typedef struct {
	char *id;
	char *name;
} copilot_model_info;

/* Client lifecycle */
copilot_client_t *copilot_client_create(void);
int               copilot_client_start(copilot_client_t *c);
int               copilot_client_stop(copilot_client_t *c);
void              copilot_client_destroy(copilot_client_t *c);

/* Session lifecycle */
copilot_session_t *copilot_session_create(copilot_client_t *c,
                                          const char *model,
                                          const char *system_prompt);
int                copilot_session_send(copilot_session_t *s,
                                        const char *message,
                                        copilot_event_cb on_event,
                                        void *userdata);
int                copilot_session_send_and_wait(copilot_session_t *s,
                                                 const char *message,
                                                 copilot_event_cb on_event,
                                                 void *userdata,
                                                 int timeout_secs);
void               copilot_session_destroy(copilot_session_t *s);

/* Tool registration */
int copilot_session_register_tool(copilot_session_t *s,
                                  const char *name,
                                  const char *description,
                                  const char *params_schema_json,
                                  copilot_tool_cb handler,
                                  void *userdata);

/* Model listing */
int  copilot_list_models(copilot_client_t *c,
                         copilot_model_info **out_models,
                         int *out_count);
void copilot_free_models(copilot_model_info *models, int count);

/* Error info (thread-local) */
const char *copilot_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* COPILOT_H */
