#include "lalrbld.h"

#include <cassert>
#include <tuple>

void LRBuilder::buildAnalizer() {
    genFirstTbl();
    genAetaTbl();

    states_.clear();
    action_tbl_.clear();
    goto_tbl_.clear();
    std::vector<int> unmarked_states;

    // Build LR(0) item groups;

    // Add initial unmarked state
    int state_idx;
    State new_state;
    new_state.emplace(std::piecewise_construct, std::make_tuple(0, 0), std::tuple<>());
    addState(new_state, state_idx);
    unmarked_states.push_back(0);

    do {
        int unm_state = unmarked_states.back();
        unmarked_states.pop_back();
        int ch;
        // calcGoto for nonterminals
        for (ch = 0; ch < grammar_.nonterm_count; ch++) {
            calcGoto(states_[unm_state], maskNonterm | ch, new_state);
            if (new_state.size() > 0)  // Nonempty state
            {
                if (addState(new_state, state_idx)) unmarked_states.push_back(state_idx);
                goto_tbl_[unm_state][ch] = state_idx;
            }
        }
        // Goto for tokens
        for (ch = 0; ch < grammar_.token_count; ch++) {
            if (grammar_.token_used[ch]) {
                calcGoto(states_[unm_state], ch, new_state);
                if (new_state.size() > 0)  // Nonempty state
                {
                    if (addState(new_state, state_idx)) unmarked_states.push_back(state_idx);
                    action_tbl_[unm_state][ch] = (state_idx << kActionTblFlagCount) + kShiftBit;
                }
            }
        }
    } while (unmarked_states.size() > 0);

    // Build lookahead sets:

    // Calculate initial lookahead sets and generate transferings
    assert(states_[0].size() > 0);
    states_[0].begin()->second.la.addValue(0);  // Add $end symbol to the lookahead set of
                                                // the $accept -> start production
    int state_count = (int)states_.size();
    for (int state_idx = 0; state_idx < state_count; state_idx++) {
        auto it = states_[state_idx].begin();
        while (it != states_[state_idx].end()) {
            // item = [B -> gamma . delta, #]
            State closure;
            // closure = calcClosure(item)
            calcClosure(it->first, closure);
            auto it2 = closure.begin();
            while (it2 != closure.end()) {
                int prod_no = it2->first.prod_no;
                int next_ch_idx = grammar_.grammar_idx[prod_no] + it2->first.pos + 1;
                assert(next_ch_idx <= grammar_.grammar_idx[prod_no + 1]);
                if (next_ch_idx < grammar_.grammar_idx[prod_no + 1])  // Not final position
                {
                    int goto_st = -1;
                    int next_ch = grammar_.grammar[next_ch_idx];
                    if (next_ch & maskNonterm)  // The next character is nonterminal
                        goto_st = goto_tbl_[state_idx][next_ch & maskId];
                    else if (action_tbl_[state_idx][next_ch] >= 0)  // Token
                        goto_st = action_tbl_[state_idx][next_ch] >> kActionTblFlagCount;
                    assert(goto_st != -1);

                    // *it2 is A -> alpha . X beta
                    // goto_item is A -> alpha X . beta
                    auto it3 = states_[goto_st].find(
                        {it2->first.prod_no, it2->first.pos + 1});  // Find goto_item in goto(*it2, X)
                    assert(it3 != states_[goto_st].end());
                    ValueSet la = it2->second.la;
                    if (la.contains(idDefault)) {  // If '#' belongs la
                        it3->second.accept_la.push_back(&it->second);
                        la.removeValue(idDefault);
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
        for (state_idx = 0; state_idx < state_count; state_idx++) {
            auto it = states_[state_idx].begin();
            while (it != states_[state_idx].end()) {
                // Accept lookahead characters
                for (int item = 0; item < (int)it->second.accept_la.size(); item++) {
                    ValueSet new_la = it->second.accept_la[item]->la - it->second.la;
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

    for (state_idx = 0; state_idx < state_count; state_idx++) {
        State closure;
        calcClosureSet(states_[state_idx], closure);
        auto it = closure.begin();
        while (it != closure.end()) {
            int prod_no = it->first.prod_no;
            int next_ch_idx = grammar_.grammar_idx[prod_no] + it->first.pos + 1;
            assert(next_ch_idx <= grammar_.grammar_idx[prod_no + 1]);
            if (next_ch_idx == grammar_.grammar_idx[prod_no + 1]) {  // Final position
                for (unsigned ch : it->second.la) {
                    int old_val = action_tbl_[state_idx][ch];
                    if (old_val < 0)
                        action_tbl_[state_idx][ch] = (3 * prod_no) << kActionTblFlagCount;  // Reduce(prod_no)
                    else if (old_val & kShiftBit) {
                        // Shift-reduce conflict :
                        int prec = grammar_.token_prec[ch];
                        int prod_prec = grammar_.prod_prec[prod_no];
                        if ((prec != -1) && (prod_prec != -1)) {
                            int cmp = (prod_prec & maskPrec) - (prec & maskPrec);
                            if (cmp == 0) {
                                if (prec & maskLeftAssoc)
                                    action_tbl_[state_idx][ch] = (3 * prod_no)
                                                                 << kActionTblFlagCount;  // Reduce(prod_no)
                                else if (!(prec & maskRightAssoc))
                                    action_tbl_[state_idx][ch] = -1;
                            } else if (cmp > 0)
                                action_tbl_[state_idx][ch] = (3 * prod_no) << kActionTblFlagCount;  // Reduce(prod_no)
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

    int state, state_count = (int)action_tbl_.size();
    action_list.clear();
    action_idx.resize(state_count);
    for (state = 0; state < state_count; state++) {
        // Try to find equal state
        int state2;
        bool found = false;
        for (state2 = 0; state2 < state; state2++) {
            found = std::equal(action_tbl_[state].begin(), action_tbl_[state].end(), action_tbl_[state2].begin());
            if (found) break;  // Found
        }
        if (!found) {
            const std::vector<int>& line = action_tbl_[state];
            action_idx[state] = (int)action_list.size();
            int ch;

            // Are there any reduce actions?
            bool can_reduce = false;
            int allowed_reduce_act = -1;
            int err_count = 0;
            for (ch = 0; ch < grammar_.token_count; ch++) {
                int act = line[ch];
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
            for (ch = 0; ch < grammar_.token_count; ch++) {
                int act = line[ch];
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
            for (ch = 0; ch < grammar_.token_count; ch++) {
                int act = line[ch];
                if (!can_reduce || (act != -1)) {
                    if (act != most_freq_act) {
                        action_list.push_back(ch);
                        // if (act == idNonassocError) act = -1;
                        action_list.push_back(act);
                    }
                } else {
                    if (most_freq_act & kShiftBit) {
                        action_list.push_back(ch);
                        action_list.push_back(allowed_reduce_act);
                    }
                }
            }
            // Add default action
            action_list.push_back(-1);
            action_list.push_back(most_freq_act);
        } else
            action_idx[state] = action_idx[state2];
    }

    // Compress goto table :

    goto_list.clear();
    goto_idx.resize(grammar_.nonterm_count);
    for (int nonterm = 0; nonterm < grammar_.nonterm_count; nonterm++) {
        goto_idx[nonterm] = (int)goto_list.size();

        // Find the most frequent state :

        std::map<int, int> histo;
        // Build histogram
        for (state = 0; state < state_count; state++) {
            int new_state = goto_tbl_[state][nonterm];
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
        for (state = 0; state < state_count; state++) {
            int state2 = goto_tbl_[state][nonterm];
            if ((state2 != -1) && (state2 != most_freq_state)) {
                goto_list.push_back(state);
                goto_list.push_back(state2);
            }
        }
        // Add default state
        goto_list.push_back(-1);
        goto_list.push_back(most_freq_state);
    }
}

void LRBuilder::calcFirst(const std::vector<int>& seq, ValueSet& first) {
    first.clear();
    bool is_empty_included = true;
    // Look through symbols of the sequence
    for (int i = 0; i < (int)seq.size(); i++) {
        int ch = seq[i];

        is_empty_included = false;
        if (ch & maskNonterm)  // If nonterminal
        {
            // Add symbols from FIRST(ch) excepts the $empty to FIRST(seq)
            first |= first_tbl_[ch & maskId];
            if (first.contains(idEmpty))  // Is $empty included
            {
                first.removeValue(idEmpty);
                is_empty_included = true;
            }
        } else                   // If terminal
            first.addValue(ch);  // Just add the terminal

        if (!is_empty_included) break;
    }

    if (is_empty_included)
        first.addValue(idEmpty);  // If FIRST(production) includes the empty character
                                  // add $empty to FIRST(left)
}

void LRBuilder::genFirstTbl() {
    bool change;
    // clear FIRST table
    assert(grammar_.nonterm_count > 0);
    first_tbl_.resize(grammar_.nonterm_count);
    for (int i = 0; i < (int)first_tbl_.size(); i++) first_tbl_[i].clear();
    do {
        change = false;
        // Look through all productions
        int prod_no, prod_count = (int)grammar_.grammar_idx.size() - 1;
        for (prod_no = 0; prod_no < prod_count; prod_no++) {
            int left = grammar_.grammar[grammar_.grammar_idx[prod_no]];
            assert(left & maskNonterm);
            std::vector<int> right;
            std::copy(grammar_.grammar.begin() + grammar_.grammar_idx[prod_no] + 1,
                      grammar_.grammar.begin() + grammar_.grammar_idx[prod_no + 1], std::back_inserter(right));
            ValueSet& cur_first_set = first_tbl_[left & maskId];

            ValueSet first;
            // Calculate FIRST(right)
            calcFirst(right, first);
            // Append FIRST(left) with FIRST(right)
            ValueSet new_chars = first - cur_first_set;
            if (!new_chars.empty()) {
                cur_first_set |= first;
                change = true;
            }
        }
    } while (change);  // Do until no changes have been made
}

void LRBuilder::genAetaTbl() {
    bool change;
    // Initialize Aeta table
    Aeta_tbl_.resize(grammar_.nonterm_count);
    for (int i = 0; i < grammar_.nonterm_count; i++) {
        Aeta_tbl_[i].clear();
        Aeta_tbl_[i].addValue(i);
    }
    int prod_no, prod_count = (int)grammar_.grammar_idx.size() - 1;
    do {
        change = false;
        // Look through all productions
        for (prod_no = 0; prod_no < prod_count; prod_no++) {
            int left = grammar_.grammar[grammar_.grammar_idx[prod_no]];
            assert(left & maskNonterm);
            if (grammar_.grammar_idx[prod_no + 1] > (grammar_.grammar_idx[prod_no] + 1))  // Nonempty production
            {
                int right = grammar_.grammar[grammar_.grammar_idx[prod_no] + 1];
                if (right & maskNonterm)  // First production symbol is nonterminal
                {
                    for (int nonterm = 0; nonterm < grammar_.nonterm_count; nonterm++) {
                        if (Aeta_tbl_[nonterm].contains(left & maskId) && !Aeta_tbl_[nonterm].contains(right & maskId)) {
                            Aeta_tbl_[nonterm].addValue(right & maskId);
                            change = true;
                        }
                    }
                }
            }
        }
    } while (change);  // Do until no changes have been made
}

void LRBuilder::calcGoto(const State& src, int ch, State& tgt) {
    tgt.clear();
    ValueSet nonkern;

    // Look through source items
    auto it = src.begin();
    while (it != src.end()) {
        int prod_no = it->first.prod_no;
        int next_ch_idx = grammar_.grammar_idx[prod_no] + it->first.pos + 1;
        assert(next_ch_idx <= grammar_.grammar_idx[prod_no + 1]);
        if (next_ch_idx < grammar_.grammar_idx[prod_no + 1])  // Not final position
        {
            int next_ch = grammar_.grammar[next_ch_idx];
            if (next_ch & maskNonterm)  // The next character is nonterminal
                nonkern |= Aeta_tbl_[next_ch & maskId];
            if (next_ch == ch) {
                tgt.emplace(std::piecewise_construct, std::make_tuple(prod_no, it->first.pos + 1),
                            std::tuple<>());  // Add to goto(src, ch)
            }
        }
        it++;
    }

    // Run through nonkernel items
    int prod_no, prod_count = (int)grammar_.grammar_idx.size() - 1;
    for (prod_no = 0; prod_no < prod_count; prod_no++) {
        int left = grammar_.grammar[grammar_.grammar_idx[prod_no]];
        assert(left & maskNonterm);
        if (nonkern.contains(left & maskId) &&  // Is production of nonkernel item
            (grammar_.grammar_idx[prod_no + 1] > (grammar_.grammar_idx[prod_no] + 1)))  // Nonempty production
        {
            int right = grammar_.grammar[grammar_.grammar_idx[prod_no] + 1];
            if (right == ch) {
                tgt.emplace(std::piecewise_construct, std::make_tuple(prod_no, 1),
                            std::tuple<>());  // Add to goto(src, ch)
            }
        }
    }
}

void LRBuilder::calcClosure(const StateItemPos& pos, State& closure) {
    StateItem item;
    item.la.addValue(idDefault);
    calcClosureSet({{pos, item}}, closure);
}

void LRBuilder::calcClosureSet(const State& src, State& closure) {
    ValueSet nonkern;
    std::vector<ValueSet> nonterm_la(grammar_.nonterm_count);
    closure = src;
    // Look through kernel items
    auto it = src.begin();
    while (it != src.end()) {
        int prod_no = it->first.prod_no;
        int next_ch_idx = grammar_.grammar_idx[prod_no] + it->first.pos + 1;
        assert(next_ch_idx <= grammar_.grammar_idx[prod_no + 1]);
        if (next_ch_idx < grammar_.grammar_idx[prod_no + 1])  // Not final position
        {
            int next_ch = grammar_.grammar[next_ch_idx];
            if (next_ch & maskNonterm)  // The next character is nonterminal
            {
                // A -> alpha . B beta
                nonkern.addValue(next_ch & maskId);

                ValueSet la;
                std::vector<int> seq;
                // seq = beta
                std::copy(grammar_.grammar.begin() + next_ch_idx + 1,
                          grammar_.grammar.begin() + grammar_.grammar_idx[prod_no + 1], std::back_inserter(seq));
                calcFirst(seq, la);  // Calculate FIRST(beta);
                if (la.contains(idEmpty)) {
                    la.removeValue(idEmpty);
                    la |= it->second.la;  // Replace $empty with LA(item)
                }
                nonterm_la[next_ch & maskId] |= la;
            }
        }
        it++;
    }

    bool change = false;
    do {
        change = false;
        // Run through nonkernel items
        int prod_no, prod_count = (int)grammar_.grammar_idx.size() - 1;
        for (prod_no = 0; prod_no < prod_count; prod_no++) {
            int left = grammar_.grammar[grammar_.grammar_idx[prod_no]];
            assert(left & maskNonterm);
            if (nonkern.contains(left & maskId) &&  // Is production of nonkernel item
                (grammar_.grammar_idx[prod_no + 1] > (grammar_.grammar_idx[prod_no] + 1)))  // Nonempty production
            {
                int right = grammar_.grammar[grammar_.grammar_idx[prod_no] + 1];
                if (right & maskNonterm)  // The first character is nonterminal
                {
                    // A -> . B beta
                    if (!nonkern.contains(right & maskId)) {
                        nonkern.addValue(right & maskId);
                        change = true;
                    }

                    ValueSet la;
                    std::vector<int> seq;
                    // seq = beta
                    std::copy(grammar_.grammar.begin() + grammar_.grammar_idx[prod_no] + 2,
                              grammar_.grammar.begin() + grammar_.grammar_idx[prod_no + 1], std::back_inserter(seq));
                    calcFirst(seq, la);  // Calculate FIRST(beta);
                    if (la.contains(idEmpty)) {
                        la.removeValue(idEmpty);
                        la |= nonterm_la[left & maskId];  // Replace $empty with LA(item)
                    }
                    ValueSet new_la = la - nonterm_la[right & maskId];
                    if (!new_la.empty()) {
                        nonterm_la[right & maskId] |= new_la;
                        change = true;
                    }
                }
            }
        }
    } while (change);

    // Add nonkernel items
    int prod_no, prod_count = (int)grammar_.grammar_idx.size() - 1;
    for (prod_no = 0; prod_no < prod_count; prod_no++) {
        int left = grammar_.grammar[grammar_.grammar_idx[prod_no]];
        assert(left & maskNonterm);
        if (nonkern.contains(left & maskId)) {  // Is production of nonkernel item
            closure.emplace(std::piecewise_construct, std::make_tuple(prod_no, 0),
                            std::forward_as_tuple(nonterm_la[left & maskId]));
        }
    }
}

bool LRBuilder::addState(const State& s, int& state_idx) {
    int state, state_count = (int)states_.size();
    for (state = 0; state < state_count; state++) {
        if ((states_[state].size() == s.size()) &&
            std::equal(states_[state].begin(), states_[state].end(), s.begin(),
                       [](const auto& i1, const auto& i2) { return i1.first == i2.first; })) {
            state_idx = state;  // Return old state index
            return false;
        }
    }
    // Add new state
    states_.push_back(s);
    action_tbl_.push_back(std::vector<int>(grammar_.token_count));
    goto_tbl_.push_back(std::vector<int>(grammar_.nonterm_count));
    for (int token = 0; token < grammar_.token_count; token++) action_tbl_[state_count][token] = -1;
    for (int nonterm = 0; nonterm < grammar_.nonterm_count; nonterm++) goto_tbl_[state_count][nonterm] = -1;
    state_idx = state_count;
    return true;
}

void LRBuilder::printItemSet(std::ostream& outp, const State& item) {
    auto it = item.begin();
    while (it != item.end()) {
        grammar_.printProduction(outp, it->first.prod_no, it->first.pos);
        outp << " [";
        for (unsigned la_ch : it->second.la) { outp << ' ' << grammar_.symbolText(la_ch); }
        outp << " ]" << std::endl;
        it++;
    }
}

void LRBuilder::printFirstTbl(std::ostream& outp) {
    outp << "---=== FIRST table : ===---" << std::endl << std::endl;
    for (int nonterm = 0; nonterm < grammar_.nonterm_count; nonterm++) {
        outp << "    FIRST(" << grammar_.name_tbl.getName(maskNonterm | nonterm) << ") = { ";
        const ValueSet& first = first_tbl_[nonterm];
        bool colon = false;
        for (unsigned ch : first) {
            if (colon) { outp << ", "; }
            outp << grammar_.name_tbl.getName(ch);
            colon = true;
        }
        outp << " }" << std::endl;
    }
    outp << std::endl;
}

void LRBuilder::printAetaTbl(std::ostream& outp) {
    outp << "---=== Aeta table : ===---" << std::endl << std::endl;
    for (int nonterm = 0; nonterm < grammar_.nonterm_count; nonterm++) {
        outp << "    Aeta(" << grammar_.name_tbl.getName(maskNonterm | nonterm) << ") = { ";
        const ValueSet& Aeta = Aeta_tbl_[nonterm];
        bool colon = false;
        for (unsigned ch : Aeta) {
            if (colon) { outp << ", "; }
            outp << grammar_.name_tbl.getName(maskNonterm | ch);
            colon = true;
        }
        outp << " }" << std::endl;
    }
    outp << std::endl;
}

void LRBuilder::printStates(std::ostream& outp, std::vector<int>& action_idx, std::vector<int>& action_list,
                            std::vector<int>& goto_idx, std::vector<int>& goto_list) {
    outp << "---=== LALR analyser states : ===---" << std::endl << std::endl;
    int state_idx, state_count = (int)states_.size();
    for (state_idx = 0; state_idx < state_count; state_idx++) {
        outp << "State " << state_idx << ":" << std::endl;
        printItemSet(outp, states_[state_idx]);
        outp << std::endl;

        // Action
        int act_idx = action_idx[state_idx];
        while (1) {
            int token = action_list[act_idx++];
            outp << "    " << grammar_.symbolText(token >= 0 ? token : idDefault) << ", ";
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
        for (int nonterm = 0; nonterm < grammar_.nonterm_count; nonterm++) {
            int idx = goto_idx[nonterm];
            while (1) {
                int state_idx2 = goto_list[idx++];
                int new_state_idx = goto_list[idx++];
                if ((state_idx2 == -1) || (state_idx2 == state_idx)) {
                    outp << "    " << grammar_.name_tbl.getName(maskNonterm | nonterm) << ", goto state "
                         << new_state_idx << std::endl;
                    break;
                }
            }
        }
        outp << std::endl;
    }
}
