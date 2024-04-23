#pragma once

/***
 *
 * ## Overview
 *
 * Helps chat with a model, by allowing role based special token tagging, based on the specified chat-handshake-template-standard.
 * This is used by main, to build on existing interactive flow and its in-prefix, in-suffix and antiprompt/reverse-promot
 *
 * 1. Use a json file to configure the needed tags for each of the supported chat-handshake-template-standard
 *    a. system -> prefix & suffix,
 *    b. user -> prefix & suffix, assistant -> prefix
 *       * [main] these override the in-prefix and in-suffix
 *    c. reverse-prompt
 *       * [main] this adds to any reverese-prompt specified using cmdline
 *    d. global -> begin & end
 *    d. systemuser-1st-user-has-prefix
 *       * if a combination of system and user messages/prompts is passed,
 *         then for the 1st user message following the 1st system message,
 *         include user prefix only if this flag is set. [chaton-tmpl-apply]
 *       * [later] one or two models which I looked at seem to require not just BoS, but also the user-role-prefix-tag
 *         to also be controlled wrt this case. So not differentiating between BoS and any user-role-prefix-tag.
 *         However if bos and user-role-prefix-tag need to be decoupled, where only bos needs this treatment,
 *         then maybe add begin and end keys (to specify the BoS) in addition to prefix and suffix keys (to specify user-role-prefix-tag), to role blocks in the json.
 *         and inturn control only begin and not prefix, wrt whether to add or not.
 * 2. [main] currently the user specified system prompt (-p + -f) is tagged using system role tags,
 *    and inturn this tagged message is tokenized with parse_special flag.
 *    So any special token related tags in the user specified system prompt will get parsed as special.
 * 3. chaton-tmpl-apply uses the json file, which was loaded, to decide on how to generate the tagged messages for tokenisation.
 *    a. input: [ { role, message }, { role, message}, ....]
 *    b. output: currently a single string is returned which contains the tagged message(s).
 *       [later] if it is needed to differentiate between the special tags added by this from user specified prompts/messages,
 *       then return [ {flag, data}, { flag, data}, {flag, data}, ....],
 *       where the flag specifies whether parse_special should be used or not for the corresponding data, during tokenization.
 *
 * ## Adding support for new model / chat-handshake-template-standard
 *
 * 1. Add suitable entries in json for that model/standard
 * 2. Update the flow in chaton-tmpl-apply, as needed.
 *    Try to update and or reuse the generic flow in chaton-tmpl-apply, as much as possible,
 *    before trying to add a custom logic.
 *    If you update the generic flow, cross check if existing json files will need to be updated or not.
 *
 * ## Notes
 *
 * Currently Main doesnt use chaton-tmpl-apply, but only 
 * * chaton-tmpl-apply-single (for system prompt) and
 * * chaton-tmpl-role-part which maps the user prefix, suffix and reverse-prompt to
 *   in-prefix, in-suffix and antiprompt of main.
 * These always adds any role specific prefix and suffix around the passed message.
 *
 */

#include <string>
#include <fstream>
#include <iostream>
#include <json.hpp>

#include "log.h"
#include "llama.h"


const auto K_SYSTEM = "system";
const auto K_USER = "user";
const auto K_ASSISTANT = "assistant";
const auto K_PREFIX = "prefix";
const auto K_SUFFIX = "suffix";
const auto K_BEGIN = "begin";
const auto K_END = "end";
const auto K_GLOBAL = "global";
const auto K_SYSTEMUSER_1ST_USER_HAS_PREFIX = "systemuser-1st-user-has-prefix";
const auto K_REVERSE_PROMPT = "reverse-prompt";


using json = nlohmann::json;

json conMeta;

inline bool chaton_meta_load(std::string &fname) {
    std::ifstream f(fname);
    conMeta = json::parse(f);
    return true;
}

