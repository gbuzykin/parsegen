#include "grammar.h"

#include "util/algorithm.h"
#include "util/format.h"

Grammar::Grammar() {
    // Initialize predefined tokens
    tokens_.resize(kCharCount + 3);  // Characters and three specials: $empty, $default, $error
    symbol_tbl_.insertName("$empty", kTokenEmpty);
    symbol_tbl_.insertName("$default", kTokenDefault);
    symbol_tbl_.insertName("$error", kTokenError);
    tokens_[kTokenError].is_used = true;
}

std::pair<unsigned, bool> Grammar::addToken(std::string name) {
    unsigned id = static_cast<unsigned>(tokens_.size());
    if (id > ValueSet::kMaxValue) { throw std::runtime_error("too many tokens"); }
    auto result = symbol_tbl_.insertName(std::move(name), id);
    if (!result.second) { return result; }
    tokens_.emplace_back();
    return result;
}

std::pair<unsigned, bool> Grammar::addNonterm(std::string name) {
    if (nonterm_count_ > ValueSet::kMaxValue) { throw std::runtime_error("too many nonterminals"); }
    auto result = symbol_tbl_.insertName(std::move(name), makeNontermId(nonterm_count_));
    if (!result.second) { return result; }
    ++nonterm_count_;
    return result;
}

