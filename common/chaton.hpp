#pragma once

/**
 * 
 * Generic tagging logic + configurable template data based chat templates handling
 * by Humans for All
 * 
 * ## Overview
 * 
 * Helps chat with models, by tagging chat messages based on the specified
 * chat-handshake-template-standard. This uses a generic tagging code driven
 * by configurable template data which is either builtin or loaded from text/
 * json file, which specifies the handshake template details.
 * 
 * This can be used by
 * 
 * * examples/main, to build on existing interactive flow and its in-prefix, in-suffix
 *   and antiprompt/reverse-prompt
 * 
 * * examples/server|..., by replacing its existing llama_chat_apply_template with the
 *   equivalent helper here.
 * 
 * 
 * ## The common pattern
 * 
 * As a convention, the tagging used by LLMs to differentiate between the
 * different parts when chatting with them normally follows a general pattern of
 * 
 * * <BeginOfSentenceIfAny> <RolePrefixIfAny> <TheContent> <RoleSuffixIfAny> <EndOfSentenceIfAny>
 * 
 * * The Roles could include System, User and Assistant (ie the Model)
 * 
 * * A chat normally consists of
 * 
 *   * a System message/prompt followed by
 * 
 *   * multiple user message/query - model message/response pairs
 * 
 * The different models will normally have all or some subset of the tagging mentioned above.
 * 
 * You may also notice some common patterns like
 * 
 * * Because a user message is normally followed by model/assistant response, in most models
 * 
 *   * user messages wont have EndOfSentenceTag and
 * 
 *   * the following model response wont have BeginOfSentenceTag
 * 
 * * Because a system message will normally be immidiately followed by a user query,
 * 
 *   * in many models, there wont be a EndOfSentenceTag following the system message and
 *     BeginOfSentenceTag wrt the 1st user message following the system message.
 * 
 *   * in some models there wont even be a RoleSuffixTag following system message
 *     and RolePrefixTag wrt the 1st user message following the system message.
 * 
 *   * however in many of these models, the subsequent user messages will have the
 *     BeginOfSentenceTag and or RolePrefixTag.
 * 
 * * Some models may require a BoS for a group of messages, independent of BoS (if any)
 *   wrt individual roles.
 * 
 * 
 * ## The Strategy
 * 
 * The configurable template data allows the user to specify the above mentioned tags wrt
 * each of the Role as well as any global tag for a group of messages. Depending on whether
 * a given model uses/needs a given tag or not you either specify the required tag or else
 * you specify a empty string.
 * 
 * A tag could be a single word or multiple words, and may include newline char specified
 * using \n and so on. The tag is always demarcated using double quotes and thus also allows
 * spaces at the begining or end of the tag, if needed.
 * 
 * In order to account for the conditionality of tags between the system message and the
 * following 1st user message, flags are provided to explicitly control whether each of
 * these possible tags is used by a specific model or not, as part of its template info.
 * 
 * The Roles are identified in the configurable template data using "system", "user" and
 * "assistant". However the model may use different words to identify these roles, in which
 * case setup RolePrefix and or RoleSuffix appropriately.
 * 
 * To identify that model is finished with generating response to user query, depending on
 * the model's handshake template standard, one will need to set the reverse-prompt to either
 * the assistant's suffix or end tag or to the user's begin or prefix tag, depending on what
 * is generated by the model at the end of its response.
 * 
 * Currently flags for trimming wrt user text (be it wrt system or user role) is not added.
 * 
 * 
 * ## Configurable template data and related optional text/JSON file
 * 
 * Can contain the template info wrt multiple models/handshake-standards. And inturn each
 * unique template is identified by a unique template id string.
 * 
 * The fields that make up a given chat-handshake-template-standard include
 * 
 * * global -> begin & end
 * 
 * * system -> begin, prefix, suffix & end
 * 
 * * user -> begin, prefix, suffix & end
 * 
 * * assistant -> begin, prefix, suffix & end
 * 
 * * reverse-prompt
 * 
 * * systemuser-system-has-suffix, systemuser-system-has-end,
 *   systemuser-1st-user-has-begin and systemuser-1st-user-has-prefix
 * 
 * By default one can preload at compile time. Additionally one could update/load
 * more at runtime. A compile time optionally enabled load from json helper is
 * provided. For any reason, if one doesnt want to use the json based mechanism,
 * and instead wants a simple mechanism for runtime updating/loading, one could
 * update ChatTemplates to extend from SimpCfg and inturn use its load from a
 * simple text file based flow.
 * 
 * 
 * ## Usage
 * 
 * One could use the logic along with compile time builtin configurable template data as is
 * or one could optionally load configurable template data from a text/json file containing
 * the template meta data and inturn call the other helper functions as needed.
 * 
 * NOTE: One could either make do with a pre-compiled chat templates info, or allow users
 * to update/modify/override the pre-compiled info and or extend with info for new models
 * or chat-handshake-template-standards at runtime.
 * 
 * Inturn one can use the helper functions to either extract a given tag or to apply all
 * tags specified wrt a given role to the passed message or to apply tags as needed for
 * a bunch of messages in one go.
 * 
 * The single message tagging helper setup to apply all tags specified wrt that role.
 * 
 * The multiple messages tagging helper chaton-tmpl-apply[-ex][-capi], will look at the
 * boolean flags when tagging the passed messages. In this the system suffix, system end,
 * user begin and user prefix get included only if corresponding flag is set, the 1st time
 * system + user message is encountered.
 * 
 * The multi messages tagging is provided in two versions.
 * * one which returns a single string which contains the tagged message(s)
 * * one which returns [ex version]
 *   * [tagged msg] the string containing the tagged message(s)
 *   * [parts lengths] an array of integers, which specifies the part lengths,
 *     which divides the returned string into parts.
 *   * [parts types] a string where each character indicates whether the corresponding
 *     part is a normal part which needs to be tokenized without parse_special
 *     or is a special part which needs to be tokenized with parse-special.
 * 
 * A single message wrapper is provided for the simple (no extended) version.
 * 
 * chaton_llama_tokenize_ex is provided to show how the extended helpers additional
 * subparts info wrt tagged message could be used to tokenize with and without
 * parse_special to the appropriate subparts that make up the tagged message.
 * 
 * 
 * ## example/main
 * 
 * The interactive commandline program under example/main, uses
 * 
 * * the system role related tags to tag the system prompt
 *   * the system prompt includes contents of -p if any
 *   * followed by contents of file specified using -f if any
 * * the user begin+prefix to map to in-prefix
 * * the user suffix+end to map to in-suffix
 * * the reverse-prompt to map to antiprompt
 * * wrt tokenization
 *   * the user specified system prompt is tokenized with parse_special flag.
 *   * however the user messages are tokenized with/without parse_special flag,
 *     based on interactive-specials.
 * 
 * Currently Main doesnt use chaton-tmpl-apply, but only 
 * * chaton-tmpl-apply-single (for system prompt) and
 * * chaton_tmpl_role_getkeys, used to map the user prefix and suffix
 *   to in-prefix, in-suffix of main.
 * * chaton_tmpl_getkey_str, used to map reverse-prompt to main's antiprompt.
 * These always adds any role specific begin+prefix and suffix+end around
 * the passed message.
 * 
 * ## other uses be it wrt llama.cpp-as-library or examples/server or ...
 * 
 * This module exposes a c-api which is equivalent to the current hardcoded
 * templating logic's llama_chat_apply_template. So any program using llama.cpp's
 * chat templating logic can be easily migrated to make use of this generic code
 * with text based config file based flow.
 * 
 * If a program doesnt want to bring in json dependency into their project,
 * one can make do with the pre initialized configurable template data which
 * is compiled in.
 * 
 * Additionally, if runtime configurability required without json dependency,
 * the ChatTemplates can be updated to extend SimpCfg from common/simpcfg.hpp,
 * which provides a simple text based config file format, along with the
 * corresponding parser for the same. This should be relatively easy, if needed.
 * 
 * ## Adding support for new model / chat-handshake-template-standard
 * 
 * 1. Add suitable entries wrt configurable template data, either as part of the
 *    compile time builtin initialisation or the text/json file loaded at runtime,
 *    for that model/standard. This in itself should work for most of the models.
 * 
 * 2. If some new model introduces a totally different kind of chat-templating
 *    tag inter/intra mixing, Try to reuse and update the generic flow in
 *    chaton-tmpl-apply-ex, as much as possible, before trying to add any custom logic.
 * 
 *    If you update the generic flow, cross check if existing text/json files will
 *    need to be updated or not.
 * 
 * 
 * ## Notes
 * 
 * Look at the sample chaton_meta.json in examples folder for how the above may apply to
 * the different llm's out there like
 * 
 * * llama2, llama3, gemma, zephyr, deepseek(normal and coder), monarch, mistral, phi3
 * * chatml, command-r, orion, openchat, vicuna
 * 
 */

