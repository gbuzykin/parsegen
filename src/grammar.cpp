#include "grammar.h"

#include <algorithm>
#include <cassert>
#include <ostream>

namespace {
[[nodiscard]] std::string str2text(std::string_view s) {
    std::string text;
    for (unsigned char ch : s) {
        switch (ch) {
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
                if (ch < 0x20 || ch == 0x7F) {
                    text += "\\x";
                    text.push_back('0' + (ch >> 4) & 0xF);
                    text.push_back('0' + ch & 0xF);
                } else {
                    text.push_back(ch);
                }
            } break;
        }
    }
    return text;
}
}  // namespace

std::vector<std::pair<std::string, int>> Grammar::getTokenList() {
    std::vector<std::pair<std::string, int>> lst;
    for (int id = 0x103; id < token_count; ++id) { lst.emplace_back(grammarSymbolText(id), id); }
    return lst;
}

std::vector<std::pair<std::string, int>> Grammar::getActionList() {
    std::vector<std::pair<std::string, int>> lst;
    for (int id = 1; id < action_count; ++id) { lst.emplace_back(grammarSymbolText(maskAction | id), id); }
    return lst;
}

std::string Grammar::grammarSymbolText(int id) const {
    if (id > 0 && id < 0x100) {
        return '\'' + str2text(std::string(1, static_cast<unsigned char>(id))) + '\'';
    } else if (id == idEmpty) {
        return "$empty";
    } else if (id == idDefault) {
        return "$default";
    }
    return std::string(name_tbl.getName(id));
}

std::string Grammar::actionNameText(int id) const { return '{' + std::string(name_tbl.getName(id | maskAction)) + '}'; }

std::string Grammar::precedenceText(int prec) const {
    std::string text;
    if (prec != -1) {
        text += " %prec " + std::to_string(prec & maskPrec);
        if (prec & maskLeftAssoc) {
            text += " %left";
        } else if (prec & maskRightAssoc) {
            text += " %right";
        } else {
            text += " %nonassoc";
        }
    }
    return text;
}

void Grammar::printProduction(std::ostream& outp, int prod_no, int pos) const {
    int left = grammar[grammar_idx[prod_no]];
    assert(left & maskNonterm);
    std::vector<int> right;
    std::copy(grammar.begin() + grammar_idx[prod_no] + 1, grammar.begin() + grammar_idx[prod_no + 1],
              std::back_inserter(right));
    outp << "    (" << prod_no << ") " << grammarSymbolText(left) << " ->";
    int i;
    if (pos != -1) {
        for (i = 0; i < pos; i++) { outp << " " << grammarSymbolText(right[i]); }
        outp << " .";
        for (i = pos; i < (int)right.size(); i++) { outp << " " << grammarSymbolText(right[i]); }
    } else {
        for (i = 0; i < (int)right.size(); i++) { outp << " " << grammarSymbolText(right[i]); }
    }
}

void Grammar::printTokens(std::ostream& outp) const {
    outp << "---=== Tokens : ===---" << std::endl << std::endl;
    for (int id = 0; id < token_count; id++) {
        if (token_used[id]) {
            outp << "    " << grammarSymbolText(id) << " " << id << precedenceText(token_prec[id]) << std::endl;
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
        if (act_on_reduce[prod_no] != 0) { outp << " " << actionNameText(act_on_reduce[prod_no]); }
        outp << precedenceText(prod_prec[prod_no]) << std::endl;
    }
    outp << std::endl;
}
