#pragma once

#include "common.h"
#include "llama.h"
#include "sampling.h"
#include "speculative.h"

// Change JSON_ASSERT from assert() to GGML_ASSERT:
#define JSON_ASSERT GGML_ASSERT
#include "json.hpp"

#include <string>
#include <memory>
#include <unordered_set>

using json = nlohmann::ordered_json;

enum stop_type {
    STOP_TYPE_NONE,
    STOP_TYPE_EOS,
    STOP_TYPE_WORD,
    STOP_TYPE_LIMIT,
};

// state diagram: https://github.com/ggerganov/llama.cpp/pull/9283
enum slot_state {
    SLOT_STATE_IDLE,
    SLOT_STATE_STARTED, // TODO: this state is only used for setting up the initial prompt processing; maybe merge it with launch_slot_with_task in the future
    SLOT_STATE_PROCESSING_PROMPT,
    SLOT_STATE_DONE_PROMPT,
    SLOT_STATE_GENERATING,
};

enum server_state {
    SERVER_STATE_LOADING_MODEL,  // Server is starting up, model not fully loaded yet
    SERVER_STATE_READY,          // Server is ready and model is loaded
};

enum server_task_type {
    SERVER_TASK_TYPE_INFERENCE,
    SERVER_TASK_TYPE_CANCEL,
    SERVER_TASK_TYPE_NEXT_RESPONSE,
    SERVER_TASK_TYPE_METRICS,
    SERVER_TASK_TYPE_SLOT_SAVE,
    SERVER_TASK_TYPE_SLOT_RESTORE,
    SERVER_TASK_TYPE_SLOT_ERASE,
    SERVER_TASK_TYPE_SET_LORA,
};

enum server_task_inf_type {
    SERVER_TASK_INF_TYPE_COMPLETION,
    SERVER_TASK_INF_TYPE_EMBEDDING,
    SERVER_TASK_INF_TYPE_RERANK,
    SERVER_TASK_INF_TYPE_INFILL,
};

// https://community.openai.com/t/openai-chat-list-of-error-codes-and-types/357791/11
enum error_type {
    ERROR_TYPE_INVALID_REQUEST,
    ERROR_TYPE_AUTHENTICATION,
    ERROR_TYPE_SERVER,
    ERROR_TYPE_NOT_FOUND,
    ERROR_TYPE_PERMISSION,
    ERROR_TYPE_UNAVAILABLE, // custom error
    ERROR_TYPE_NOT_SUPPORTED, // custom error
};

struct server_task {
    int id        = -1; // to be filled by server_queue
    int id_target = -1; // used by SERVER_TASK_TYPE_CANCEL

    llama_tokens prompt_tokens;
    server_task_type type;

    // TODO @ngxson : we should get rid of json type here
    json data;

    server_task_inf_type inf_type = SERVER_TASK_INF_TYPE_COMPLETION;

    // utility function
    static std::unordered_set<int> get_list_id(const std::vector<server_task> & tasks) {
        std::unordered_set<int> ids(tasks.size());
        for (size_t i = 0; i < tasks.size(); i++) {
            ids.insert(tasks[i].id);
        }
        return ids;
    }
};

struct slot_params {
    bool stream       = true;
    bool cache_prompt = true; // remember the prompt to avoid reprocessing all prompt

    int32_t n_keep    =  0; // number of tokens to keep from initial prompt
    int32_t n_discard =  0; // number of tokens after n_keep that may be discarded when shifting context, 0 defaults to half
    int32_t n_predict = -1; // new tokens to predict
    int32_t n_indent  =  0; // mininum line indentation for the generated text in number of whitespace characters

    int64_t t_max_prompt_ms  = -1; // TODO: implement
    int64_t t_max_predict_ms = -1; // if positive, limit the generation phase to this time limit

    std::vector<std::string> antiprompt;
    bool timings_per_token = false;

    struct common_params_sampling sampling;
    struct common_params_speculative speculative;

