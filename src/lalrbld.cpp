#include "lalrbld.h"

#include <cassert>
#include <stdexcept>

void LRBuilder::buildAnalizer() {
    buildFirstTable();
    buildAetaTable();

    // Build LR(0) item groups;

    std::vector<unsigned> pending_states;
    states_.reserve(100);
    action_tbl_.reserve(100);
    goto_tbl_.reserve(100);
    pending_states.reserve(100);

    // Add initial state
    pending_states.push_back(addState({{Position{0, 0}, LookAheadSet{}}}).first);

    do {
        unsigned n_state = pending_states.back();
        pending_states.pop_back();
        // Goto for nonterminals
        for (unsigned n = 0; n < grammar_.getNontermCount(); ++n) {
            PositionSet new_state = calcGoto(states_[n_state], makeNontermId(n));
            if (!new_state.empty()) {
                auto [n_new_state, success] = addState(new_state);
                if (success) { pending_states.push_back(n_new_state); }
                goto_tbl_[n_state][n] = n_new_state;
            }
        }
        // Goto for tokens
        for (unsigned symb = 0; symb < grammar_.getTokenCount(); ++symb) {
            if (grammar_.getTokenInfo(symb).is_used) {
                PositionSet new_state = calcGoto(states_[n_state], symb);
                if (!new_state.empty()) {
                    auto [n_new_state, success] = addState(new_state);
                    if (success) { pending_states.push_back(n_new_state); }
                    action_tbl_[n_state][symb] = (n_new_state << kActionTblFlagCount) + kShiftBit;
                }
            }
        }
    } while (!pending_states.empty());

    // Build lookahead sets:

    // Calculate initial lookahead sets and generate transitions
    // Add `$end` symbol to lookahead set of `$accept -> start` production
    states_[0].begin()->second.la.addValue(0);
    for (unsigned n_state = 0; n_state < states_.size(); ++n_state) {
        for (const auto& [pos, la_set] : states_[n_state]) {
            // [ B -> gamma . delta, # ]
            PositionSet closure = calcClosure({{pos, LookAheadSet{kTokenDefault}}});
            for (const auto& [closure_pos, closure_la_set] : closure) {
                const auto& prod = grammar_.getProductionInfo(closure_pos.n_prod);
                if (closure_pos.pos > prod.right.size()) {
                    throw std::runtime_error("invalid position");
                } else if (closure_pos.pos == prod.right.size()) {
                    continue;
                }
                int goto_state = -1;
                unsigned next_symb = prod.right[closure_pos.pos];
                if (isNonterm(next_symb)) {
                    goto_state = goto_tbl_[n_state][getIndex(next_symb)];
                } else if (action_tbl_[n_state][next_symb] >= 0) {  // Token
                    goto_state = action_tbl_[n_state][next_symb] >> kActionTblFlagCount;
                }
                if (goto_state <= 0) { throw std::runtime_error("invalid goto state"); }

                // `A -> alpha . X beta` -> `A -> alpha X . beta`
                ValueSet la = closure_la_set.la;
                auto it = states_[goto_state].find({closure_pos.n_prod, closure_pos.pos + 1});
                if (it == states_[goto_state].end()) {
                    throw std::runtime_error("can't find state for the next position");
                }
                if (la.contains(kTokenDefault)) {
                    it->second.accept_la_from.push_back(&la_set);
                    la.removeValue(kTokenDefault);
                }
                it->second.la |= la;
            }
        }
    }
    // Start transition iterations
    bool change = false;
    do {
        change = false;
        for (auto& state : states_) {
            for (auto& [pos, la_set] : state) {
                // Accept lookahead characters
                for (const auto* accept_from : la_set.accept_la_from) {
                    ValueSet old_la = la_set.la;
                    la_set.la |= accept_from->la;
                    if (la_set.la != old_la) { change = true; }
                }
            }
        }
    } while (change);

    // Generate actions :

    for (unsigned n_state = 0; n_state < states_.size(); ++n_state) {
        for (const auto& [pos, la_set] : calcClosure(states_[n_state])) {
            const auto& prod = grammar_.getProductionInfo(pos.n_prod);
            if (pos.pos > prod.right.size()) {
                throw std::runtime_error("invalid position");
            } else if (pos.pos != prod.right.size()) {  // Not final position
                continue;
            }
            for (unsigned symb : la_set.la) {
                int& action = action_tbl_[n_state][symb];
                if (action < 0) {
                    action = (3 * pos.n_prod) << kActionTblFlagCount;  // Reduce(n_prod)
                } else if (action & kShiftBit) {
                    // Shift-Reduce conflict
                    const auto& token_info = grammar_.getTokenInfo(symb);
                    int token_prec = token_info.prec;
                    int prod_prec = prod.prec;
                    if (token_prec >= 0 && prod_prec >= 0) {
                        if (prod_prec > token_prec) {
                            action = (3 * pos.n_prod) << kActionTblFlagCount;  // Reduce(n_prod)
                        } else if (token_prec == prod_prec) {
                            if (token_info.assoc == Assoc::kLeft) {
                                action = (3 * pos.n_prod) << kActionTblFlagCount;  // Reduce(n_prod)
                            } else if (token_info.assoc == Assoc::kNone) {
                                action = -1;
                            }
                        }
                    } else {
                        ++sr_conflict_count_;
                    }
                } else {  // Reduce-Reduce conflict
                    ++rr_conflict_count_;
                }
            }
        }
    }
}

