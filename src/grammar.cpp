#include "grammar.h"

#include <algorithm>
#include <cassert>
#include <ostream>
#include <stdexcept>

Grammar::Grammar() {
    // Initialize predefined tokens
    tokens_.resize(kCharCount + 3);  // Characters and three specials: $empty, $default, $error
    name_tbl_.insertName("$end", kTokenEnd);
    name_tbl_.insertName("$empty", kTokenEmpty);
    name_tbl_.insertName("$default", kTokenDefault);
    name_tbl_.insertName("$error", kTokenError);
    tokens_[kTokenEnd].is_used = true;
    tokens_[kTokenError].is_used = true;

    // Add predefined $accept nonterminal
    name_tbl_.insertName("$accept", kNontermAccept);
    used_nonterms_.addValue(getIndex(kNontermAccept));

    // Add augmenting production
    addProduction(kNontermAccept, {kNontermAccept + 1, kTokenEnd}, -1);
}

std::pair<unsigned, bool> Grammar::addToken(std::string name) {
    unsigned id = static_cast<unsigned>(tokens_.size());
    if (id > ValueSet::kMaxValue) { throw std::runtime_error("too many tokens"); }
    auto result = name_tbl_.insertName(std::move(name), id);
    if (!result.second) { return result; }
    tokens_.emplace_back();
    return result;
}

std::pair<unsigned, bool> Grammar::addNonterm(std::string name) {
    if (nonterm_count_ > ValueSet::kMaxValue) { throw std::runtime_error("too many nonterminals"); }
    auto result = name_tbl_.insertName(std::move(name), makeNontermId(nonterm_count_));
    if (!result.second) { return result; }
    ++nonterm_count_;
    return result;
}

std::pair<unsigned, bool> Grammar::addAction(std::string name) {
    if (action_count_ > ValueSet::kMaxValue) { throw std::runtime_error("too many actions"); }
    auto result = name_tbl_.insertName(std::move(name), makeActionId(action_count_));
    if (!result.second) { return result; }
    ++action_count_;
    return result;
}

bool Grammar::setTokenPrecAndAssoc(unsigned id, int prec, Assoc assoc) {
    auto& tk = tokens_[id];
    if (tk.prec >= 0) { return false; }
    tk = {true, prec, assoc};
    return true;
}

Grammar::ProductionInfo& Grammar::addProduction(unsigned left, std::vector<unsigned> right, int prec) {
    if (prec < 0) {  // Calculate default precedence from the last token
        if (auto it = std::find_if(right.rbegin(), right.rend(), isToken); it != right.rend()) {
            prec = tokens_[*it].prec;
        }
    }

    unsigned final_action = 0;
    if (!right.empty()) {
        // Add dummy productions for not final actions
        for (auto it = right.begin(); it != right.end() - 1; ++it) {
            if (isAction(*it)) {
                unsigned nonterm = addNonterm('@' + std::to_string(nonterm_count_)).first;
                productions_.emplace_back(ProductionInfo{nonterm, {}, getIndex(*it), -1});
                defined_nonterms_.addValue(getIndex(nonterm));
                *it = nonterm;
            }
        }
        // Remove final action and save it separately
        if (isAction(right.back())) {
            final_action = getIndex(right.back());
            right.pop_back();
        }
    }

    defined_nonterms_.addValue(getIndex(left));
    for (unsigned id : right) {
        if (isNonterm(id)) {
            used_nonterms_.addValue(getIndex(id));
        } else {
            tokens_[id].is_used = true;
        }
    }
    return productions_.emplace_back(ProductionInfo{left, std::move(right), final_action, prec});
}

std::string_view Grammar::getName(unsigned id) const {
    auto name = name_tbl_.getName(id);
    if (name.empty()) { throw std::runtime_error("can't find id"); }
    return name;
}

std::vector<std::pair<std::string_view, unsigned>> Grammar::getTokenList() {
    std::vector<std::pair<std::string_view, unsigned>> lst;
    lst.reserve(tokens_.size() - kCharCount);
    for (unsigned id = kCharCount; id < static_cast<unsigned>(tokens_.size()); ++id) {
        std::string_view name = getName(id);
        if (name[0] != '$') { lst.emplace_back(name, id); }
    }
    return lst;
}

