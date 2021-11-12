#pragma once

#include "grammar.h"

#include <iostream>
#include <unordered_map>
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

// Input file parser class
class Parser {
 public:
// Include constant definitions
#include "lex_def.h"

    Parser(std::istream& input, Grammar& grammar) : input_(input), grammar_(grammar) {}
    int parse();
    const std::string& getErrorString() const { return err_string_; };

 private:
    static int symb_to_idx_[];
    static int def_[];
    static int base_[];
    static int next_[];
    static int check_[];
    static int accept_list_[];
    static int accept_idx_[];

    std::istream& input_;
    int sc_ = sc_initial;
    int state_ = sc_initial;
    std::vector<int> sc_stack_;
    std::vector<int> state_stack_;
    std::vector<char> text_;
    int line_no_ = 1;
    LValType lval_;
    std::vector<char> str_;  // String token
    Grammar& grammar_;
    std::string err_string_;

    std::unordered_map<std::string, std::string> options_;

    int lex();
    void pushStartCondition(int sc) {
        sc_stack_.push_back(sc_);
        sc_ = sc;
    };
    bool popStartCondition() {
        if (sc_stack_.size() == 0) return false;
        sc_ = sc_stack_.back();
        sc_stack_.pop_back();
        return true;
    };

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
        input_.get(ch);
        return ((int)ch & 0xFF);
    };
    void ungetChar() { input_.unget(); };

    int errorSyntax(int);
    int errorNameRedef(int, const std::string&);
    int errorLeftPartIsNotNonterm(int);
    int errorUndefAction(int, const std::string&);
    int errorUndefNonterm(const std::string&);
    int errorUnusedProd(const std::string&);
    int errorPrecRedef(int, int);
    int errorUndefToken(int, const std::string&);
    int errorUndefPrec(int, int);
    int errorInvUseOfPredefToken(int, int);
    int errorInvOption(int, const std::string&);
    int errorInvUseOfActName(int, const std::string&);

    static int str_to_int(const char*);
    static inline int dig(char ch) { return (int)(ch - '0'); };
    static inline int hdig(char ch) {
        if ((ch >= 'a') && (ch <= 'f')) return 10 + (int)(ch - 'a');
        if ((ch >= 'A') && (ch <= 'F')) return 10 + (int)(ch - 'A');
        return dig(ch);
    };
};