void LRBuilder::compressTables(std::vector<int>& action_idx, std::vector<int>& action_list, std::vector<int>& goto_idx,
                               std::vector<int>& goto_list) {
    // Compress action table :

    action_idx.resize(action_tbl_.size());
    for (unsigned n_state = 0; n_state < action_tbl_.size(); ++n_state) {
        // Try to find equal state
        unsigned state2;
        bool found = false;
        for (state2 = 0; state2 < n_state; state2++) {
            found = std::equal(action_tbl_[n_state].begin(), action_tbl_[n_state].end(), action_tbl_[state2].begin());
            if (found) break;  // Found
        }
        if (!found) {
            const std::vector<int>& line = action_tbl_[n_state];
            action_idx[n_state] = (int)action_list.size();

            // Are there any reduce actions?
            bool can_reduce = false;
            int allowed_reduce_act = -1;
            int err_count = 0;
            for (unsigned symb = 0; symb < grammar_.getTokenCount(); ++symb) {
                int act = line[symb];
                if (act == -1)
                    err_count++;
                else if (!can_reduce && !(act & kShiftBit)) {
                    allowed_reduce_act = act;
                    can_reduce = true;
                }
            }

            // Find the most frequent action :

            // Build histogram
            std::map<int, int> histo;
            for (unsigned symb = 0; symb < grammar_.getTokenCount(); ++symb) {
                int act = line[symb];
                if (!can_reduce || (act != -1)) {
                    int freq = 1;
                    if (can_reduce && !(act & kShiftBit)) freq += err_count;
                    std::pair<std::map<int, int>::iterator, bool> ins_res = histo.insert(std::pair<int, int>(act, freq));
                    if (!ins_res.second) ins_res.first->second++;
                }
            }
            // Find maximum frequency
            int max_freq = 0, most_freq_act = 0;
            std::map<int, int>::const_iterator it = histo.begin();
            while (it != histo.end()) {
                if (it->second > max_freq) {
                    max_freq = it->second;
                    most_freq_act = it->first;
                }
                it++;
            }

            // Build list
            for (unsigned symb = 0; symb < grammar_.getTokenCount(); ++symb) {
                int act = line[symb];
                if (!can_reduce || (act != -1)) {
                    if (act != most_freq_act) {
                        action_list.push_back(symb);
                        // if (act == idNonassocError) act = -1;
                        action_list.push_back(act);
                    }
                } else {
                    if (most_freq_act & kShiftBit) {
                        action_list.push_back(symb);
                        action_list.push_back(allowed_reduce_act);
                    }
                }
            }
            // Add default action
            action_list.push_back(-1);
            action_list.push_back(most_freq_act);
        } else
            action_idx[n_state] = action_idx[state2];
    }

    // Compress goto table :

    goto_idx.resize(grammar_.getNontermCount());
    for (unsigned n = 0; n < grammar_.getNontermCount(); ++n) {
        goto_idx[n] = (int)goto_list.size();

        // Find the most frequent state :

        std::map<int, int> histo;
        // Build histogram
        for (unsigned n_state = 0; n_state < goto_tbl_.size(); ++n_state) {
            int new_state = goto_tbl_[n_state][n];
            if (new_state != -1) {
                std::pair<std::map<int, int>::iterator, bool> ins_res = histo.insert(std::pair<int, int>(new_state, 1));
                if (!ins_res.second) ins_res.first->second++;
            }
        }
        // Find maximum frequency
        int max_freq = 0, most_freq_state = 0;
        std::map<int, int>::const_iterator it = histo.begin();
        while (it != histo.end()) {
            if (it->second > max_freq) {
                max_freq = it->second;
                most_freq_state = it->first;
            }
            it++;
        }

        // Build list
        for (unsigned n_state = 0; n_state < goto_tbl_.size(); ++n_state) {
            int state2 = goto_tbl_[n_state][n];
            if ((state2 != -1) && (state2 != most_freq_state)) {
                goto_list.push_back(n_state);
                goto_list.push_back(state2);
            }
        }
        // Add default state
        goto_list.push_back(-1);
        goto_list.push_back(most_freq_state);
    }
}

