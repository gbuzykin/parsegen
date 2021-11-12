#include "lalrbld.h"

#include <cassert>

void LRBuilder::buildAnalizer() {
    buildFirstTbl();
    buildAetaTbl();

    // Build LR(0) item groups;

    std::vector<unsigned> pending_states;

    // Add initial state
    pending_states.push_back(addState(State{{Position{0, 0}, StateItem{}}}).first);

    do {
        unsigned state_idx = pending_states.back();
        pending_states.pop_back();
        // Goto for nonterminals
        for (unsigned ch = 0; ch < grammar_.getNontermCount(); ch++) {
            State new_state = calcGoto(states_[state_idx], kNontermFlag | ch);
            if (!new_state.empty()) {
                auto [new_state_idx, success] = addState(new_state);
                if (success) { pending_states.push_back(new_state_idx); }
                goto_tbl_[state_idx][ch] = new_state_idx;
            }
        }
        // Goto for tokens
        for (unsigned ch = 0; ch < grammar_.getTokenCount(); ch++) {
            if (grammar_.getTokenInfo(ch).is_used) {
                State new_state = calcGoto(states_[state_idx], ch);
                if (!new_state.empty()) {
                    auto [new_state_idx, success] = addState(new_state);
                    if (success) { pending_states.push_back(new_state_idx); }
                    action_tbl_[state_idx][ch] = (new_state_idx << kActionTblFlagCount) + kShiftBit;
                }
            }
        }
    } while (pending_states.size() > 0);

    // Build lookahead sets:

    // Calculate initial lookahead sets and generate transferings
    assert(states_[0].size() > 0);
    states_[0].begin()->second.la.addValue(0);  // Add $end symbol to the lookahead set of
                                                // the $accept -> start production
    for (unsigned state_idx = 0; state_idx < states_.size(); ++state_idx) {
        auto it = states_[state_idx].begin();
        while (it != states_[state_idx].end()) {
            // item = [B -> gamma . delta, #]
            StateItem item;
            item.la.addValue(kTokenDefault);
            State closure = calcClosure({{it->first, item}});
            auto it2 = closure.begin();
            while (it2 != closure.end()) {
                const auto& prod = grammar_.getProductionInfo(it2->first.n_prod);
                unsigned next_ch_idx = it2->first.pos;
                assert(next_ch_idx <= prod.right.size());
                if (next_ch_idx < prod.right.size()) {  // Not final position
                    int goto_st = -1;
                    unsigned next_ch = prod.right[next_ch_idx];
                    if (next_ch & kNontermFlag)  // The next character is nonterminal
                        goto_st = goto_tbl_[state_idx][next_ch & ~kNontermFlag];
                    else if (action_tbl_[state_idx][next_ch] >= 0)  // Token
                        goto_st = action_tbl_[state_idx][next_ch] >> kActionTblFlagCount;
                    assert(goto_st != -1);

                    // *it2 is A -> alpha . X beta
                    // goto_item is A -> alpha X . beta
                    auto it3 = states_[goto_st].find(
                        {it2->first.n_prod, it2->first.pos + 1});  // Find goto_item in goto(*it2, X)
                    assert(it3 != states_[goto_st].end());
                    ValueSet la = it2->second.la;
                    if (la.contains(kTokenDefault)) {  // If '#' belongs la
                        it3->second.accept_la.push_back(&it->second);
                        la.removeValue(kTokenDefault);
                    }
                    it3->second.la |= la;
                }
                it2++;
            }
            it++;
        }
    }

    // Start transfering iterations
    bool change = false;
    do {
        change = false;
        for (unsigned state_idx = 0; state_idx < states_.size(); ++state_idx) {
            auto it = states_[state_idx].begin();
            while (it != states_[state_idx].end()) {
                // Accept lookahead characters
                for (const auto* accept_from : it->second.accept_la) {
                    ValueSet new_la = accept_from->la - it->second.la;
                    if (!new_la.empty()) {
                        it->second.la |= new_la;
                        change = true;
                    }
                }
                it++;
            }
        }
    } while (change);

    // Generate actions :

    for (unsigned state_idx = 0; state_idx < states_.size(); ++state_idx) {
        State closure = calcClosure(states_[state_idx]);
        auto it = closure.begin();
        while (it != closure.end()) {
            unsigned n_prod = it->first.n_prod;
            const auto& prod = grammar_.getProductionInfo(n_prod);
            unsigned next_ch_idx = it->first.pos;
            assert(next_ch_idx <= prod.right.size());
            if (next_ch_idx == prod.right.size()) {  // Final position
                for (unsigned ch : it->second.la) {
                    int old_val = action_tbl_[state_idx][ch];
                    if (old_val < 0)
                        action_tbl_[state_idx][ch] = (3 * n_prod) << kActionTblFlagCount;  // Reduce(n_prod)
                    else if (old_val & kShiftBit) {
                        // Shift-reduce conflict :
                        const auto& token_info = grammar_.getTokenInfo(ch);
                        int prec = token_info.prec;
                        int prod_prec = prod.prec;
                        if ((prec != -1) && (prod_prec != -1)) {
                            int cmp = prod_prec - prec;
                            if (cmp == 0) {
                                if (token_info.assoc == Assoc::kLeft)
                                    action_tbl_[state_idx][ch] = (3 * n_prod) << kActionTblFlagCount;  // Reduce(n_prod)
                                else if (token_info.assoc == Assoc::kNone)
                                    action_tbl_[state_idx][ch] = -1;
                            } else if (cmp > 0)
                                action_tbl_[state_idx][ch] = (3 * n_prod) << kActionTblFlagCount;  // Reduce(n_prod)
                        } else
                            sr_conflict_count_++;
                    } else {
                        // Reduce-reduce conflict
                        rr_conflict_count_++;
                    }
                }
            }
            it++;
        }
    }
}