    // params only used in to_json()
    int32_t n_ctx;
    uint32_t seed_cur;
    bool can_speculative;

    // OAI-compat fields
    bool oaicompat = false;
    std::string oaicompat_model;
    std::string oaicompat_cmpl_id;
    bool verbose = false;

    json to_json() {
        std::vector<std::string> samplers;
        samplers.reserve(sampling.samplers.size());
        for (const auto & sampler : sampling.samplers) {
            samplers.emplace_back(common_sampler_type_to_str(sampler));
        }

        return json {
            {"n_ctx",                     n_ctx},
            {"n_predict",                 n_predict},     // Server configured n_predict
            {"temperature",               sampling.temp},
            {"dynatemp_range",            sampling.dynatemp_range},
            {"dynatemp_exponent",         sampling.dynatemp_exponent},
            {"top_k",                     sampling.top_k},
            {"top_p",                     sampling.top_p},
            {"min_p",                     sampling.min_p},
            {"xtc_probability",           sampling.xtc_probability},
            {"xtc_threshold",             sampling.xtc_threshold},
            {"typical_p",                 sampling.typ_p},
            {"repeat_last_n",             sampling.penalty_last_n},
            {"repeat_penalty",            sampling.penalty_repeat},
            {"presence_penalty",          sampling.penalty_present},
            {"frequency_penalty",         sampling.penalty_freq},
            {"dry_multiplier",            sampling.dry_multiplier},
            {"dry_base",                  sampling.dry_base},
            {"dry_allowed_length",        sampling.dry_allowed_length},
            {"dry_penalty_last_n",        sampling.dry_penalty_last_n},
            {"dry_sequence_breakers",     sampling.dry_sequence_breakers},
            {"mirostat",                  sampling.mirostat},
            {"mirostat_tau",              sampling.mirostat_tau},
            {"mirostat_eta",              sampling.mirostat_eta},
            {"penalize_nl",               sampling.penalize_nl},
            {"stop",                      antiprompt},
            {"max_tokens",                n_predict}, // User configured n_predict
            {"n_keep",                    n_keep},
            {"n_discard",                 n_discard},
            {"ignore_eos",                sampling.ignore_eos},
            {"stream",                    stream},
            //{"logit_bias",                sampling.logit_bias},
            {"n_probs",                   sampling.n_probs},
            {"min_keep",                  sampling.min_keep},
            {"grammar",                   sampling.grammar},
            {"samplers",                  samplers},
            {"speculative",               can_speculative},
            {"speculative.n_max",         speculative.n_max},
            {"speculative.n_min",         speculative.n_min},
            {"speculative.p_min",         speculative.p_min},
            {"timings_per_token",         timings_per_token},
        };
    }
};

struct result_timings {
    int32_t prompt_n = -1;
    double prompt_ms;
    double prompt_per_token_ms;
    double prompt_per_second;

    int32_t predicted_n = -1;
    double predicted_ms;
    double predicted_per_token_ms;
    double predicted_per_second;

    json to_json() {
        return {
            {"prompt_n",               prompt_n},
            {"prompt_ms",              prompt_ms},
            {"prompt_per_token_ms",    prompt_per_token_ms},
            {"prompt_per_second",      prompt_per_second},

            {"predicted_n",            predicted_n},
            {"predicted_ms",           predicted_ms},
            {"predicted_per_token_ms", predicted_per_token_ms},
            {"predicted_per_second",   predicted_per_second},
        };
    }
};

struct server_task_result {
    int id           = -1;
    int id_slot      = -1;
    virtual bool is_error() {
        // only used by server_task_result_error
        return false;
    }
    virtual bool is_stop() {
        // only used by server_task_result_cmpl_partial
        return false;
    }
    virtual int get_index() {
        return -1;
    }
    virtual json to_json() = 0;
    virtual json to_json_oai_compat() {
        // used by server_task_result_cmpl_final and server_task_result_cmpl_partial
        return json();
    }
    virtual ~server_task_result() = default;
};