ValueSet LRBuilder::calcFirst(const std::vector<unsigned>& seq, unsigned pos) {
    ValueSet first;
    bool is_empty_included = true;

    // Look through symbols of the sequence
    for (auto it = seq.begin() + pos; it != seq.end(); ++it) {
        is_empty_included = false;
        if (isNonterm(*it)) {
            // Add symbols from FIRST(symb) excepts the `$empty` to FIRST(seq)
            first |= first_tbl_[getIndex(*it)];
            if (first.contains(kTokenEmpty)) {  // Is `$empty` included
                first.removeValue(kTokenEmpty);
                is_empty_included = true;
            }
        } else {  // Token
            first.addValue(*it);
        }

        if (!is_empty_included) { break; }
    }

    // If FIRST(production) includes the empty character add `$empty` to FIRST(left)
    if (is_empty_included) { first.addValue(kTokenEmpty); }
    return first;
}

LRBuilder::PositionSet LRBuilder::calcGoto(const PositionSet& s, unsigned symb) {
    ValueSet nonkern;
    PositionSet s_next;

    // Look through source items
    for (const auto& [pos, la_set] : s) {
        const auto& prod = grammar_.getProductionInfo(pos.n_prod);
        if (pos.pos > prod.right.size()) {
            throw std::runtime_error("invalid position");
        } else if (pos.pos < prod.right.size()) {
            unsigned next_symb = prod.right[pos.pos];
            if (isNonterm(next_symb)) { nonkern |= Aeta_tbl_[getIndex(next_symb)]; }
            if (next_symb == symb) { s_next.emplace(Position{pos.n_prod, pos.pos + 1}, LookAheadSet{}); }
        }
    }

    // Run through nonkernel items
    for (unsigned n_prod = 0; n_prod < grammar_.getProductionCount(); ++n_prod) {
        const auto& prod = grammar_.getProductionInfo(n_prod);
        assert(isNonterm(prod.left));
        if (nonkern.contains(getIndex(prod.left)) && !prod.right.empty() && prod.right[0] == symb) {
            s_next.emplace(Position{n_prod, 1}, LookAheadSet{});
        }
    }

    return s_next;
}