std::pair<unsigned, bool> Grammar::addAction(std::string name) {
    if (action_count_ > ValueSet::kMaxValue) { throw std::runtime_error("too many actions"); }
    auto result = action_tbl_.insertName(std::move(name), makeActionId(action_count_));
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

Grammar::ProductionInfo& Grammar::addProduction(unsigned lhs, std::vector<unsigned> rhs, int prec) {
    if (prec < 0) {  // Calculate default precedence from the last token
        if (auto [it, found] = util::find_if(util::reverse_range(rhs), isToken); found) { prec = tokens_[*it].prec; }
    }

    unsigned final_action = 0;
    if (!rhs.empty()) {
        // Add dummy productions for not final actions
        for (auto it = rhs.begin(); it != rhs.end() - 1; ++it) {
            if (isAction(*it)) {
                unsigned nonterm = addNonterm('@' + std::to_string(nonterm_count_)).first;
                productions_.emplace_back(nonterm, getIndex(*it));
                defined_nonterms_.addValue(getIndex(nonterm));
                *it = nonterm;
            }
        }
        // Remove final action and save it separately
        if (isAction(rhs.back())) {
            final_action = getIndex(rhs.back());
            rhs.pop_back();
        }
    }

    defined_nonterms_.addValue(getIndex(lhs));
    for (unsigned id : rhs) {
        if (isNonterm(id)) {
            used_nonterms_.addValue(getIndex(id));
        } else {
            tokens_[id].is_used = true;
        }
    }
    return productions_.emplace_back(lhs, std::move(rhs), final_action, prec);
}

bool Grammar::addStartCondition(std::string name) {
    auto [it, found] = util::find_if(start_conditions_, [&name](const auto& sc) { return sc.first == name; });
    if (found) { return false; }
    start_conditions_.emplace_back(std::move(name), 0);
    return true;
}

bool Grammar::setStartConditionProd(std::string_view name, unsigned n_prod) {
    auto [it, found] = util::find_if(start_conditions_, [&name](const auto& sc) { return sc.first == name; });
    if (!found) { return false; }
    it->second = n_prod;
    return true;
}

std::string_view Grammar::getSymbolName(unsigned id) const {
    auto name = symbol_tbl_.getName(id);
    if (name.empty()) { throw std::runtime_error("can't find symbol id"); }
    return name;
}

std::string_view Grammar::getActionName(unsigned id) const {
    auto name = action_tbl_.getName(id);
    if (name.empty()) { throw std::runtime_error("can't find action id"); }
    return name;
}

std::vector<std::pair<std::string_view, unsigned>> Grammar::getTokenList() {
    std::vector<std::pair<std::string_view, unsigned>> lst;
    lst.reserve(tokens_.size() - kCharCount);
    for (unsigned id = kCharCount; id < static_cast<unsigned>(tokens_.size()); ++id) {
        std::string_view name = getSymbolName(id);
        if (name[0] != '$') { lst.emplace_back(name, id); }
    }
    return lst;
}

std::vector<std::pair<std::string_view, unsigned>> Grammar::getActionList() {
    std::vector<std::pair<std::string_view, unsigned>> lst;
    lst.reserve(action_count_ - 1);
    for (unsigned n = 1; n < action_count_; ++n) { lst.emplace_back(getActionName(makeActionId(n)), n); }
    return lst;
}

void Grammar::printTokens(util::iobuf& outp) const {
    outp.write("---=== Tokens : ===---").endl().endl();
    for (unsigned id = 0; id < static_cast<unsigned>(tokens_.size()); ++id) {
        if (tokens_[id].is_used) {
            util::fprint(outp, "    {} {}", symbolText(id), id);
            if (tokens_[id].prec >= 0) {
                util::fprint(outp, " %prec {}", tokens_[id].prec);
                switch (tokens_[id].assoc) {
                    case Assoc::kNone: outp.write(" %nonassoc"); break;
                    case Assoc::kLeft: outp.write(" %left"); break;
                    case Assoc::kRight: outp.write(" %right"); break;
                }
            }
            outp.endl();
        }
    }
    outp.endl();
}

void Grammar::printNonterms(util::iobuf& outp) const {
    outp.write("---=== Nonterminals : ===---").endl().endl();
    for (unsigned id = makeNontermId(0); id < makeNontermId(nonterm_count_); ++id) {
        util::fprintln(outp, "    {} {}", getSymbolName(id), id);
    }
    outp.endl();
}

void Grammar::printActions(util::iobuf& outp) const {
    outp.write("---=== Actions : ===---").endl().endl();
    for (unsigned id = makeActionId(1); id < makeActionId(action_count_); ++id) {
        util::fprintln(outp, "    {} {}", getActionName(id), id);
    }
    outp.endl();
}

void Grammar::printGrammar(util::iobuf& outp) const {
    outp.write("---=== Grammar : ===---").endl().endl();
    for (unsigned n_prod = 0; n_prod < static_cast<unsigned>(productions_.size()); ++n_prod) {
        printProduction(outp, n_prod, std::nullopt);
        const auto& prod = productions_[n_prod];
        if (prod.action > 0) { outp.put(' ').write(decoratedSymbolText(makeActionId(prod.action))); }
        if (prod.prec >= 0) { util::fprint(outp, " %prec {}", prod.prec); }
        outp.endl();
    }
    outp.endl();
}

void Grammar::printProduction(util::iobuf& outp, unsigned n_prod, std::optional<unsigned> pos) const {
    const auto& prod = productions_[n_prod];
    util::fprint(outp, "    ({}) {} ->", n_prod, getSymbolName(prod.lhs));
    if (pos) {
        for (size_t i = 0; i < *pos; ++i) { outp.put(' ').write(decoratedSymbolText(prod.rhs[i])); }
        outp.write(" .");
        for (size_t i = *pos; i < prod.rhs.size(); ++i) { outp.put(' ').write(decoratedSymbolText(prod.rhs[i])); }
    } else {
        for (unsigned id : prod.rhs) { outp.put(' ').write(decoratedSymbolText(id)); }
    }
}

std::string Grammar::symbolText(unsigned id) const {
    if (id < kCharCount) {
        std::string text("\'");
        switch (id) {
            case '\0': text += "\\0"; break;
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
                    if (id >= 0x10) { text.push_back('0' + ((id >> 4) & 0xF)); }
                    text.push_back('0' + (id & 0xF));
                } else {
                    text.push_back(id);
                }
            } break;
        }
        text += '\'';
        return text;
    }
    return std::string(getSymbolName(id));
}

std::string Grammar::decoratedSymbolText(unsigned id) const {
    if (isAction(id)) { return '{' + std::string(getActionName(id)) + '}'; }
    std::string text(symbolText(id));
    if (isToken(id) && text[0] != '$' && text[0] != '\'') { text = '[' + text + ']'; }
    return text;
}