inline std::string stop_type_to_str(stop_type type) {
    switch (type) {
        case STOP_TYPE_EOS:   return "eos";
        case STOP_TYPE_WORD:  return "word";
        case STOP_TYPE_LIMIT: return "limit";
        default:              return "none";
    }
}

struct completion_token_output {
    llama_token tok;
    std::string text_to_send;
    struct token_prob {
        llama_token tok;
        float prob;
    };
    std::vector<token_prob> probs;
};

struct server_task_result_cmpl_final : server_task_result {
    int index = 0;
    std::string content;
    bool stream;
    result_timings timings;
    std::string prompt;

    bool truncated;
    int32_t n_decoded;
    int32_t n_prompt_tokens;
    int32_t n_tokens_cached;
    int32_t has_new_line;
    std::string stopping_word;
    stop_type stop = STOP_TYPE_NONE;

    std::vector<completion_token_output> probs_output;

    slot_params generation_params;

    // OAI-compat fields
    std::string oaicompat_model;
    std::string oaicompat_cmpl_id;
    bool verbose = false;

    virtual int get_index() override {
        return index;
    }

    virtual json to_json() override {
        // non-OAI-compat JSON
        return json {
            {"index",               index},
            {"content",             content},
            {"id_slot",             id_slot},
            {"stop",                true},
            {"model",               oaicompat_model},
            {"tokens_predicted",    n_decoded},
            {"tokens_evaluated",    n_prompt_tokens},
            {"generation_settings", generation_params.to_json()},
            {"prompt",              prompt},
            {"has_new_line",        has_new_line},
            {"truncated",           truncated},
            {"stop_type",           stop_type_to_str(stop)},
            {"stopping_word",       stopping_word},
            {"tokens_cached",       n_tokens_cached},
            {"timings",             timings.to_json()},
        };
    }

    virtual json to_json_oai_compat() override {
        std::string finish_reason = "length";
        if (stop == STOP_TYPE_WORD || stop == STOP_TYPE_EOS) {
            finish_reason = "stop";
        }

        json choices = json::array({json{
            {"finish_reason", finish_reason},
            {"index", 0},
            {"message", json{
                {"content", content},
                {"role", "assistant"}
            }
        }}});

        std::time_t t = std::time(0);

        json res = json {
            {"choices", choices},
            {"created", t},
            {"model", oaicompat_model},
            {"object", "chat.completion"},
            {"usage", json {
                {"completion_tokens", n_decoded},
                {"prompt_tokens",     n_prompt_tokens},
                {"total_tokens",      n_decoded + n_prompt_tokens}
            }},
            {"id", oaicompat_cmpl_id}
        };

        // extra fields for debugging purposes
        if (verbose) {
            res["__verbose"] = to_json();
        }

        // TODO: fix this
        // if (result.contains("completion_probabilities")) {
        //     res["completion_probabilities"] = json_value(result, "completion_probabilities", json::array());
        // }

        if (timings.prompt_n >= 0) {
            res.push_back({"timings", timings.to_json()});
        }

        return res;
    }
};

struct server_task_result_cmpl_partial : server_task_result {
    int index = 0;
    std::string content;

    bool truncated;
    int32_t n_decoded;
    int32_t n_prompt_tokens;

    stop_type stop = STOP_TYPE_NONE;

    std::vector<completion_token_output> probs_output;
    result_timings timings;

    // OAI-compat fields
    std::string oaicompat_model;
    std::string oaicompat_cmpl_id;
    bool verbose = false;

    virtual int get_index() override {
        return index;
    }

    virtual bool is_stop() override {
        return stop != STOP_TYPE_NONE;
    }

    virtual json to_json() override {
        bool is_stop = stop != STOP_TYPE_NONE;
        // non-OAI-compat JSON
        json res = json {
            {"index",            index},
            {"content",          content},
            {"stop_type",        stop_type_to_str(stop)},
            {"stop",             is_stop},
            {"id_slot",          id_slot},
            {"tokens_predicted", n_decoded},
            {"tokens_evaluated", n_prompt_tokens},
        };
        // populate the timings object when needed (usually for the last response or with timings_per_token enabled)
        if (timings.prompt_n > 0) {
            res.push_back({"timings", timings.to_json()});
        }
        if (is_stop) {
            res.push_back({"truncated", truncated});
        }
        return res;
    }