LRBuilder::PositionSet LRBuilder::calcClosure(const PositionSet& s) {
    ValueSet nonkern;
    std::vector<ValueSet> nonterm_la(grammar_.getNontermCount());

    // Look through kernel items
    for (const auto& [pos, la_set] : s) {
        const auto& prod = grammar_.getProductionInfo(pos.n_prod);
        if (pos.pos > prod.right.size()) {
            throw std::runtime_error("invalid position");
        } else if (pos.pos == prod.right.size()) {
            continue;
        }
        unsigned next_symb = prod.right[pos.pos];
        if (isNonterm(next_symb)) {
            // A -> alpha . B beta
            nonkern.addValue(getIndex(next_symb));
            ValueSet first = calcFirst(prod.right, pos.pos + 1);  // Calculate FIRST(beta);
            if (first.contains(kTokenEmpty)) {
                first.removeValue(kTokenEmpty);
                first |= la_set.la;
            }
            nonterm_la[getIndex(next_symb)] |= first;
        }
    }

    bool change = false;
    do {
        change = false;
        // Run through nonkernel items
        for (const auto& prod : grammar_.getProductions()) {
            assert(isNonterm(prod.left));
            if (nonkern.contains(getIndex(prod.left)) && !prod.right.empty() && isNonterm(prod.right[0])) {
                unsigned n_right = getIndex(prod.right[0]);
                // A -> . B beta
                if (!nonkern.contains(n_right)) {
                    nonkern.addValue(n_right);
                    change = true;
                }
                ValueSet first = calcFirst(prod.right, 1);  // Calculate FIRST(beta);
                if (first.contains(kTokenEmpty)) {
                    first.removeValue(kTokenEmpty);
                    first |= nonterm_la[getIndex(prod.left)];
                }
                ValueSet old_la = nonterm_la[n_right];
                nonterm_la[n_right] |= first;
                if (nonterm_la[n_right] != old_la) { change = true; }
            }
        }
    } while (change);

    PositionSet closure = s;

    // Add nonkernel items
    for (unsigned n_prod = 0; n_prod < grammar_.getProductionCount(); ++n_prod) {
        unsigned left = grammar_.getProductionInfo(n_prod).left;
        assert(isNonterm(left));
        if (nonkern.contains(getIndex(left))) {  // Is production of nonkernel item
            closure.emplace(Position{n_prod, 0}, LookAheadSet{nonterm_la[getIndex(left)]});
        }
    }

    return closure;
}

std::pair<unsigned, bool> LRBuilder::addState(const PositionSet& s) {
    auto it = std::find_if(states_.begin(), states_.end(), [&s](const auto& s2) {
        return s2.size() == s.size() && std::equal(s2.begin(), s2.end(), s.begin(),
                                                   [](const auto& i1, const auto& i2) { return i1.first == i2.first; });
    });
    if (it != states_.end()) { return {static_cast<unsigned>(it - states_.begin()), false}; }
    // Add new state
    states_.push_back(s);
    action_tbl_.emplace_back(grammar_.getTokenCount(), -1);
    goto_tbl_.emplace_back(grammar_.getNontermCount(), -1);
    return {static_cast<unsigned>(states_.size()) - 1, true};
}

void LRBuilder::buildFirstTable() {
    first_tbl_.resize(grammar_.getNontermCount());

    bool change;
    do {
        change = false;
        // Look through all productions
        for (const auto& prod : grammar_.getProductions()) {
            assert(isNonterm(prod.left));
            unsigned n_left = getIndex(prod.left);
            ValueSet first = calcFirst(prod.right);
            // Append FIRST(left) with FIRST(right)
            ValueSet old = first_tbl_[n_left];
            first_tbl_[n_left] |= first;
            if (first_tbl_[n_left] != old) { change = true; }
        }
    } while (change);
}