#include <string>
#include <fstream>
#include <groupkv.hpp>

#include "log.h"
#include "llama.h"

#define LOGXLN LOG_TEELN

const auto K_SYSTEM = "system";
const auto K_USER = "user";
const auto K_ASSISTANT = "assistant";
const auto K_PREFIX = "prefix";
const auto K_SUFFIX = "suffix";
const auto K_BEGIN = "begin";
const auto K_END = "end";
const auto K_GLOBAL = "global";
const auto K_SYSTEMUSER_SYSTEM_HAS_SUFFIX = "systemuser-system-has-suffix";
const auto K_SYSTEMUSER_SYSTEM_HAS_END = "systemuser-system-has-end";
const auto K_SYSTEMUSER_1ST_USER_HAS_BEGIN = "systemuser-1st-user-has-begin";
const auto K_SYSTEMUSER_1ST_USER_HAS_PREFIX = "systemuser-1st-user-has-prefix";
const auto K_REVERSE_PROMPT = "reverse-prompt";



/**
 * Helps keep user prompt and chat-hs-template tag parts seperate, but in sequence.
 * Inturn gives the flexibility to tokenize with or without parse_special flag, wrt the different parts of the chat msg(s).
 * One could use the triplet of str, get_types and get_partslens to achieve the above mentioned flexibility.
 */