inline void _chaton_meta_dump(std::string &tmpl) {
    json theJson;
    if (tmpl.empty()) {
        theJson = conMeta;
    } else {
        theJson = conMeta[tmpl];
    }
    LOG_TEELN("\n\nINFO:%s:ChatOn Meta\n%s", __func__, theJson.dump(4).c_str());
    if (!tmpl.empty()) {
        LOG("INFO:%s:%s:%s", __func__, "global->begin", chaton_tmpl_role_kv(tmpl, K_GLOBAL, K_BEGIN));
        LOG("INFO:%s:%s:%s", __func__, "global->end", chaton_tmpl_role_kv(tmpl, K_GLOBAL, K_END));
        LOG("INFO:%s:%s:%s", __func__, "system->prefix", chaton_tmpl_role_kv(tmpl, K_SYSTEM, K_PREFIX));
        LOG("INFO:%s:%s:%s", __func__, "system->suffix", chaton_tmpl_role_kv(tmpl, K_SYSTEM, K_SUFFIX));
        LOG("INFO:%s:%s:%s", __func__, "user->prefix", chaton_tmpl_role_kv(tmpl, K_USER, K_PREFIX));
        LOG("INFO:%s:%s:%s", __func__, "user->suffix", chaton_tmpl_role_kv(tmpl, K_USER, K_SUFFIX));
        LOG("INFO:%s:%s:%s", __func__, "assistant->prefix", chaton_tmpl_role_kv(tmpl, K_ASSISTANT, K_PREFIX));
        LOG("INFO:%s:%s:%s", __func__, "assistant->suffix", chaton_tmpl_role_kv(tmpl, K_ASSISTANT, K_SUFFIX));
    }
}

/**
 * if tmpl is
 * * empty string, then dump the full loaded chaton-meta
 * * chaton-template-id, then dump contents related to that specific chat-handshake-template-standard
 */
inline bool chaton_meta_ok(std::string &tmpl) {
    if (conMeta == nullptr) {
        LOG_TEELN("ERRR:%s:ChatOn Meta: Not loaded yet...", __func__);
        return false;
    }
    _chaton_meta_dump(tmpl);
    return true;
}


// Return user-prefix + msg + user-suffix
// NOTE: This currently doesnt return about which parts of the tagged message contain tags and which parts the user message
inline std::string chaton_tmpl_apply_single(const std::string &tmpl, const std::string &role, const std::string &content) {
    std::stringstream ss;
    ss << conMeta[tmpl][role][K_PREFIX] << content << conMeta[tmpl][role][K_SUFFIX];
    std::string taggedStr = ss.str();
    LOG_TEELN("DBUG:%s:%s:%s:%s", __func__, tmpl.c_str(), role.c_str(), taggedStr.c_str());
    return taggedStr;
}

// global-begin + [role-prefix + msg + role-suffix] + global-end
// if there is a combination of system-user messages,
//    then 1st user message will have user-prefix only if systemuser-1st-user-has-prefix is true
// NOTE: This currently doesnt return about which parts of the tagged message contain tags and which parts the user message
inline std::string chaton_tmpl_apply(const std::string &tmpl, const std::vector<llama_chat_message> &msgs) {
    std::stringstream ss;
    ss << conMeta[tmpl][K_GLOBAL][K_BEGIN];
    int cntSystem = 0;
    int cntUser = 0;
    int cntOthers = 0;
    for(const auto msg: msgs) {
        auto role = msg.role;
        auto content = msg.content;
        auto prefix = conMeta[tmpl][role][K_PREFIX];
        if (role == K_SYSTEM) {
            cntSystem += 1;
            ss << prefix;
        } else if (role == K_USER) {
            cntUser += 1;
            if ((cntSystem == 1) && (cntUser == 1)) {
                if (conMeta[tmpl][K_SYSTEMUSER_1ST_USER_HAS_PREFIX]) {
                    ss << prefix;
                }
            } else {
                ss << prefix;
            }
        } else {
            cntOthers += 1;
            ss << prefix;
        }
        ss << content << conMeta[tmpl][role][K_SUFFIX];
    }
    ss << conMeta[tmpl][K_GLOBAL][K_END];
    std::string taggedMsgs = ss.str();
    LOG_TEELN("DBUG:%s:%s:%s", __func__, tmpl.c_str(), taggedMsgs.c_str());
    return taggedMsgs;
}

inline std::string chaton_tmpl_role_kv(const std::string &tmpl, const std::string &role, const std::string &key) {
    std::string got = conMeta[tmpl][role][key];
    LOG_TEELN("DBUG:%s:%s:%s:%s:%s", __func__, tmpl.c_str(), role.c_str(), key.c_str(), got.c_str());
    return got;
}

inline std::string chaton_tmpl_kv(const std::string &tmpl, const std::string &key) {
    std::string got = conMeta[tmpl][key];
    LOG_TEELN("DBUG:%s:%s:%s:%s", __func__, tmpl.c_str(), key.c_str(), got.c_str());
    return got;
}
