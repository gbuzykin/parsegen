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

struct TokenLoc {
    unsigned n_line = 0;
    unsigned n_col = 0;
};

class Parser;

class Log {
 public:
    enum class MsgType { kDebug = 0, kInfo, kWarning, kError, kFatal };

    explicit Log(MsgType type) : type_(type) {}
    Log(MsgType type, const Parser* parser) : type_(type), parser_(parser) {}
    Log(MsgType type, const Parser* parser, const TokenLoc& l) : type_(type), parser_(parser), loc_(l) {}
    ~Log() { printMessage(type_, loc_, ss_.str()); }
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;

    template<typename Ty>
    Log& operator<<(const Ty& v) {
        ss_ << v;
        return *this;
    }
    void printMessage(MsgType type, const TokenLoc& l, const std::string& msg);
    operator int() const { return -1; }

 private:
    MsgType type_ = MsgType::kDebug;
    const Parser* parser_ = nullptr;
    TokenLoc loc_;
    std::stringstream ss_;
};

// Input file parser class
class Parser {
 public:
    Parser(std::istream& input, std::string file_name, Grammar& grammar);
    int parse();
    std::string_view getFileName() const { return file_name_; }
    std::string_view getCurrentLine() const { return current_line_; }

 private:
    using TokenVal = std::variant<unsigned, std::string_view>;

    struct TokenInfo {
        TokenVal val;
        TokenLoc loc;
    };

    std::istream& input_;
    std::string file_name_;
    std::unique_ptr<char[]> text_;
    std::string current_line_;
    unsigned n_line_ = 1, n_col_ = 1;
    lex_detail::CtxData lex_ctx_;
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
    Log logWarning() const { return Log(Log::MsgType::kWarning, this, tkn_.loc); }
    Log logError() const { return Log(Log::MsgType::kError, this, tkn_.loc); }
    int logSyntaxError(int tt) const;
};