void LRBuilder::compressTables(std::vector<int>& action_idx, std::vector<int>& action_list, std::vector<int>& goto_idx,
                               std::vector<int>& goto_list) {
    // Compress action table :

    action_idx.resize(action_tbl_.size());
    for (unsigned state_idx = 0; state_idx < action_tbl_.size(); ++state_idx) {
        // Try to find equal state
        unsigned state2;
        bool found = false;
        for (state2 = 0; state2 < state_idx; state2++) {
            found = std::equal(action_tbl_[state_idx].begin(), action_tbl_[state_idx].end(),
                               action_tbl_[state2].begin());
            if (found) break;  // Found
        }
        if (!found) {
            const std::vector<int>& line = action_tbl_[state_idx];
            action_idx[state_idx] = (int)action_list.size();

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
            action_idx[state_idx] = action_idx[state2];
    }

    // Compress goto table :

    goto_idx.resize(grammar_.getNontermCount());
    for (unsigned nonterm = 0; nonterm < grammar_.getNontermCount(); ++nonterm) {
        goto_idx[nonterm] = (int)goto_list.size();

        // Find the most frequent state :

        std::map<int, int> histo;
        // Build histogram
        for (unsigned state_idx = 0; state_idx < goto_tbl_.size(); ++state_idx) {
            int new_state = goto_tbl_[state_idx][nonterm];
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
        for (unsigned state_idx = 0; state_idx < goto_tbl_.size(); ++state_idx) {
            int state2 = goto_tbl_[state_idx][nonterm];
            if ((state2 != -1) && (state2 != most_freq_state)) {
                goto_list.push_back(state_idx);
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
        if (*it & kNontermFlag)  // If nonterminal
        {
            // Add symbols from FIRST(ch) excepts the $empty to FIRST(seq)
            first |= first_tbl_[*it & ~kNontermFlag];
            if (first.contains(kTokenEmpty))  // Is $empty included
            {
                first.removeValue(kTokenEmpty);
                is_empty_included = true;
            }
        } else                    // If terminal
            first.addValue(*it);  // Just add the terminal

        if (!is_empty_included) break;
    }

    // If FIRST(production) includes the empty character add $empty to FIRST(left)
    if (is_empty_included) first.addValue(kTokenEmpty);
    return first;
}

LRBuilder::State LRBuilder::calcGoto(const State& s, unsigned symb) {
    State tgt;
    ValueSet nonkern;

    // Look through source items
    auto it = s.begin();
    while (it != s.end()) {
        unsigned n_prod = it->first.n_prod;
        const auto& prod = grammar_.getProductionInfo(n_prod);
        unsigned next_ch_idx = it->first.pos;
        assert(next_ch_idx <= prod.right.size());
        if (next_ch_idx < prod.right.size()) {
            unsigned next_ch = prod.right[next_ch_idx];
            if (next_ch & kNontermFlag) nonkern |= Aeta_tbl_[next_ch & ~kNontermFlag];
            if (next_ch == symb) { tgt.emplace(Position{n_prod, it->first.pos + 1}, StateItem{}); }
        }
        it++;
    }

    // Run through nonkernel items
    for (unsigned n_prod = 0; n_prod < grammar_.getProductionCount(); ++n_prod) {
        const auto& prod = grammar_.getProductionInfo(n_prod);
        assert(prod.left & kNontermFlag);
        if (nonkern.contains(prod.left & ~kNontermFlag) && !prod.right.empty() && prod.right[0] == symb) {
            tgt.emplace(Position{n_prod, 1}, StateItem{});
        }
    }

    return tgt;
}

LRBuilder::State LRBuilder::calcClosure(const State& s) {
    ValueSet nonkern;
    std::vector<ValueSet> nonterm_la(grammar_.getNontermCount());
    State closure = s;
    // Look through kernel items
    auto it = s.begin();
    while (it != s.end()) {
        const auto& prod = grammar_.getProductionInfo(it->first.n_prod);
        unsigned next_ch_idx = it->first.pos;
        assert(next_ch_idx <= prod.right.size());
        if (next_ch_idx < prod.right.size()) {
            unsigned next_ch = prod.right[next_ch_idx];
            if (next_ch & kNontermFlag) {
                // A -> alpha . B beta
                nonkern.addValue(next_ch & ~kNontermFlag);

                // seq = beta
                ValueSet la = calcFirst(prod.right, next_ch_idx + 1);  // Calculate FIRST(beta);
                if (la.contains(kTokenEmpty)) {
                    la.removeValue(kTokenEmpty);
                    la |= it->second.la;  // Replace $empty with LA(item)
                }
                nonterm_la[next_ch & ~kNontermFlag] |= la;
            }
        }
        it++;
    }

    bool change = false;
    do {
        change = false;
        // Run through nonkernel items
        for (unsigned n_prod = 0; n_prod < grammar_.getProductionCount(); ++n_prod) {
            const auto& prod = grammar_.getProductionInfo(n_prod);
            assert(prod.left & kNontermFlag);
            if (nonkern.contains(prod.left & ~kNontermFlag) && !prod.right.empty() && (prod.right[0] & kNontermFlag)) {
                // A -> . B beta
                if (!nonkern.contains(prod.right[0] & ~kNontermFlag)) {
                    nonkern.addValue(prod.right[0] & ~kNontermFlag);
                    change = true;
                }

                // seq = beta
                ValueSet la = calcFirst(prod.right, 1);  // Calculate FIRST(beta);
                if (la.contains(kTokenEmpty)) {
                    la.removeValue(kTokenEmpty);
                    la |= nonterm_la[prod.left & ~kNontermFlag];  // Replace $empty with LA(item)
                }
                ValueSet new_la = la - nonterm_la[prod.right[0] & ~kNontermFlag];
                if (!new_la.empty()) {
                    nonterm_la[prod.right[0] & ~kNontermFlag] |= new_la;
                    change = true;
                }
            }
        }
    } while (change);

    // Add nonkernel items
    for (unsigned n_prod = 0; n_prod < grammar_.getProductionCount(); ++n_prod) {
        unsigned left = grammar_.getProductionInfo(n_prod).left;
        assert(left & kNontermFlag);
        if (nonkern.contains(left & ~kNontermFlag)) {  // Is production of nonkernel item
            closure.emplace(Position{n_prod, 0}, StateItem{nonterm_la[left & ~kNontermFlag], {}});
        }
    }

    return closure;
}

std::pair<unsigned, bool> LRBuilder::addState(const State& s) {
    for (unsigned state_idx = 0; state_idx < states_.size(); ++state_idx) {
        if ((states_[state_idx].size() == s.size()) &&
            std::equal(states_[state_idx].begin(), states_[state_idx].end(), s.begin(),
                       [](const auto& i1, const auto& i2) { return i1.first == i2.first; })) {
            return {state_idx, false};  // Return old state index
        }
    }
    // Add new state
    states_.push_back(s);
    action_tbl_.emplace_back(grammar_.getTokenCount(), -1);
    goto_tbl_.emplace_back(grammar_.getNontermCount(), -1);
    return {static_cast<unsigned>(states_.size() - 1), true};
}

void LRBuilder::buildFirstTbl() {
    first_tbl_.resize(grammar_.getNontermCount());
    assert(!first_tbl_.empty());

    bool change;
    do {
        change = false;
        // Look through all productions
        for (unsigned n_prod = 0; n_prod < grammar_.getProductionCount(); ++n_prod) {
            const auto& prod = grammar_.getProductionInfo(n_prod);
            assert(prod.left & kNontermFlag);
            ValueSet& cur_first_set = first_tbl_[prod.left & ~kNontermFlag];

            // Calculate FIRST(right)
            ValueSet first = calcFirst(prod.right);
            // Append FIRST(left) with FIRST(right)
            ValueSet new_chars = first - cur_first_set;
            if (!new_chars.empty()) {
                cur_first_set |= first;
                change = true;
            }
        }
    } while (change);
}

void LRBuilder::buildAetaTbl() {
    Aeta_tbl_.resize(grammar_.getNontermCount());
    assert(!Aeta_tbl_.empty());

    for (unsigned nonterm = 0; nonterm < grammar_.getNontermCount(); ++nonterm) {
        Aeta_tbl_[nonterm].addValue(nonterm);
    }

    bool change;
    do {
        change = false;
        for (unsigned n_prod = 0; n_prod < grammar_.getProductionCount(); ++n_prod) {
            const auto& prod = grammar_.getProductionInfo(n_prod);
            assert(prod.left & kNontermFlag);
            if (!prod.right.empty()) {
                if (prod.right[0] & kNontermFlag) {
                    for (unsigned nonterm = 0; nonterm < grammar_.getNontermCount(); ++nonterm) {
                        if (Aeta_tbl_[nonterm].contains(prod.left & ~kNontermFlag) &&
                            !Aeta_tbl_[nonterm].contains(prod.right[0] & ~kNontermFlag)) {
                            Aeta_tbl_[nonterm].addValue(prod.right[0] & ~kNontermFlag);
                            change = true;
                        }
                    }
                }
            }
        }
    } while (change);
}

void LRBuilder::printItemSet(std::ostream& outp, const State& s) {
    auto it = s.begin();
    while (it != s.end()) {
        grammar_.printProduction(outp, it->first.n_prod, it->first.pos);
        outp << " [";
        for (unsigned la_ch : it->second.la) { outp << ' ' << grammar_.symbolText(la_ch); }
        outp << " ]" << std::endl;
        it++;
    }
}

void LRBuilder::printFirstTbl(std::ostream& outp) {
    outp << "---=== FIRST table : ===---" << std::endl << std::endl;
    for (unsigned nonterm = 0; nonterm < grammar_.getNontermCount(); ++nonterm) {
        outp << "    FIRST(" << grammar_.getName(kNontermFlag | nonterm) << ") = { ";
        const ValueSet& first = first_tbl_[nonterm];
        bool colon = false;
        for (unsigned ch : first) {
            if (colon) { outp << ", "; }
            outp << grammar_.symbolText(ch);
            colon = true;
        }
        outp << " }" << std::endl;
    }
    outp << std::endl;
}

void LRBuilder::printAetaTbl(std::ostream& outp) {
    outp << "---=== Aeta table : ===---" << std::endl << std::endl;
    for (unsigned nonterm = 0; nonterm < grammar_.getNontermCount(); ++nonterm) {
        outp << "    Aeta(" << grammar_.getName(kNontermFlag | nonterm) << ") = { ";
        const ValueSet& Aeta = Aeta_tbl_[nonterm];
        bool colon = false;
        for (unsigned ch : Aeta) {
            if (colon) { outp << ", "; }
            outp << grammar_.getName(kNontermFlag | ch);
            colon = true;
        }
        outp << " }" << std::endl;
    }
    outp << std::endl;
}

void LRBuilder::printStates(std::ostream& outp, std::vector<int>& action_idx, std::vector<int>& action_list,
                            std::vector<int>& goto_idx, std::vector<int>& goto_list) {
    outp << "---=== LALR analyser states : ===---" << std::endl << std::endl;
    for (unsigned state_idx = 0; state_idx < states_.size(); state_idx++) {
        outp << "State " << state_idx << ":" << std::endl;
        printItemSet(outp, states_[state_idx]);
        outp << std::endl;

        // Action
        int act_idx = action_idx[state_idx];
        while (1) {
            int token = action_list[act_idx++];
            outp << "    " << grammar_.symbolText(token >= 0 ? token : kTokenDefault) << ", ";
            int act = action_list[act_idx++];
            if (act == -1)
                outp << "error" << std::endl;
            else if (act & kShiftBit)
                outp << "shift and goto state " << (act >> kActionTblFlagCount) << std::endl;
            else if (act == 0)
                outp << "accept" << std::endl;
            else
                outp << "reduce using rule " << (act >> kActionTblFlagCount) / 3 << std::endl;
            if (token < 0) break;
        }
        outp << std::endl;

        // Goto
        for (unsigned nonterm = 0; nonterm < grammar_.getNontermCount(); ++nonterm) {
            int idx = goto_idx[nonterm];
            while (1) {
                int state_idx2 = goto_list[idx++];
                int new_state_idx = goto_list[idx++];
                if ((state_idx2 == -1) || (state_idx2 == state_idx)) {
                    outp << "    " << grammar_.getName(kNontermFlag | nonterm) << ", goto state " << new_state_idx
                         << std::endl;
                    break;
                }
            }
        }
        outp << std::endl;
    }
}
