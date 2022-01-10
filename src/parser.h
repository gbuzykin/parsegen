#pragma once

#include "grammar.h"
#include "logger.h"

#include <iostream>
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
    Parser(std::istream& input, std::string file_name, Grammar& grammar);
    bool parse();
    const std::string& getFileName() const { return file_name_; }
    const std::string& getCurrentLine() const { return current_line_; }

 private:
    using TokenVal = std::variant<unsigned, std::string_view>;

    struct TokenInfo {
        TokenVal val;
        TokenLoc loc;
    };

    struct InputContext {
        const char* first = nullptr;
        const char* last = nullptr;
        unsigned ln = 1, col = 1;
    };

    std::istream& input_;
    std::string file_name_;
    std::unique_ptr<char[]> text_;
    std::string current_line_;
    char* text_top_ = nullptr;
    InputContext in_ctx_;
    std::vector<int> lex_state_stack_;
    TokenInfo tkn_;
    Grammar& grammar_;
    std::unordered_map<std::string_view, std::string_view> options_;

    static int dig(char ch) { return static_cast<int>(ch - '0'); }
    static int hdig(char ch) {
        if ((ch >= 'a') && (ch <= 'f')) { return static_cast<int>(ch - 'a') + 10; }
        if ((ch >= 'A') && (ch <= 'F')) { return static_cast<int>(ch - 'A') + 10; }
        return static_cast<int>(ch - '0');
    }

    int lex();
    void logSyntaxError(int tt) const;
};