class ChatParts {

    std::vector<std::string> parts = {};
    std::string types = {""};

public:
    // Identify string with special tokens that need to be processed.
    static const auto S = 's';
    // Identify string which shouldnt have special token processing done.
    static const auto N = 'n';
    // Identify no string condition and or ignore string.
    static const auto X = '?';

    ChatParts() : parts{}, types{""} {}

    char last_type() {
        if (types.length() == 0) {
            return ChatParts::X;
        }
        return types[types.length()-1];
    }

    void add_part(char type, const std::string &part) {
        if (last_type() == type) {
            parts[parts.size()-1] += part;
        } else {
            parts.emplace_back(part);
            types += type;
        }
    }

    std::string str() {
        std::string allin = "";
        for(auto part: parts) {
            allin += part;
        }
        return allin;
    }

    std::string get_partstypes() {
        return types;
    }

    std::vector<int32_t> get_partslens() {
        std::vector<int32_t> lens = {};
        for(auto part: parts) {
            lens.push_back(part.length());
        }
        return lens;
    }

    std::string name() {
        return typeid(*this).name();
    }

    std::string dump(const std::string &msgTag) {
        std::stringstream ss;
        std::string me = name() + ":" + __func__;
        ss << msgTag << ":NumTypes:" << types.length() << std::endl;
        ss << msgTag << ":NumParts:" << parts.size() << std::endl;
        ss << msgTag << ":StrLength:" << str().length() << std::endl;
        if (parts.size() != types.length()) {
            LOG_TEELN("DBUG:%s:Mismatch between parts[%zu] and types[%zu]", me.c_str(), parts.size(), types.length());
        }
        int i = 0;
        for(auto part: parts) {
            ss << msgTag << ":Part:" << i << ":" << types[i] << ":" << part << std::endl;
            i += 1;
        }
        return ss.str();
    }

};


class ChatTemplates : public GroupKV {

public:

    ChatTemplates(GroupKVMapMapVariant defaultMap) : GroupKV(defaultMap) {}

    /**
     * Check if the specified chat-template exists or not.
     * NOTE: This doesnt cross check, if the template inturn contains all the required fields or not.
    */
    bool tmpl_exists(const std::string &tmpl, const std::string &msgTag="") {
        if (!group_exists(tmpl)) {
            LOG_TEELN("WARN:CT:%s:%s:Specified template-id [%s] not found...", __func__, msgTag.c_str(), tmpl.c_str());
            return false;
        }
        return true;
    }

