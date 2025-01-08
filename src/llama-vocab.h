#pragma once

#include "llama.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <set>
#include <memory>

struct LLM_KV;
struct llama_model_loader;

struct llama_vocab {
    struct token_data {
        std::string      text;
        float            score;
        llama_token_attr attr;
    };

    uint32_t n_vocab = 0; // TODO: not great because has to keep in sync with hparams.n_vocab

    llama_vocab();
    ~llama_vocab();

    void load(llama_model_loader & ml, const LLM_KV & kv);

    enum llama_vocab_type     get_type()     const;
    enum llama_vocab_pre_type get_pre_type() const;

    std::string type_name() const;

    bool is_normal      (llama_token id) const;
    bool is_unknown     (llama_token id) const;
    bool is_control     (llama_token id) const;
    bool is_byte        (llama_token id) const;
    bool is_user_defined(llama_token id) const;
    bool is_unused      (llama_token id) const;
    bool is_eog         (llama_token id) const;

    uint8_t     token_to_byte(llama_token id) const;
    llama_token byte_to_token(uint8_t ch)     const;

    llama_token text_to_token(const std::string & text) const;

    const token_data & get_token_data(llama_token id) const;

    const char *     token_get_text (llama_token id) const;
    float            token_get_score(llama_token id) const;
    llama_token_attr token_get_attr (llama_token id) const;

    llama_token token_bos() const;
    llama_token token_eos() const;
    llama_token token_eot() const;
    llama_token token_eom() const;
    llama_token token_unk() const;
    llama_token token_cls() const;
    llama_token token_sep() const;
    llama_token token_nl () const;
    llama_token token_pad() const;

    llama_token token_prefix() const;
    llama_token token_middle() const;
    llama_token token_suffix() const;

    llama_token token_fim_pre() const;
    llama_token token_fim_suf() const;
    llama_token token_fim_mid() const;
    llama_token token_fim_pad() const;
    llama_token token_fim_rep() const;
    llama_token token_fim_sep() const;

    bool add_space_prefix          () const;
    bool add_bos_token             () const;
    bool add_eos_token             () const;
    bool ignore_merges             () const;
    bool clean_spaces              () const;
    bool remove_extra_whitespaces  () const;
    bool escape_whitespaces        () const;
    bool treat_whitespace_as_suffix() const;

    int max_token_text_len() const;

    void print_info() const;

    int find_bpe_rank(const std::string & token_left, const std::string & token_right) const;

    std::vector<llama_token> tokenize(
                  std::string   raw_text,
                         bool   add_special,
                         bool   parse_special = false) const;

    int32_t tokenize(
                   const char * text,
                      int32_t   text_len,
                  llama_token * tokens,
                      int32_t   n_tokens_max,
                         bool   add_special,
                         bool   parse_special) const;

    // does not write null-terminator to buf
    int32_t token_to_piece(
                  llama_token   token,
                         char * buf,
                      int32_t   length,
                      int32_t   lstrip,
                         bool   special) const;

    // use cached data
    const std::string & token_to_piece(llama_token token) const;

    // check if token0 is contained as a prefix in token1
    bool token_is_prefix(
                  llama_token   token0,
                  llama_token   token1) const;

    int32_t detokenize(
            const llama_token * tokens,
                      int32_t   n_tokens,
                         char * text,
                      int32_t   text_len_max,
                         bool   remove_special,
                         bool   unparse_special) const;

    std::string detokenize(
            const std::vector<llama_token> & tokens,
                                      bool   special) const;

private:
    struct impl;
    std::unique_ptr<impl> pimpl;

    std::string token_to_piece_for_cache(
                  llama_token   token,
                         bool   special) const;

    enum llama_vocab_type     type     = LLAMA_VOCAB_TYPE_SPM;
    enum llama_vocab_pre_type pre_type = LLAMA_VOCAB_PRE_TYPE_DEFAULT;

    int max_token_len = 0; // used for optimizing longest token search

    // default LLaMA special tokens
    // TODO: should we set all of these to LLAMA_TOKEN_NULL?
    llama_token special_bos_id  = 1;
    llama_token special_eos_id  = 2;
    llama_token special_eot_id  = LLAMA_TOKEN_NULL;
    llama_token special_eom_id  = LLAMA_TOKEN_NULL;
    llama_token special_unk_id  = 0;
    llama_token special_sep_id  = LLAMA_TOKEN_NULL;
    llama_token special_pad_id  = LLAMA_TOKEN_NULL;
    llama_token special_cls_id  = LLAMA_TOKEN_NULL; // TODO: revisit if this is really needed https://github.com/ggerganov/llama.cpp/pull/10930
    llama_token special_mask_id = LLAMA_TOKEN_NULL;

    llama_token linefeed_id = 13;

    // fim tokens
    llama_token special_fim_pre_id = LLAMA_TOKEN_NULL;
    llama_token special_fim_suf_id = LLAMA_TOKEN_NULL;
    llama_token special_fim_mid_id = LLAMA_TOKEN_NULL;
    llama_token special_fim_pad_id = LLAMA_TOKEN_NULL;
    llama_token special_fim_rep_id = LLAMA_TOKEN_NULL; // repo
    llama_token special_fim_sep_id = LLAMA_TOKEN_NULL; // file separator

    // tokenizer flags
    bool tokenizer_add_space_prefix           = false;
    bool tokenizer_add_bos                    = false;
    bool tokenizer_add_eos                    = false;
    bool tokenizer_ignore_merges              = false;
    bool tokenizer_clean_spaces               = false;  // clean_up_tokenization_spaces
    bool tokenizer_remove_extra_whitespaces   = false;
    bool tokenizer_escape_whitespaces         = true;
    bool tokenizer_treat_whitespace_as_suffix = false;
};
