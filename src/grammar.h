#pragma once

#include "nametbl.h"

#include <vector>

const int idEnd = 0x000;
const int idEmpty = 0x100;
const int idDefault = 0x101;
const int idError = 0x102;

const int maskNonterm = 0x1000;
const int maskAction = 0x2000;
const int maskId = 0x0FFF;

const int maskLeftAssoc = 0x1000;
const int maskRightAssoc = 0x2000;
const int maskPrec = 0x0FFF;

struct Grammar {
    int token_count = 0;
    int nonterm_count = 0;
    int action_count = 0;
    std::vector<bool> token_used;
    std::vector<int> grammar;
    std::vector<int> grammar_idx;
    std::vector<int> act_on_reduce;
    std::vector<int> token_prec;
    std::vector<int> prod_prec;
    NameTable name_tbl;
    std::vector<std::pair<std::string_view, int>> getTokenList();
    std::vector<std::pair<std::string_view, int>> getActionList();
    void printProduction(std::ostream& outp, int prod_no, int pos = -1) const;
    void printTokens(std::ostream& outp) const;
    void printNonterms(std::ostream& outp) const;
    void printActions(std::ostream& outp) const;
    void printGrammar(std::ostream& outp) const;
    [[nodiscard]] std::string symbolText(unsigned id) const;
    [[nodiscard]] std::string decoratedSymbolText(unsigned id) const;
};