std::vector<std::pair<std::string_view, unsigned>> Grammar::getActionList() {
    std::vector<std::pair<std::string_view, unsigned>> lst;
    lst.reserve(action_count_ - 1);
    for (unsigned n = 1; n < action_count_; ++n) { lst.emplace_back(getName(makeActionId(n)), n); }
    return lst;
}

void Grammar::printTokens(std::ostream& outp) const {
    outp << "---=== Tokens : ===---" << std::endl << std::endl;
    for (unsigned id = 0; id < static_cast<unsigned>(tokens_.size()); ++id) {
        if (tokens_[id].is_used) {
            outp << "    " << symbolText(id) << ' ' << id;
            if (tokens_[id].prec >= 0) {
                outp << " %prec " << tokens_[id].prec;
                switch (tokens_[id].assoc) {
                    case Assoc::kNone: outp << " %nonassoc"; break;
                    case Assoc::kLeft: outp << " %left"; break;
                    case Assoc::kRight: outp << " %right"; break;
                }
            }
            outp << std::endl;
        }
    }
    outp << std::endl;
}

void Grammar::printNonterms(std::ostream& outp) const {
    outp << "---=== Nonterminals : ===---" << std::endl << std::endl;
    for (unsigned id = makeNontermId(0); id < makeNontermId(nonterm_count_); ++id) {
        outp << "    " << getName(id) << ' ' << id << std::endl;
    }
    outp << std::endl;
}

void Grammar::printActions(std::ostream& outp) const {
    outp << "---=== Actions : ===---" << std::endl << std::endl;
    for (unsigned id = makeActionId(1); id < makeActionId(action_count_); ++id) {
        outp << "    " << getName(id) << ' ' << id << std::endl;
    }
    outp << std::endl;
}

void Grammar::printGrammar(std::ostream& outp) const {
    outp << "---=== Grammar : ===---" << std::endl << std::endl;
    for (unsigned n_prod = 0; n_prod < static_cast<unsigned>(productions_.size()); ++n_prod) {
        printProduction(outp, n_prod, std::nullopt);
        const auto& prod = productions_[n_prod];
        if (prod.action > 0) { outp << ' ' << decoratedSymbolText(makeActionId(prod.action)); }
        if (prod.prec >= 0) { outp << " %prec " << prod.prec; }
        outp << std::endl;
    }
    outp << std::endl;
}

void Grammar::printProduction(std::ostream& outp, unsigned n_prod, std::optional<unsigned> pos) const {
    const auto& prod = productions_[n_prod];
    outp << "    (" << n_prod << ") " << getName(prod.left) << " ->";
    if (pos) {
        for (size_t i = 0; i < *pos; ++i) { outp << ' ' << decoratedSymbolText(prod.right[i]); }
        outp << " .";
        for (size_t i = *pos; i < prod.right.size(); ++i) { outp << ' ' << decoratedSymbolText(prod.right[i]); }
    } else {
        for (unsigned id : prod.right) { outp << ' ' << decoratedSymbolText(id); }
    }
}

std::string Grammar::symbolText(unsigned id) const {
    if (id > kTokenEnd && id < kCharCount) {
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
                if (id < 0x20 || (id >= 0x7F && id < 0x100)) {
                    text += "\\x";
                    if (id >= 0x10) { text.push_back('0' + (id >> 4) & 0xF); }
                    text.push_back('0' + id & 0xF);
                } else {
                    text.push_back(id);
                }
            } break;
        }
        text += '\'';
        return text;
    }
    return std::string(getName(id));
}

std::string Grammar::decoratedSymbolText(unsigned id) const {
    std::string text(symbolText(id));
    if (isAction(id)) {
        text = '{' + text + '}';
    } else if (isToken(id) && text[0] != '$' && text[0] != '\'') {
        text = '[' + text + ']';
    }
    return text;
}
