#include "copilot.h"

#include <copilot/copilot.hpp>

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

/* Thread-local error string */
static thread_local std::string g_last_error;

static void set_error(const char *msg)
{
	g_last_error = msg;
}

static void set_error(const std::exception &e)
{
	g_last_error = e.what();
}

/* Opaque wrapper structs */
struct copilot_client {
	std::unique_ptr<copilot::Client> impl;
};

struct copilot_session {
	std::shared_ptr<copilot::Session> impl;
	copilot::Subscription subscription;
};

/* Map C++ event types to C enum */
static copilot_event_type map_event_type(copilot::SessionEventType t)
{
	switch (t) {
	case copilot::SessionEventType::SessionStart:
		return COPILOT_EVENT_SESSION_START;
	case copilot::SessionEventType::SessionResume:
		return COPILOT_EVENT_SESSION_RESUME;
	case copilot::SessionEventType::SessionError:
		return COPILOT_EVENT_SESSION_ERROR;
	case copilot::SessionEventType::SessionIdle:
		return COPILOT_EVENT_SESSION_IDLE;
	case copilot::SessionEventType::AssistantMessage:
		return COPILOT_EVENT_ASSISTANT_MESSAGE;
	case copilot::SessionEventType::AssistantMessageDelta:
		return COPILOT_EVENT_ASSISTANT_MESSAGE_DELTA;
	case copilot::SessionEventType::ToolExecutionStart:
		return COPILOT_EVENT_TOOL_EXECUTION_START;
	case copilot::SessionEventType::ToolExecutionComplete:
		return COPILOT_EVENT_TOOL_EXECUTION_COMPLETE;
	default:
		return COPILOT_EVENT_UNKNOWN;
	}
}

/* Extract a JSON string from a SessionEvent for the C callback */
static std::string event_data_json(const copilot::SessionEvent &evt)
{
	/* For delta events, provide just the delta text for efficiency */
	if (auto *d = evt.try_as<copilot::AssistantMessageDeltaData>()) {
		copilot::json j;
		j["message_id"] = d->message_id;
		j["delta"] = d->delta_content;
		return j.dump();
	}
	if (auto *d = evt.try_as<copilot::AssistantMessageData>()) {
		copilot::json j;
		j["message_id"] = d->message_id;
		j["content"] = d->content;
		return j.dump();
	}
	if (auto *d = evt.try_as<copilot::SessionErrorData>()) {
		copilot::json j;
		j["error_type"] = d->error_type;
		j["message"] = d->message;
		return j.dump();
	}
	/* Fallback: empty object */
	return "{}";
}

