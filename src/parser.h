#pragma once

#include "grammar.h"
#include "logger.h"

#include <unordered_map>
#include <variant>
#include <vector>

enum {
    tt_eof = 256,
    tt_symb,
    tt_id,
    tt_predef_id,
    tt_internal_id,
    tt_token_id,
    tt_action_id,
    tt_string,
    tt_start,
    tt_token,
    tt_action,
    tt_option,
    tt_left,
    tt_right,
    tt_nonassoc,
    tt_prec,
    tt_sep,
    tt_unterm_token,
};

namespace lex_detail {
#include "lex_defs.h"
}

// Input file parser class
class Parser {
 public:
    Parser(uxs::iobuf& input, std::string file_name, Grammar& grammar);
    bool parse();
    const std::string& getFileName() const { return file_name_; }
    const std::string& getCurrentLine() const { return current_line_; }

 private:
    using TokenVal = std::variant<unsigned, std::string_view>;

    struct TokenInfo {
        TokenVal val;
        TokenLoc loc;
    };

    uxs::iobuf& input_;
    std::string file_name_;
    std::unique_ptr<char[]> text_;
    std::string current_line_;
    char* first_ = nullptr;
    char* last_ = nullptr;
    unsigned ln_ = 1, col_ = 1;
    uxs::basic_inline_dynbuffer<int, 1> state_stack_;
    TokenInfo tkn_;
    Grammar& grammar_;
    std::unordered_map<std::string_view, std::string_view> options_;

    int lex();
    void logSyntaxError(int tt) const;
};