void LRBuilder::buildAetaTable() {
    Aeta_tbl_.resize(grammar_.getNontermCount());

    for (unsigned n = 0; n < Aeta_tbl_.size(); ++n) { Aeta_tbl_[n].addValue(n); }

    bool change;
    do {
        change = false;
        for (const auto& prod : grammar_.getProductions()) {
            assert(isNonterm(prod.left));
            if (!prod.right.empty()) {
                if (isNonterm(prod.right[0])) {
                    unsigned n_right = getIndex(prod.right[0]);
                    for (auto& Aeta : Aeta_tbl_) {
                        if (Aeta.contains(getIndex(prod.left)) && !Aeta.contains(n_right)) {
                            Aeta.addValue(n_right);
                            change = true;
                        }
                    }
                }
            }
        }
    } while (change);
}

void LRBuilder::printFirstTable(std::ostream& outp) {
    outp << "---=== FIRST table : ===---" << std::endl << std::endl;
    for (unsigned n = 0; n < first_tbl_.size(); ++n) {
        outp << "    FIRST(" << grammar_.getName(makeNontermId(n)) << ") = { ";
        bool colon = false;
        for (unsigned symb : first_tbl_[n]) {
            if (colon) { outp << ", "; }
            outp << grammar_.symbolText(symb);
            colon = true;
        }
        outp << " }" << std::endl;
    }
    outp << std::endl;
}

void LRBuilder::printAetaTable(std::ostream& outp) {
    outp << "---=== Aeta table : ===---" << std::endl << std::endl;
    for (unsigned n = 0; n < Aeta_tbl_.size(); ++n) {
        outp << "    Aeta(" << grammar_.getName(makeNontermId(n)) << ") = { ";
        bool colon = false;
        for (unsigned symb : Aeta_tbl_[n]) {
            if (colon) { outp << ", "; }
            outp << grammar_.getName(makeNontermId(symb));
            colon = true;
        }
        outp << " }" << std::endl;
    }
    outp << std::endl;
}

void LRBuilder::printStates(std::ostream& outp, const std::vector<int>& action_idx, const std::vector<int>& action_list,
                            const std::vector<int>& goto_idx, const std::vector<int>& goto_list) {
    outp << "---=== LALR analyser states : ===---" << std::endl << std::endl;
    for (unsigned n_state = 0; n_state < states_.size(); n_state++) {
        outp << "State " << n_state << ':' << std::endl;
        for (const auto& [pos, la_set] : states_[n_state]) {
            grammar_.printProduction(outp, pos.n_prod, pos.pos);
            outp << " [";
            for (unsigned symb : la_set.la) { outp << ' ' << grammar_.symbolText(symb); }
            outp << " ]" << std::endl;
        }
        outp << std::endl;

        // Action
        for (const int* action_tbl = &action_list[action_idx[n_state]];; action_tbl += 2) {
            int token = action_tbl[0], action = action_tbl[1];
            outp << "    " << grammar_.symbolText(token >= 0 ? token : kTokenDefault) << ", ";
            if (action < 0) {
                outp << "error" << std::endl;
            } else if (action & kShiftBit) {
                outp << "shift and goto state " << (action >> kActionTblFlagCount) << std::endl;
            } else if (action == 0) {
                outp << "accept" << std::endl;
            } else {
                outp << "reduce using rule " << (action >> kActionTblFlagCount) / 3 << std::endl;
            }
            if (token < 0) { break; }
        }
        outp << std::endl;

        // Goto
        for (unsigned n = 0; n < grammar_.getNontermCount(); ++n) {
            for (const int* goto_tbl = &goto_list[goto_idx[n]];; goto_tbl += 2) {
                int from_state = goto_tbl[0], to_state = goto_tbl[1];
                if (from_state < 0 || from_state == n_state) {
                    outp << "    " << grammar_.getName(makeNontermId(n)) << ", goto state " << to_state << std::endl;
                    break;
                }
            }
        }
        outp << std::endl;
    }
}