    /**
     * Check if all expected keys/fields are present wrt the specified chat-template.
     * If any key/field is missing, expect a exception.
     * 
     * Additionally also return a string containing info about all the fields.
    */
    bool tmpl_basiccheck(const std::string &tmpl, std::stringstream &ss, const std::string &msgTag) {

        if (!tmpl_exists(tmpl, msgTag)) {
            return false;
        }

        std::string globalBegin = get_value<std::string>(tmpl, { K_GLOBAL, K_BEGIN });
        std::string globalEnd = get_value<std::string>(tmpl, { K_GLOBAL, K_END });

        std::string systemBegin = get_value<std::string>(tmpl, { K_SYSTEM, K_BEGIN });
        std::string systemPrefix = get_value<std::string>(tmpl, { K_SYSTEM, K_PREFIX });
        std::string systemSuffix = get_value<std::string>(tmpl, { K_SYSTEM, K_SUFFIX });
        std::string systemEnd = get_value<std::string>(tmpl, { K_SYSTEM, K_END });

        std::string userBegin = get_value<std::string>(tmpl, { K_USER, K_BEGIN });
        std::string userPrefix = get_value<std::string>(tmpl, { K_USER, K_PREFIX });
        std::string userSuffix = get_value<std::string>(tmpl, { K_USER, K_SUFFIX });
        std::string userEnd = get_value<std::string>(tmpl, { K_USER, K_END });

        std::string assistantBegin = get_value<std::string>(tmpl, { K_ASSISTANT, K_BEGIN });
        std::string assistantPrefix = get_value<std::string>(tmpl, { K_ASSISTANT, K_PREFIX });
        std::string assistantSuffix = get_value<std::string>(tmpl, { K_ASSISTANT, K_SUFFIX });
        std::string assistantEnd = get_value<std::string>(tmpl, { K_ASSISTANT, K_END });

        std::string reversePrompt = get_value<std::string>(tmpl, { K_REVERSE_PROMPT });

        bool systemHasSuffix = get_value<bool>(tmpl, { K_SYSTEMUSER_SYSTEM_HAS_SUFFIX });
        bool systemHasEnd = get_value<bool>(tmpl, { K_SYSTEMUSER_SYSTEM_HAS_END });
        bool userHasBegin = get_value<bool>(tmpl, { K_SYSTEMUSER_1ST_USER_HAS_BEGIN });
        bool userHasPrefix = get_value<bool>(tmpl, { K_SYSTEMUSER_1ST_USER_HAS_PREFIX });

        ss << msgTag << ":" + tmpl + ":" << "global-begin" << ":" << globalBegin << std::endl;
        ss << msgTag << ":" + tmpl + ":" << "global-end" << ":" << globalEnd << std::endl;
        ss << msgTag << ":" + tmpl + ":" << "system-begin" << ":" << systemBegin << std::endl;
        ss << msgTag << ":" + tmpl + ":" << "system-prefix" << ":" << systemPrefix << std::endl;
        ss << msgTag << ":" + tmpl + ":" << "system-suffix" << ":" << systemSuffix << std::endl;
        ss << msgTag << ":" + tmpl + ":" << "system-end" << ":" << systemEnd << std::endl;
        ss << msgTag << ":" + tmpl + ":" << "user-begin" << ":" << userBegin << std::endl;
        ss << msgTag << ":" + tmpl + ":" << "user-prefix" << ":" << userPrefix << std::endl;
        ss << msgTag << ":" + tmpl + ":" << "user-suffix" << ":" << userSuffix << std::endl;
        ss << msgTag << ":" + tmpl + ":" << "user-end" << ":" << userEnd << std::endl;
        ss << msgTag << ":" + tmpl + ":" << "assistant-begin" << ":" << assistantBegin << std::endl;
        ss << msgTag << ":" + tmpl + ":" << "assistant-prefix" << ":" << assistantPrefix << std::endl;
        ss << msgTag << ":" + tmpl + ":" << "assistant-suffix" << ":" << assistantSuffix << std::endl;
        ss << msgTag << ":" + tmpl + ":" << "assistant-end" << ":" << assistantEnd << std::endl;
        ss << msgTag << ":" + tmpl + ":" << K_REVERSE_PROMPT << ":" << reversePrompt << std::endl;
        ss << msgTag << ":" + tmpl + ":" << K_SYSTEMUSER_SYSTEM_HAS_SUFFIX << ":" << systemHasSuffix << std::endl;
        ss << msgTag << ":" + tmpl + ":" << K_SYSTEMUSER_SYSTEM_HAS_END << ":" << systemHasEnd << std::endl;
        ss << msgTag << ":" + tmpl + ":" << K_SYSTEMUSER_1ST_USER_HAS_BEGIN << ":" << userHasBegin << std::endl;
        ss << msgTag << ":" + tmpl + ":" << K_SYSTEMUSER_1ST_USER_HAS_PREFIX << ":" << userHasPrefix << std::endl;

        if (!userEnd.empty()) {
            LOG_TEELN("WARN:CT:%s:User-End seems to be set to [%s], do cross check if this is proper and needed", msgTag.c_str(), userEnd.c_str());
        }
        if (!assistantBegin.empty()) {
            LOG_TEELN("WARN:CT:%s:Assistant-Begin seems to be set to [%s], do cross check if this is proper and needed", msgTag.c_str(), assistantBegin.c_str());
        }

        return true;
    }

