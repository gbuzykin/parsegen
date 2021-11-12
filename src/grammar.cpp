#include "grammar.h"

#include <algorithm>
#include <cassert>
#include <ostream>

std::vector<std::pair<std::string_view, int>> Grammar::getTokenList() {
    std::vector<std::pair<std::string_view, int>> lst;
    for (int id = 0x100; id < token_count; ++id) {
        std::string_view text = name_tbl.getName(id);
        if (text[0] != '$') { lst.emplace_back(text, id); }
    }
    return lst;
}

std::vector<std::pair<std::string_view, int>> Grammar::getActionList() {
    std::vector<std::pair<std::string_view, int>> lst;
    for (int id = 1; id < action_count; ++id) { lst.emplace_back(name_tbl.getName(maskAction | id), id); }
    return lst;
}

void Grammar::printProduction(std::ostream& outp, int prod_no, int pos) const {
    int left = grammar[grammar_idx[prod_no]];
    assert(left & maskNonterm);
    std::vector<int> right;
    std::copy(grammar.begin() + grammar_idx[prod_no] + 1, grammar.begin() + grammar_idx[prod_no + 1],
              std::back_inserter(right));
    outp << "    (" << prod_no << ") " << name_tbl.getName(left) << " ->";
    if (pos != -1) {
        for (int i = 0; i < pos; i++) { outp << " " << decoratedSymbolText(right[i]); }
        outp << " .";
        for (int i = pos; i < (int)right.size(); i++) { outp << " " << decoratedSymbolText(right[i]); }
    } else {
        for (int i = 0; i < (int)right.size(); i++) { outp << " " << decoratedSymbolText(right[i]); }
    }
}

void Grammar::printTokens(std::ostream& outp) const {
    outp << "---=== Tokens : ===---" << std::endl << std::endl;
    for (int id = 0; id < token_count; id++) {
        if (token_used[id]) {
            outp << "    " << symbolText(id) << ' ' << id;
            int prec = token_prec[id];
            if (prec >= 0) {
                outp << " %prec " << (prec & maskPrec);
                if (prec & maskLeftAssoc) {
                    outp << " %left";
                } else if (prec & maskRightAssoc) {
                    outp << " %right";
                } else {
                    outp << " %nonassoc";
                }
            }
            outp << std::endl;
        }
    }
    outp << std::endl;
}

void Grammar::printNonterms(std::ostream& outp) const {
    outp << "---=== Nonterminals : ===---" << std::endl << std::endl;
    for (int id = maskNonterm; id < (maskNonterm + nonterm_count); id++) {
        outp << "    " << name_tbl.getName(id) << " " << id << std::endl;
    }
    outp << std::endl;
}

void Grammar::printActions(std::ostream& outp) const {
    outp << "---=== Actions : ===---" << std::endl << std::endl;
    for (int id = maskAction + 1; id < (maskAction + action_count); id++) {
        outp << "    " << name_tbl.getName(id) << " " << id << std::endl;
    }
    outp << std::endl;
}

void Grammar::printGrammar(std::ostream& outp) const {
    outp << "---=== Grammar : ===---" << std::endl << std::endl;
    int prod_no, prod_count = (int)grammar_idx.size() - 1;
    for (prod_no = 0; prod_no < prod_count; prod_no++) {
        printProduction(outp, prod_no);
        if (act_on_reduce[prod_no] > 0) { outp << " " << decoratedSymbolText(maskAction | act_on_reduce[prod_no]); }
        if (prod_prec[prod_no] >= 0) { outp << " %prec " << (prod_prec[prod_no] & maskPrec); }
        outp << std::endl;
    }
    outp << std::endl;
}

std::string Grammar::symbolText(unsigned id) const {
    if (id > idEnd && id < 0x100) {
        std::string text("\'");
        switch (id) {
            case '\n': text += "\\n"; break;
            case '\t': text += "\\t"; break;
            case '\v': text += "\\v"; break;
            case '\b': text += "\\b"; break;
            case '\r': text += "\\r"; break;
            case '\f': text += "\\f"; break;
            case '\a': text += "\\a"; break;
            case '\\': text += "\\\\"; break;
            case '\'': text += "\\\'"; break;
            case '\"': text += "\\\""; break;
            default: {
                if (id < 0x20 || id == 0x7F) {
                    text += "\\x";
                    if (id > 0xF) { text.push_back('0' + (id >> 4) & 0xF); }
                    text.push_back('0' + id & 0xF);
                } else {
                    text.push_back(id);
                }
            } break;
        }
        text += '\'';
        return text;
    }
    return std::string(name_tbl.getName(id));
}

std::string Grammar::decoratedSymbolText(unsigned id) const {
    std::string text(symbolText(id));
    if (id & maskAction) {
        text = '{' + text + '}';
    } else if (!(id & maskNonterm) && text[0] != '$' && text[0] != '\'') {
        text = '[' + text + ']';
    }
    return text;
}