    virtual json to_json_oai_compat() override {
        bool first = n_decoded == 0;

        std::string finish_reason;
        if (stop == STOP_TYPE_WORD || stop == STOP_TYPE_EOS) {
            finish_reason = "stop";
        } else if (stop == STOP_TYPE_LIMIT) {
            finish_reason = "length";
        }

        std::time_t t = std::time(0);

        json choices;

        if (!finish_reason.empty()) {
            choices = json::array({json{{"finish_reason", finish_reason},
                                        {"index", 0},
                                        {"delta", json::object()}}});
        } else {
            if (first) {
                if (content.empty()) {
                    choices = json::array({json{{"finish_reason", nullptr},
                                                {"index", 0},
                                                {"delta", json{{"role", "assistant"}}}}});
                } else {
                    // We have to send this as two updates to conform to openai behavior
                    json initial_ret = json{{"choices", json::array({json{
                                            {"finish_reason", nullptr},
                                            {"index", 0},
                                            {"delta", json{
                                                {"role", "assistant"}
                                            }}}})},
                                {"created", t},
                                {"id", oaicompat_cmpl_id},
                                {"model", oaicompat_model},
                                {"object", "chat.completion.chunk"}};

                    json second_ret = json{
                                {"choices", json::array({json{{"finish_reason", nullptr},
                                                                {"index", 0},
                                                                {"delta", json{
                                                                {"content", content}}}
                                                                }})},
                                {"created", t},
                                {"id", oaicompat_cmpl_id},
                                {"model", oaicompat_model},
                                {"object", "chat.completion.chunk"}};

                    return std::vector<json>({initial_ret, second_ret});
                }
            } else {
                // Some idiosyncrasy in task processing logic makes several trailing calls
                // with empty content, we ignore these at the calee site.
                if (content.empty()) {
                    return std::vector<json>({json::object()});
                }

                choices = json::array({json{
                    {"finish_reason", nullptr},
                    {"index", 0},
                    {"delta",
                    json{
                        {"content", content},
                    }},
                }});
            }
        }

        json ret = json {
            {"choices", choices},
            {"created", t},
            {"id",      oaicompat_cmpl_id},
            {"model",   oaicompat_model},
            {"object",  "chat.completion.chunk"}
        };

        if (timings.prompt_n >= 0) {
            ret.push_back({"timings", timings.to_json()});
        }

        if (!finish_reason.empty()) {
            ret.push_back({"usage", json {
                {"completion_tokens", n_decoded},
                {"prompt_tokens",     n_prompt_tokens},
                {"total_tokens",      n_decoded + n_prompt_tokens},
            }});
        }

        return std::vector<json>({ret});
    }
};

struct server_task_result_embd : server_task_result {
    int index = 0;
    std::vector<float> embedding;

    virtual int get_index() override {
        return index;
    }

    virtual json to_json() override {
        return json {
            {"index",     index},
            {"embedding", embedding},
        };
    }
};

struct server_task_result_rerank : server_task_result {
    int index = 0;
    float score = -1e6;

    virtual int get_index() override {
        return index;
    }

    virtual json to_json() override {
        return json {
            {"index", index},
            {"score", score},
        };
    }
};