    /**
     * For the specified chat-template, get the value associated with the specified key/field.
    */
    template <typename SupportedDataType>
    SupportedDataType tmpl_getkey(const std::string &tmpl, const std::string &key, const SupportedDataType &defaultValue) {
        return get_value(tmpl, {key}, defaultValue, "CTTmplGetKey");
    }

    /**
     * For the specified chat-template and the role within, cumulate the values of the specified keys/fields
     * and return the same.
    */
    std::string tmpl_role_getkeys(const std::string &tmpl, const std::string &role, const std::vector<std::string> &keys) {
        std::string got = "";
        std::string sKeys = "";
        for(auto key: keys) {
            got += get_value<std::string>(tmpl, {role, key}, "", "CTTmplRoleGetKeys");
            sKeys += "+";
            sKeys += key;
        }
        LDBUG_LN("DBUG:CT:%s:%s:%s:%s:%s", __func__, tmpl.c_str(), role.c_str(), sKeys.c_str(), got.c_str());
        return got;
    }

    /**
     * Given the template model/standard id and a bunch of messages including their roles,
     * this returns tagged messages, subPartsTypes string and subPartsLens vector.
     * The returned subParts types string and lens vector help identify the parts of the
     * tagged msgs string, which relate to passed msgs and added tags.
     * 
     * * a string containing the tagged messages
     *   [global-begin] + 1 or more [[role-begin] + [role-prefix] + msg + [role-suffix] +[role-end]] + [global-end]
     * * a string where the chars contain info about
     *   type of sub-strings/parts that make up the tagged messages string.
     * * a vector of ints,
     *   which give the length of each part in the tagged messages string.
     * 
     * If a combination of system-user messages is passed, then tags between the 1st system and
     * the 1st user message, is based on the flags set wrt the corresponding template standard.
     * If you dont want this behaviour, pass non 0 values wrt the optional cntSystemMsgCnt and
     * cntUserMsgCnt arguments.
    */
    bool chaton_tmpl_apply_ex(
            const std::string &tmpl,
            const std::vector<const llama_chat_message *> &msgs,
            bool alertAssistantAtEnd,
            bool applyGlobalIfAny,
            std::string &tagged,
            std::string &types,
            std::vector<int32_t> &lens,
            int curSystemMsgCnt = 0,
            int curUserMsgCnt = 0
            ) {
        if (!tmpl_exists(tmpl)) {
            return false;
        }
        ChatParts cp = {};
        if (applyGlobalIfAny) {
            std::string globalBegin = tmpl_role_getkeys(tmpl, K_GLOBAL, {K_BEGIN});
            cp.add_part(ChatParts::S, globalBegin);
        }
        int cntSystem = curSystemMsgCnt;
        int cntUser = curUserMsgCnt;
        int cntOthers = 0;
        for(const auto msg: msgs) {
            auto role = msg->role;
            auto content = msg->content;
            std::string begin = tmpl_role_getkeys(tmpl, role, {K_BEGIN});
            auto prefix = tmpl_role_getkeys(tmpl, role, {K_PREFIX});
            auto suffix = tmpl_role_getkeys(tmpl, role, {K_SUFFIX});
            auto end = tmpl_role_getkeys(tmpl, role, {K_END});
            if (role == K_SYSTEM) {
                cntSystem += 1;
                cp.add_part(ChatParts::S, begin);
                cp.add_part(ChatParts::S, prefix);
            } else if (role == K_USER) {
                cntUser += 1;
                if ((cntSystem == 1) && (cntUser == 1)) {
                    if (tmpl_getkey(tmpl, K_SYSTEMUSER_1ST_USER_HAS_BEGIN, true)) {
                        cp.add_part(ChatParts::S, begin);
                    }
                    if (tmpl_getkey(tmpl, K_SYSTEMUSER_1ST_USER_HAS_PREFIX, true)) {
                        cp.add_part(ChatParts::S, prefix);
                    }
                } else {
                    cp.add_part(ChatParts::S, begin);
                    cp.add_part(ChatParts::S, prefix);
                }
            } else {
                cntOthers += 1;
                cp.add_part(ChatParts::S, begin);
                cp.add_part(ChatParts::S, prefix);
            }
            cp.add_part(ChatParts::N, content);
            if (role == K_SYSTEM) {
                if (cntSystem == 1) {
                    if (tmpl_getkey(tmpl, K_SYSTEMUSER_SYSTEM_HAS_SUFFIX, true)) {
                        cp.add_part(ChatParts::S, suffix);
                    }
                    if (tmpl_getkey(tmpl, K_SYSTEMUSER_SYSTEM_HAS_END, true)) {
                        cp.add_part(ChatParts::S, end);
                    }
                } else {
                    cp.add_part(ChatParts::S, suffix);
                    cp.add_part(ChatParts::S, end);
                }
            } else {
                cp.add_part(ChatParts::S, suffix);
                cp.add_part(ChatParts::S, end);
            }
        }
        if (alertAssistantAtEnd) {
            auto assistantBeginPrefix = tmpl_role_getkeys(tmpl, K_ASSISTANT, {K_BEGIN, K_PREFIX});
            cp.add_part(ChatParts::S, assistantBeginPrefix);
        }
        if (applyGlobalIfAny) {
            auto globalEnd = tmpl_role_getkeys(tmpl, K_GLOBAL, {K_END});
            cp.add_part(ChatParts::S, globalEnd);
        }
        LDBUG_LN("DBUG:CT:%s", cp.dump("INFO:ChatOnTmplApplyEx").c_str());
        tagged = cp.str();
        LDBUG_LN("DBUG:CT:%s:%s:%s", __func__, tmpl.c_str(), tagged.c_str());
        LDBUG_LN("DBUG:CT:%s:CntSys[%d]:CntUsr[%d]:CntOthers[%d]", __func__, cntSystem, cntUser, cntOthers);
        types = cp.get_partstypes();
        lens = cp.get_partslens();
        return true;
    }

};