extern "C" {

/* ---------- Client ---------- */

copilot_client_t *copilot_client_create(void)
{
	try {
		auto c = new copilot_client;
		c->impl = std::make_unique<copilot::Client>();
		return c;
	} catch (const std::exception &e) {
		set_error(e);
		return nullptr;
	}
}

int copilot_client_start(copilot_client_t *c)
{
	if (!c) {
		set_error("null client");
		return -1;
	}
	try {
		c->impl->start().get();
		return 0;
	} catch (const std::exception &e) {
		set_error(e);
		return -1;
	}
}

int copilot_client_stop(copilot_client_t *c)
{
	if (!c) {
		set_error("null client");
		return -1;
	}
	try {
		c->impl->stop().get();
		return 0;
	} catch (const std::exception &e) {
		set_error(e);
		return -1;
	}
}

void copilot_client_destroy(copilot_client_t *c)
{
	delete c;
}

/* ---------- Session ---------- */

copilot_session_t *copilot_session_create(copilot_client_t *c,
                                          const char *model,
                                          const char *system_prompt)
{
	if (!c) {
		set_error("null client");
		return nullptr;
	}
	try {
		copilot::SessionConfig cfg;
		if (model)
			cfg.model = std::string(model);
		if (system_prompt) {
			copilot::SystemMessageConfig smc;
			smc.content = std::string(system_prompt);
			cfg.system_message = smc;
		}
		cfg.streaming = true;

		auto session = c->impl->create_session(cfg).get();
		auto s = new copilot_session;
		s->impl = session;
		return s;
	} catch (const std::exception &e) {
		set_error(e);
		return nullptr;
	}
}

int copilot_session_send(copilot_session_t *s,
                         const char *message,
                         copilot_event_cb on_event,
                         void *userdata)
{
	if (!s) {
		set_error("null session");
		return -1;
	}
	try {
		/* Subscribe to events for this send */
		if (on_event) {
			s->subscription = s->impl->on(
				[on_event, userdata](const copilot::SessionEvent &evt) {
					auto type = map_event_type(evt.type);
					auto json_str = event_data_json(evt);
					on_event(type, json_str.c_str(), userdata);
				});
		}

		copilot::MessageOptions opts;
		opts.prompt = message ? message : "";
		s->impl->send(opts).get();
		return 0;
	} catch (const std::exception &e) {
		set_error(e);
		return -1;
	}
}

int copilot_session_send_and_wait(copilot_session_t *s,
                                  const char *message,
                                  copilot_event_cb on_event,
                                  void *userdata,
                                  int timeout_secs)
{
	if (!s) {
		set_error("null session");
		return -1;
	}
	try {
		if (on_event) {
			s->subscription = s->impl->on(
				[on_event, userdata](const copilot::SessionEvent &evt) {
					auto type = map_event_type(evt.type);
					auto json_str = event_data_json(evt);
					on_event(type, json_str.c_str(), userdata);
				});
		}

		copilot::MessageOptions opts;
		opts.prompt = message ? message : "";
		auto timeout = std::chrono::seconds(timeout_secs > 0 ? timeout_secs : 60);
		s->impl->send_and_wait(opts, timeout).get();
		return 0;
	} catch (const std::exception &e) {
		set_error(e);
		return -1;
	}
}

void copilot_session_destroy(copilot_session_t *s)
{
	if (!s)
		return;
	try {
		s->impl->destroy().get();
	} catch (...) {
		/* best-effort cleanup */
	}
	delete s;
}

/* ---------- Tools ---------- */

int copilot_session_register_tool(copilot_session_t *s,
                                  const char *name,
                                  const char *description,
                                  const char *params_schema_json,
                                  copilot_tool_cb handler,
                                  void *userdata)
{
	if (!s || !name || !description) {
		set_error("null argument");
		return -1;
	}
	try {
		copilot::Tool tool;
		tool.name = name;
		tool.description = description;
		if (params_schema_json)
			tool.parameters_schema = copilot::json::parse(params_schema_json);
		else
			tool.parameters_schema = copilot::json::object();

		tool.handler = [handler, userdata, tool_name = std::string(name)](
				const copilot::ToolInvocation &inv) -> copilot::ToolResultObject {
			copilot::ToolResultObject result;
			if (!handler) {
				result.result_type = copilot::ToolResultType::Failure;
				result.error = "no handler";
				return result;
			}
			std::string args = inv.arguments
				? inv.arguments->dump()
				: "{}";
			char *ret = handler(tool_name.c_str(), args.c_str(), userdata);
			if (ret) {
				result.text_result_for_llm = ret;
				free(ret);
			} else {
				result.result_type = copilot::ToolResultType::Failure;
				result.error = "handler returned null";
			}
			return result;
		};

		s->impl->register_tool(tool);
		return 0;
	} catch (const std::exception &e) {
		set_error(e);
		return -1;
	}
}

/* ---------- Models ---------- */

int copilot_list_models(copilot_client_t *c,
                        copilot_model_info **out_models,
                        int *out_count)
{
	if (!c || !out_models || !out_count) {
		set_error("null argument");
		return -1;
	}
	try {
		auto models = c->impl->list_models().get();
		int n = (int)models.size();
		auto *arr = (copilot_model_info *)calloc(n, sizeof(copilot_model_info));
		if (!arr) {
			set_error("out of memory");
			return -1;
		}
		for (int i = 0; i < n; i++) {
			arr[i].id = strdup(models[i].id.c_str());
			arr[i].name = strdup(models[i].name.c_str());
		}
		*out_models = arr;
		*out_count = n;
		return 0;
	} catch (const std::exception &e) {
		set_error(e);
		return -1;
	}
}

void copilot_free_models(copilot_model_info *models, int count)
{
	if (!models)
		return;
	for (int i = 0; i < count; i++) {
		free(models[i].id);
		free(models[i].name);
	}
	free(models);
}

/* ---------- Error ---------- */

const char *copilot_last_error(void)
{
	return g_last_error.empty() ? nullptr : g_last_error.c_str();
}

} /* extern "C" */