// this function maybe used outside of server_task_result_error
static json format_error_response(const std::string & message, const enum error_type type) {
    std::string type_str;
    int code = 500;
    switch (type) {
        case ERROR_TYPE_INVALID_REQUEST:
            type_str = "invalid_request_error";
            code = 400;
            break;
        case ERROR_TYPE_AUTHENTICATION:
            type_str = "authentication_error";
            code = 401;
            break;
        case ERROR_TYPE_NOT_FOUND:
            type_str = "not_found_error";
            code = 404;
            break;
        case ERROR_TYPE_SERVER:
            type_str = "server_error";
            code = 500;
            break;
        case ERROR_TYPE_PERMISSION:
            type_str = "permission_error";
            code = 403;
            break;
        case ERROR_TYPE_NOT_SUPPORTED:
            type_str = "not_supported_error";
            code = 501;
            break;
        case ERROR_TYPE_UNAVAILABLE:
            type_str = "unavailable_error";
            code = 503;
            break;
    }
    return json {
        {"code", code},
        {"message", message},
        {"type", type_str},
    };
}

struct server_task_result_error : server_task_result {
    int index = 0;
    error_type err_type = ERROR_TYPE_SERVER;
    std::string err_msg;

    virtual bool is_error() override {
        return true;
    }

    virtual json to_json() override {
        return format_error_response(err_msg, err_type);
    }
};

struct server_task_result_metrics : server_task_result {
    int n_idle_slots;
    int n_processing_slots;
    int n_tasks_deferred;
    int64_t t_start;

    int32_t kv_cache_tokens_count;
    int32_t kv_cache_used_cells;

    // TODO: somehow reuse server_metrics in the future, instead of duplicating the fields
    uint64_t n_prompt_tokens_processed_total = 0;
    uint64_t t_prompt_processing_total       = 0;
    uint64_t n_tokens_predicted_total        = 0;
    uint64_t t_tokens_generation_total       = 0;

    uint64_t n_prompt_tokens_processed = 0;
    uint64_t t_prompt_processing       = 0;

    uint64_t n_tokens_predicted  = 0;
    uint64_t t_tokens_generation = 0;

    uint64_t n_decode_total     = 0;
    uint64_t n_busy_slots_total = 0;

    // TODO: get rid of this json object and use to_json() instead
    json slots_data = json::array();

    virtual json to_json() override {
        return json {
            { "idle",                            n_idle_slots },
            { "processing",                      n_processing_slots },
            { "deferred",                        n_tasks_deferred },
            { "t_start",                         t_start },

            { "n_prompt_tokens_processed_total", n_prompt_tokens_processed_total },
            { "t_tokens_generation_total",       t_tokens_generation_total },
            { "n_tokens_predicted_total",        n_tokens_predicted_total },
            { "t_prompt_processing_total",       t_prompt_processing_total },

            { "n_prompt_tokens_processed",       n_prompt_tokens_processed },
            { "t_prompt_processing",             t_prompt_processing },
            { "n_tokens_predicted",              n_tokens_predicted },
            { "t_tokens_generation",             t_tokens_generation },

            { "n_decode_total",                  n_decode_total },
            { "n_busy_slots_total",              n_busy_slots_total },

            { "kv_cache_tokens_count",           kv_cache_tokens_count },
            { "kv_cache_used_cells",             kv_cache_used_cells },

            { "slots",                           slots_data },
        };
    }
};

struct server_task_result_slot_save_load : server_task_result {
    std::string filename;
    bool is_save; // true = save, false = load

    size_t n_tokens;
    size_t n_bytes;
    double t_ms;

    virtual json to_json() override {
        if (is_save) {
            return json {
                { "id_slot",   id_slot },
                { "filename",  filename },
                { "n_saved",   n_tokens },
                { "n_written", n_bytes },
                { "timings", {
                    { "save_ms", t_ms }
                }},
            };
        } else {
            return json {
                { "id_slot",    id_slot },
                { "filename",   filename },
                { "n_restored", n_tokens },
                { "n_read",     n_bytes },
                { "timings", {
                    { "restore_ms", t_ms }
                }},
            };
        }
    }
};

struct server_task_result_slot_erase : server_task_result {
    size_t n_erased;

    virtual json to_json() override {
        return json {
            { "id_slot",  id_slot },
            { "n_erased", n_erased },
        };
    }
};

struct server_task_result_apply_lora : server_task_result {
    virtual json to_json() override {
        return json {{ "success", true }};
    }
};