// The compiled-in configurable template data (the meta)
#include "chaton_meta.hpp"
//ChatTemplates gCT = {{}};


inline bool chaton_tmpl_exists(const std::string &tmpl) {
    return gCT.tmpl_exists(tmpl);
}

inline std::string chaton_tmpl_role_getkeys(const std::string &tmpl, const std::string &role, const std::vector<std::string> &keys) {
    return gCT.tmpl_role_getkeys(tmpl, role, keys);
}

inline std::string chaton_tmpl_getkey_str(const std::string &tmpl, const std::string &key) {
    return gCT.tmpl_getkey<std::string>(tmpl, {key}, "");
}

inline bool chaton_tmpl_getkey_bool(const std::string &tmpl, const std::string &key) {
    return gCT.tmpl_getkey<bool>(tmpl, {key}, false);
}


// Given the template standard and a bunch of messages including their roles, this returns
// the tagged messages as a string.
// global-begin + 1 or more [[role-begin] + [role-prefix] + msg + [role-suffix] +[role-end]] + global-end
//
// Additionally also return info about the parts that make up the tagged message.
inline bool chaton_tmpl_apply_ex(
        const std::string &tmpl,
        const std::vector<const llama_chat_message *> &msgs,
        bool alertAssistantAtEnd,
        bool applyGlobalIfAny,
        std::string &tagged,
        std::string &types,
        std::vector<int32_t> &lens,
        int curSystemMsgCnt = 0,
        int curUserMsgCnt = 0
        ) {
    return gCT.chaton_tmpl_apply_ex(tmpl, msgs, alertAssistantAtEnd, applyGlobalIfAny, tagged, types, lens, curSystemMsgCnt, curUserMsgCnt);
}

// Given the template standard and a bunch of messages including their roles, this returns
// the tagged messages as a string.
// global-begin + 1 or more [[role-begin] + [role-prefix] + msg + [role-suffix] +[role-end]] + global-end
inline int32_t chaton_tmpl_apply(
        const std::string &tmpl,
        const std::vector<const llama_chat_message *> &msgs,
        bool alertAssistantAtEnd,
        bool applyGlobalIfAny,
        std::string &tagged
        ) {
    std::string types;
    std::vector<int32_t> lens;
    if (!chaton_tmpl_apply_ex(tmpl, msgs, alertAssistantAtEnd, applyGlobalIfAny, tagged, types, lens)) {
        return -1;
    }
    return tagged.size();
}

