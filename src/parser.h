#pragma once

#include "grammar.h"

#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace detail {
template<typename InputIt>
InputIt from_utf8(InputIt in, InputIt in_end, uint32_t* pcode) {
    if (in >= in_end) { return in; }
    uint32_t code = static_cast<uint8_t>(*in);
    if ((code & 0xC0) == 0xC0) {
        static const uint32_t mask_tbl[] = {0xFF, 0x1F, 0xF, 0x7};
        static const uint32_t count_tbl[] = {1, 1, 1, 1, 2, 2, 3, 0};
        uint32_t count = count_tbl[(code >> 3) & 7];  // continuation byte count
        if (in_end - in <= count) { return in; }
        code &= mask_tbl[count];
        while (count > 0) {
            code = (code << 6) | ((*++in) & 0x3F);
            --count;
        }
    }
    *pcode = code;
    return ++in;
}
}  // namespace detail

enum {
    tt_eof = 0,
    tt_symb = 256,
    tt_id,
    tt_predef_id,
    tt_internal_id,
    tt_token_id,
    tt_action_id,
    tt_string,
    tt_token,
    tt_action,
    tt_option,
    tt_left,
    tt_right,
    tt_nonassoc,
    tt_prec,
    tt_sep,
    tt_unterminated_token,
};

namespace lex_detail {
#include "lex_defs.h"
}

// Input file parser class
class Parser {
 public:
    Parser(std::istream& input, std::string file_name, Grammar& grammar);
    int parse();

 private:
    using TokenVal = std::variant<unsigned, std::string_view>;

    struct ErrorLogger {
        Parser* parser;
        std::stringstream ss;
        explicit ErrorLogger(Parser* in_parser) : parser(in_parser) {}
        ErrorLogger(ErrorLogger&& el) noexcept : parser(el.parser) { el.parser = nullptr; }
        ~ErrorLogger() {
            if (parser) { parser->printError(ss.str()); }
        }
        ErrorLogger(const ErrorLogger&) = delete;
        ErrorLogger& operator=(const ErrorLogger&) = delete;
        ErrorLogger& operator=(ErrorLogger&&) = delete;
        template<typename Ty>
        ErrorLogger& operator<<(const Ty& v) {
            ss << v;
            return *this;
        }
        operator int() const { return -1; }
    };

    std::istream& input_;
    std::string file_name_;
    std::unique_ptr<char[]> text_;
    std::string current_line_;
    unsigned n_line_ = 1, n_col_ = 1;
    lex_detail::CtxData lex_ctx_;
    std::vector<int> lex_state_stack_;
    TokenVal tkn_val_;
    unsigned tkn_col_ = 0;
    Grammar& grammar_;
    std::unordered_map<std::string_view, std::string_view> options_;

    static int dig(char ch) { return static_cast<int>(ch - '0'); }
    static int hdig(char ch) {
        if ((ch >= 'a') && (ch <= 'f')) { return static_cast<int>(ch - 'a') + 10; }
        if ((ch >= 'A') && (ch <= 'F')) { return static_cast<int>(ch - 'A') + 10; }
        return static_cast<int>(ch - '0');
    }

    int lex();
    int logSyntaxError(int tt);
    ErrorLogger logError() { return ErrorLogger(this); }
    void printError(const std::string& msg);
};
