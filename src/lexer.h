#pragma once

#include <iostream>
#include <vector>

const int tt_act = 256;
const int tt_int = 257;
const int tt_string = 258;
const int tt_symb = 259;
const int tt_id = 260;
const int tt_token = 261;
const int tt_action = 262;
const int tt_option = 263;
const int tt_left = 264;
const int tt_right = 265;
const int tt_nonassoc = 266;
const int tt_prec = 267;
const int tt_sep = 268;

union LValType {
    int i;
    char* str;
};

// Lexer class
class Lexer {
 public:
// Include constant definitions
#include "lex_def.h"

    Lexer(std::istream* in = 0);

    std::istream* switchStream(std::istream* in = 0);

    int lex();
    void pushStartCondition(int sc) {
        sc_stack_.push_back(sc_);
        sc_ = sc;
    };
    int getStartCondition() const { return sc_; };
    bool popStartCondition() {
        if (sc_stack_.size() == 0) return false;
        sc_ = sc_stack_.back();
        sc_stack_.pop_back();
        return true;
    };

 public:
    const LValType& getLVal() { return lval_; };
    int getLineNo() const { return line_no_; };
    void setLineNo(int line_no) { line_no_ = line_no; };

 private:
    static int symb_to_idx_[];
    static int def_[];
    static int base_[];
    static int next_[];
    static int check_[];
    static int accept_list_[];
    static int accept_idx_[];

    std::istream* input_;
    int sc_, state_;
    std::vector<int> sc_stack_;
    std::vector<int> state_stack_;
    std::vector<char> text_;
    int line_no_;
    LValType lval_;
    std::vector<char> str_;  // String token

    bool onPatternMatched(int, int&);
    const char* getText() const { return &text_[0]; };
    int getLeng() const { return (int)(text_.size() - 1); };
    void reset() {
        state_ = sc_;
        state_stack_.clear();
        text_.clear();
    };
    int getChar() {
        char ch = 0;
        input_->get(ch);
        return ((int)ch & 0xFF);
    };
    void ungetChar() { input_->unget(); };

    static int str_to_int(const char*);
    static inline int dig(char ch) { return (int)(ch - '0'); };
    static inline int hdig(char ch) {
        if ((ch >= 'a') && (ch <= 'f')) return 10 + (int)(ch - 'a');
        if ((ch >= 'A') && (ch <= 'F')) return 10 + (int)(ch - 'A');
        return dig(ch);
    };
};