const int BYPASS_MSGCNT = 101;
//
// Given the template standard, role and a message, this creates the tagged message.
//
// string containing the tagged message
// * role-(begin+prefix) + msg + role-(suffix+end)
//
// ALERT: This currently assumes/behaves as if the system or user message it is working on
// is a non-1st message belonging to that role.
//
inline size_t chaton_tmpl_apply_single(
        const std::string &tmpl,
        const std::string &role,
        const std::string &content,
        bool alertAssistantAtEnd,
        bool applyGlobalIfAny,
        std::string &tagged
        ) {
    std::string types;
    std::vector<int32_t> lens;
    llama_chat_message cm {role.c_str(), content.c_str()};
    if (!chaton_tmpl_apply_ex(tmpl, {&cm}, alertAssistantAtEnd, applyGlobalIfAny, tagged, types, lens, BYPASS_MSGCNT, BYPASS_MSGCNT)) {
        return -1;
    }
    return tagged.size();
}

// Given the template standard and a bunch of messages including their roles, this returns
// the tagged messages as a string.
// global-begin + 1 or more [[role-begin] + [role-prefix] + msg + [role-suffix] +[role-end]] + global-end
//
// If the passed char array is smaller than that required for the tagged messages string,
// * part of the tagged messages string which fits within dest buffer is copied
// * the returned value, indicates the size of the actual tagged message
//
// NOTE:
// * ideally the passed char array should be able to fit the tagged messages string + 0|null char.
// * if the return value from this function is larger than or equal to destLength,
//   then you will have to increase the size of the dest buffer, and call this
//   function a second time, to ensure that one gets the full tagged messages string.
inline int32_t chaton_tmpl_apply_capi(
        const char *tmpl,
        const struct llama_chat_message *msgs,
        const size_t numMsgs,
        bool alertAssistantAtEnd,
        char *dest,
        int32_t destLength
        ) {
    if ((tmpl == nullptr) || (dest == nullptr)) {
        return -1;
    }
    std::vector<const llama_chat_message *> vMsgs;
    for(size_t i=0; i<numMsgs; i++) {
        vMsgs.push_back(&msgs[i]);
    }
    std::string taggedMsgs;
    int32_t taggedLength = chaton_tmpl_apply(tmpl, vMsgs, alertAssistantAtEnd, true, taggedMsgs);
    if (taggedLength < 0) {
        return taggedLength;
    }
    if (destLength > 0) {
        strlcpy(dest, taggedMsgs.c_str(), destLength);
    }
    return taggedLength;
}

//
// In addition to the semantic provided by chaton_tmpl_apply_capi
// this additionally also returns info about the parts that make up
// the returned tagged message.
//
// partsTypes and partsLengths should be arrays that can accomodate the
// same number of elements belonging to its respective type.
// Inturn the pNumParts should point to a int which specifies the
// number of elements.
// If the generated tagged message has more parts than the specified
// *pNumParts, then the logic copies partsTypes and partsLengths to the
// specified length/NumOfParts only. Parallely it updates *pNumParts
// to the actual needed length (not including any terminating null char or so).
//
inline int32_t chaton_tmpl_apply_ex_capi(
        const char *tmpl,
        const struct llama_chat_message *msgs,
        const size_t numMsgs,
        bool alertAssistantAtEnd,
        char *dest,
        int32_t destLength,
        char *partsTypes,
        int32_t *partsLengths,
        int32_t *pNumParts
        ) {
    if ((tmpl == nullptr) || (dest == nullptr) || (pNumParts == nullptr)) {
        return -1;
    }
    std::vector<const llama_chat_message *> vMsgs;
    for(size_t i=0; i<numMsgs; i++) {
        vMsgs.push_back(&msgs[i]);
    }
    std::string taggedMsgs;
    std::string types;
    std::vector<int32_t> lens;
    if (!chaton_tmpl_apply_ex(tmpl, vMsgs, alertAssistantAtEnd, true, taggedMsgs, types, lens)) {
        return -1;
    }
    int32_t taggedLength = taggedMsgs.size();
    if (taggedLength < 0) {
        return taggedLength;
    }
    if (destLength > 0) {
        strlcpy(dest, taggedMsgs.c_str(), destLength);
    }
    if (*pNumParts > 0) {
        if (partsTypes != nullptr) {
            strlcpy(partsTypes, types.c_str(), *pNumParts);
        }
        if (partsLengths != nullptr) {
            memcpy(partsLengths, lens.data(), (*pNumParts)*sizeof(int32_t));
        }
    }
    *pNumParts = types.length();
    return taggedLength;
}

// Copied from common.cpp, updated wrt model and logging flow.
inline std::vector<llama_token> chaton_llama_tokenize(
    const struct llama_model * model,
           const std::string & text,
                        bool   add_special,
                        bool   parse_special) {
    LDBUG_LN("DBUG:%s:%s:special[add:%d, parse:%d]", __func__, text.c_str(), add_special, parse_special);
    if (model == nullptr) {
        LOG_TEELN("ERRR:%s:Model NOT Provided:%s:special[add:%d, parse:%d]", __func__, text.c_str(), add_special, parse_special);
        return std::vector<llama_token>{};
    }
    // upper limit for the number of tokens
    int n_tokens = text.length() + 2 * add_special;
    std::vector<llama_token> result(n_tokens);
    n_tokens = llama_tokenize(model, text.data(), text.length(), result.data(), result.size(), add_special, parse_special);
    if (n_tokens < 0) {
        result.resize(-n_tokens);
        int check = llama_tokenize(model, text.data(), text.length(), result.data(), result.size(), add_special, parse_special);
        GGML_ASSERT(check == -n_tokens);
    } else {
        result.resize(n_tokens);
    }
    return result;
}

// Tokenize the passed taggedText, keeping in mind the subparts within and
// inturn whether to parse special tokens in them or not (partsTypes).
// If you want to parse special tokens in the taggedText, independent of what
// partsTypes specifies, then set forceParseSpecial to true.
inline std::vector<llama_token> chaton_llama_tokenize_ex(
        const struct llama_model *model,
        const std::string &taggedText,
        const std::string &partsTypes,
        const std::vector<int32_t> &partsLengths,
        bool addSpecial,
        bool forceParseSpecial
        ) {
    std::vector<llama_token> tokens;
    int iPart = 0;
    int iStart = 0;
    for(auto partLen: partsLengths) {
        auto partType = partsTypes[iPart];
        iPart += 1;
        auto msgPart = taggedText.substr(iStart, partLen);
        iStart += partLen;
        auto parseSpecial = partType == ChatParts::S ? true : false;
        parseSpecial |= forceParseSpecial;
        auto curTokens = chaton_llama_tokenize(model, msgPart, addSpecial, parseSpecial);
        tokens.insert(tokens.end(), curTokens.begin(), curTokens.end());
    }
    return tokens;
}


/**
 * Validate specified chaton-template-id and inturn dump the contents related to that
 * specific chat-handshake-template-standard, wrt the specified ChatTemplates.
 * If ct is nullptr, then map to the compiled-in ChatTemplates global instance.
 * 
 * ALERT: If no template-id is specified, it is ignored with a warning.
 * NOTE: It optionally dumps the full loaded chaton templates data
 * NOTE: It uses tmpl_basiccheck, which raises exception, if all the required
 * keys/fields are not present wrt the specified template-standard/model-id.
 */
inline bool _chaton_meta_validate_dump(std::string &tmpl, ChatTemplates *ct=nullptr) {
    if (ct == nullptr) {
        ct = &gCT;
    }
    LDBUG_LN("\n\nINFO:%s:%s:\n%s", __func__, tmpl.c_str(), ct->dump("", "INFO:ChatOnMetaValidateDump").c_str());
    if (tmpl.empty()) {
        return true;
    }
    std::stringstream ss;
    if (ct->tmpl_basiccheck(tmpl, ss, "INFO:ChatOnMetaValidateDump")) {
        LOGXLN("%s", ss.str().c_str());
    } else {
        return false;
    }
    return true;
}

/**
 * In the passed ChatTemplates instance, verify that specified chaton-template-id
 * contains required fields using meta-validate-dump.
 * If ct is nullptr, then map to the compiled-in ChatTemplates global instance.
 */
inline bool chaton_meta_ok(std::string &tmpl, ChatTemplates *ct=nullptr) {
    return _chaton_meta_validate_dump(tmpl, ct);
}
