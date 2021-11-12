#include "lalrbld.h"

#include "lexer.h"

#include <cassert>
#include <fstream>
#include <sstream>

int LRBuilder::loadGrammar(std::istream& inp) {
    name_tbl_.clear();

    // Initialize prodefined tokens
    token_count_ = 259;  // Characters and three specials: $empty, $default, error
    token_strings_.clear();
    token_used_.resize(token_count_);
    token_prec_.resize(token_count_);
    for (int i = 0; i < (int)token_prec_.size(); i++) {
        token_used_[i] = false;
        token_prec_[i] = -1;
    }
    int id = idEnd;
    name_tbl_.insertName("$end", id);
    id = idError;
    name_tbl_.insertName("error", id);
    token_used_[idEnd] = true;
    token_used_[idError] = true;

    output_report_ = false;
    class_name_ = "Parser";
    tkn_file_name_ = "par_tkn.h";
    act_file_name_ = "par_act.h";
    tbl_file_name_ = "par_tbl.inl";
    report_file_name_ = "report.txt";

    Lexer lexer(&inp);
    int tt = -1;

    // Load definitions
    action_count_ = 0;
    int prec = 0;
    while (1) {
        if (tt == -1) tt = lexer.lex();  // Get next token

        if (tt == tt_sep)  // End of definition section
            break;
        switch (tt) {
            case tt_token:  // '%token' definition
            {
                // Get token name
                tt = lexer.lex();
                if (tt != tt_id) return errorSyntax(lexer.getLineNo());
                std::string token_name = lexer.getLVal().str;
                // Add token to the name table
                if (!name_tbl_.insertName(token_name, token_count_))
                    return errorNameRedef(lexer.getLineNo(), token_name);
                // Get token string
                tt = lexer.lex();
                std::string token_string;
                if (tt == tt_string) {
                    token_string = lexer.getLVal().str;
                    tt = -1;
                }
                token_strings_.push_back(token_string);
                token_used_.push_back(true);
                token_prec_.push_back(-1);
                token_count_++;
                if (token_count_ > ValueSet::kMaxValue) throw std::runtime_error("too many tokens");
            } break;
            case tt_action:  // '%action' definition
            {
                // Get action name
                tt = lexer.lex();
                if (tt != tt_id) return errorSyntax(lexer.getLineNo());
                std::string action_name = lexer.getLVal().str;
                tt = -1;
                // Add action to the name table
                int id = maskAction | action_count_;
                if (!name_tbl_.insertName(action_name, id)) return errorNameRedef(lexer.getLineNo(), action_name);
                action_count_++;
                if (action_count_ > ValueSet::kMaxValue) throw std::runtime_error("too many actions");
            } break;
            case tt_option:  // '%option' definition
            {
                tt = lexer.lex();
                if (tt != tt_id) return errorSyntax(lexer.getLineNo());
                std::string opt_id = lexer.getLVal().str;
                int opt_idx = -1;
                if (opt_id == "report")
                    opt_idx = 0;
                else if (opt_id == "class_name")
                    opt_idx = 1;
                else if (opt_id == "tkn_file_name")
                    opt_idx = 2;
                else if (opt_id == "act_file_name")
                    opt_idx = 3;
                else if (opt_id == "tbl_file_name")
                    opt_idx = 4;
                else if (opt_id == "report_file_name")
                    opt_idx = 5;
                else
                    return errorInvOption(lexer.getLineNo(), opt_id);

                if (opt_idx != 0) {
                    tt = lexer.lex();
                    if (tt != '=') return errorSyntax(lexer.getLineNo());
                    tt = lexer.lex();
                    if (tt != tt_string) return errorSyntax(lexer.getLineNo());
                    switch (opt_idx) {
                        case 1:  // 'class_name' option
                            class_name_ = lexer.getLVal().str;
                            break;
                        case 2:  // 'tkn_file_name' option
                            tkn_file_name_ = lexer.getLVal().str;
                            break;
                        case 3:  // 'act_file_name' option
                            act_file_name_ = lexer.getLVal().str;
                            break;
                        case 4:  // 'tbl_file_name' option
                            tbl_file_name_ = lexer.getLVal().str;
                            break;
                        case 5:  // 'report_file_name' option
                            report_file_name_ = lexer.getLVal().str;
                            break;
                    }
                } else  // 'report' option
                    output_report_ = true;
                tt = -1;
            } break;
            case tt_left:      // '%left' definition
            case tt_right:     // '%right' definition
            case tt_nonassoc:  // '%nonassoc' definition
            {
                int assoc = tt;
                while (1) {
                    int id;
                    // Get token
                    tt = lexer.lex();
                    if (tt == tt_id) {
                        id = token_count_;
                        std::string token_name = lexer.getLVal().str;
                        if (name_tbl_.insertName(token_name, id)) {
                            token_strings_.push_back("");
                            token_used_.push_back(true);
                            token_prec_.push_back(-1);
                            token_count_++;
                            if (token_count_ > ValueSet::kMaxValue) throw std::runtime_error("too many tokens");
                        } else if (id == idError)
                            return errorInvUseOfPredefToken(lexer.getLineNo(), id);
                    } else if (tt == tt_symb) {
                        id = lexer.getLVal().i;
                        token_used_[id] = true;
                    } else
                        break;

                    if (token_prec_[id] != -1) return errorPrecRedef(lexer.getLineNo(), id);
                    if (assoc == tt_left)
                        token_prec_[id] = maskLeftAssoc | prec;
                    else if (assoc == tt_right)
                        token_prec_[id] = maskRightAssoc | prec;
                    else
                        token_prec_[id] = prec;
                }
                prec++;
            } break;
            default: return errorSyntax(lexer.getLineNo());
        }
    }

    // clear grammar
    grammar_.clear();
    grammar_idx_.clear();
    act_on_reduce_.clear();
    prod_prec_.clear();
    // Load grammar
    int act_nonterm_count = 0;
    nonterm_count_ = 1;
    ValueSet nonterm_used, nonterm_defined;
    // Add augmenting production
    grammar_idx_.push_back((int)grammar_.size());
    grammar_.push_back(maskNonterm);
    grammar_.push_back(maskNonterm | 1);
    grammar_.push_back(idEnd);
    grammar_idx_.push_back((int)grammar_.size());
    act_on_reduce_.push_back(-1);
    prod_prec_.push_back(-1);
    id = maskNonterm;
    name_tbl_.insertName("$accept", id);

    while (1) {
        // Read left part of the production
        tt = lexer.lex();
        if ((tt == tt_sep) || (tt == 0))
            break;
        else if (tt != tt_id)
            return errorSyntax(lexer.getLineNo());
        std::string left_part_name = lexer.getLVal().str;
        // Try to find name in the name table
        int left_part_id = maskNonterm | nonterm_count_;
        bool ins_res = name_tbl_.insertName(left_part_name, left_part_id);
        if (!ins_res)  // Already present
        {
            // Check for nonterminal
            if (!(left_part_id & maskNonterm)) return errorLeftPartIsNotNonterm(lexer.getLineNo());
        } else  // New name added
            nonterm_count_++;
        nonterm_defined.addValue(left_part_id & maskId);  // Add to the set of defined nonterminals

        // Eat up ':'
        if (lexer.lex() != ':') return errorSyntax(lexer.getLineNo());

        // Read production
        bool stop = false;
        std::vector<int> act_nonterms, act_ids;
        grammar_.push_back(left_part_id);
        int prec = -1;
        do {
            tt = lexer.lex();
            switch (tt) {
                case tt_prec:  // Set production precedence
                {
                    int id = -1;
                    // Get token
                    tt = lexer.lex();
                    if (tt == tt_id) {
                        std::string token_name = lexer.getLVal().str;
                        if (!name_tbl_.findName(token_name, id)) return errorUndefToken(lexer.getLineNo(), token_name);
                        if (id & maskNonterm) return errorUndefToken(lexer.getLineNo(), token_name);
                    } else if (tt == tt_symb)
                        id = lexer.getLVal().i;
                    else
                        return errorSyntax(lexer.getLineNo());

                    if (!token_used_[id] || (token_prec_[id] == -1)) return errorUndefPrec(lexer.getLineNo(), id);
                    prec = token_prec_[id];
                } break;
                case tt_symb:  // Single symbol
                {
                    int id = lexer.getLVal().i;
                    grammar_.push_back(id);
                    token_used_[id] = true;
                } break;
                case tt_act:  // Action
                {
                    // Add action on reduce
                    int act_id = -1;
                    std::string act_name = lexer.getLVal().str;
                    if (!name_tbl_.findName(act_name, act_id)) return errorUndefAction(lexer.getLineNo(), act_name);
                    int id = maskNonterm | nonterm_count_;
                    grammar_.push_back(id);
                    act_nonterms.push_back(id);
                    act_ids.push_back(act_id & maskId);
                    nonterm_count_++;
                    if (nonterm_count_ > ValueSet::kMaxValue) throw std::runtime_error("too many nonterminals");
                } break;
                case tt_id:  // Identifier
                {
                    int id = maskNonterm | nonterm_count_;
                    std::string name = lexer.getLVal().str;
                    if (name_tbl_.insertName(name, id)) {
                        nonterm_count_++;
                        if (nonterm_count_ > ValueSet::kMaxValue) throw std::runtime_error("too many nonterminals");
                    }
                    if (id & maskAction) return errorInvUseOfActName(lexer.getLineNo(), name);
                    grammar_.push_back(id);
                    if (id & maskNonterm) nonterm_used.addValue(id & maskId);  // Add to the set of used nonterminals
                } break;
                case '|':
                case ';': {
                    int i;
                    // Save production action
                    if ((act_nonterms.size() > 0) &&
                        (grammar_[grammar_.size() - 1] == act_nonterms[act_nonterms.size() - 1])) {
                        // Action is at the end of production
                        act_on_reduce_.push_back(act_ids[act_ids.size() - 1]);
                        grammar_.pop_back();
                        act_nonterms.pop_back();
                        act_ids.pop_back();
                        nonterm_count_--;
                    } else
                        act_on_reduce_.push_back(-1);
                    // Calculate default precedence
                    if (prec == -1) {
                        for (i = (int)(grammar_.size() - 1); i > (int)grammar_idx_[grammar_idx_.size() - 1]; i--) {
                            int id = grammar_[i];
                            if (!(id & maskNonterm))  // Is token
                            {
                                assert(token_used_[id]);
                                prec = token_prec_[id];
                                break;
                            }
                        }
                    }
                    prod_prec_.push_back(prec);
                    // Add productions for not end actions
                    for (i = 0; i < (int)act_nonterms.size(); i++) {
                        std::string name;
                        numberToStr(++act_nonterm_count, name);
                        name_tbl_.insertName("@" + name, act_nonterms[i]);
                        grammar_idx_.push_back((int)grammar_.size());
                        grammar_.push_back(act_nonterms[i]);
                        act_on_reduce_.push_back(act_ids[i]);
                        prod_prec_.push_back(-1);
                    }
                    act_nonterms.clear();
                    act_ids.clear();
                    grammar_idx_.push_back((int)grammar_.size());
                    if (tt == '|') {
                        prec = -1;
                        grammar_.push_back(left_part_id);  // Add next production
                    } else
                        stop = true;
                } break;
                default: return errorSyntax(lexer.getLineNo());
            }
        } while (!stop);
    }

    // Add start nonterminal to the set of used nonterminals
    if (grammar_.size() > 2) nonterm_used.addValue(1);

    // Check grammar
    ValueSet undef = nonterm_used - nonterm_defined;
    ValueSet unused = nonterm_defined - nonterm_used;
    if (!undef.empty()) {
        std::string name;
        name_tbl_.getIdName(maskNonterm | undef.getFirstValue(), name);
        return errorUndefNonterm(name);
    } else if (!unused.empty()) {
        std::string name;
        name_tbl_.getIdName(maskNonterm | unused.getFirstValue(), name);
        return errorUnusedProd(name);
    }
    return 0;
}

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
    std::set<Item> new_state;
    Item initial_item;
    initial_item.prod_no = 0;
    initial_item.pos = 0;
    new_state.insert(initial_item);
    addState(new_state, state_idx);
    unmarked_states.push_back(0);

    do {
        int unm_state = unmarked_states.back();
        unmarked_states.pop_back();
        int ch;
        // calcGoto for nonterminals
        for (ch = 0; ch < nonterm_count_; ch++) {
            calcGoto(states_[unm_state], maskNonterm | ch, new_state);
            if (new_state.size() > 0)  // Nonempty state
            {
                if (addState(new_state, state_idx)) unmarked_states.push_back(state_idx);
                goto_tbl_[unm_state][ch] = state_idx;
            }
        }
        // Goto for tokens
        for (ch = 0; ch < token_count_; ch++) {
            if (token_used_[ch]) {
                calcGoto(states_[unm_state], ch, new_state);
                if (new_state.size() > 0)  // Nonempty state
                {
                    if (addState(new_state, state_idx)) unmarked_states.push_back(state_idx);
                    action_tbl_[unm_state][ch] = state_idx;
                }
            }
        }
    } while (unmarked_states.size() > 0);

    // Build lookahead sets:

    // Calculate initial lookahead sets and generate transferings
    assert(states_[0].size() > 0);
    const_cast<Item&>(*states_[0].begin()).la.addValue(0);  // Add $end symbol to the lookahead set of
                                                            // the $accept -> start production
    int state_count = (int)states_.size();
    for (int state_idx = 0; state_idx < state_count; state_idx++) {
        std::set<Item>::const_iterator it = states_[state_idx].begin();
        while (it != states_[state_idx].end()) {
            // item = [B -> gamma . delta, #]
            Item item;
            item.prod_no = it->prod_no;
            item.pos = it->pos;
            item.la.addValue(idDefault);
            std::set<Item> closure;
            // closure = calcClosure(item)
            calcClosure(item, closure);
            std::set<Item>::const_iterator it2 = closure.begin();
            while (it2 != closure.end()) {
                int prod_no = it2->prod_no;
                int next_ch_idx = grammar_idx_[prod_no] + it2->pos + 1;
                assert(next_ch_idx <= grammar_idx_[prod_no + 1]);
                if (next_ch_idx < grammar_idx_[prod_no + 1])  // Not final position
                {
                    int goto_st = -1;
                    int next_ch = grammar_[next_ch_idx];
                    if (next_ch & maskNonterm)  // The next character is nonterminal
                        goto_st = goto_tbl_[state_idx][next_ch & maskId];
                    else  // Token
                        goto_st = action_tbl_[state_idx][next_ch];
                    assert(goto_st != -1);

                    // *it2 is A -> alpha . X beta
                    Item goto_item = *it2;
                    goto_item.pos++;
                    // goto_item is A -> alpha X . beta
                    std::set<Item>::iterator it3 = states_[goto_st].find(goto_item);  // Find goto_item in goto(*it2, X)
                    assert(it3 != states_[goto_st].end());
                    ValueSet la = goto_item.la;
                    if (la.contains(idDefault))  // If '#' belongs la
                    {
                        const_cast<Item&>(*it3).accept_la.push_back(&(*it));
                        la.removeValue(idDefault);
                    }
                    const_cast<Item&>(*it3).la |= la;
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
            std::set<Item>::iterator it = states_[state_idx].begin();
            while (it != states_[state_idx].end()) {
                // Accept lookahead characters
                for (int item = 0; item < (int)it->accept_la.size(); item++) {
                    ValueSet new_la = it->accept_la[item]->la - it->la;
                    if (!new_la.empty()) {
                        const_cast<Item&>(*it).la |= new_la;
                        change = true;
                    }
                }
                it++;
            }
        }
    } while (change);

    // Generate actions :

    int sr_conflict_count = 0;
    int rr_conflict_count = 0;
    for (state_idx = 0; state_idx < state_count; state_idx++) {
        std::set<Item> closure;
        calcClosureSet(states_[state_idx], closure);
        std::set<Item>::const_iterator it = closure.begin();
        while (it != closure.end()) {
            int prod_no = it->prod_no;
            int next_ch_idx = grammar_idx_[prod_no] + it->pos + 1;
            assert(next_ch_idx <= grammar_idx_[prod_no + 1]);
            if (next_ch_idx == grammar_idx_[prod_no + 1])  // Final position
            {
                int ch = it->la.getFirstValue();
                while (ch != -1) {
                    int old_val = action_tbl_[state_idx][ch];
                    if (old_val == -1)
                        action_tbl_[state_idx][ch] = maskReduce | prod_no;  // Reduce(prod_no)
                    else if (!(old_val & maskReduce)) {
                        // Shift-reduce conflict :
                        int prec = token_prec_[ch];
                        int prod_prec = prod_prec_[prod_no];
                        if ((prec != -1) && (prod_prec != -1)) {
                            int cmp = (prod_prec & maskPrec) - (prec & maskPrec);
                            if (cmp == 0) {
                                if (prec & maskLeftAssoc)
                                    action_tbl_[state_idx][ch] = maskReduce | prod_no;  // Reduce(prod_no)
                                else if (!(prec & maskRightAssoc))
                                    action_tbl_[state_idx][ch] = idNonassocError;
                            } else if (cmp > 0)
                                action_tbl_[state_idx][ch] = maskReduce | prod_no;  // Reduce(prod_no)
                        } else
                            sr_conflict_count++;
                    } else {
                        // Reduce-reduce conflict
                        rr_conflict_count++;
                    }
                    ch = it->la.getNextValue(ch);
                }
            }
            it++;
        }
    }

    if (sr_conflict_count > 0)
        std::cout << "Warning: " << sr_conflict_count << " shift/reduce conflict(s) found!" << std::endl;
    if (rr_conflict_count > 0)
        std::cout << "Warning: " << rr_conflict_count << " reduce/reduce conflict(s) found!" << std::endl;

    // Compress states
    compressTables();

    if (output_report_) {
        std::ofstream outp(report_file_name_.c_str());
        if (outp) {
            // Output tables
            printTokens(outp);
            printNonterms(outp);
            printActions(outp);
            printGrammar(outp);
            printFirstTbl(outp);
            printAetaTbl(outp);
            printStates(outp);
        } else
            std::cout << "Warning: cannot open file \'" << report_file_name_ << "\'!" << std::endl;
    }

    // Output definitions
    outputDefinitions(tkn_file_name_, act_file_name_);

    // Output tables
    outputTables(tbl_file_name_);

    // clear temporary arrays
    first_tbl_.clear();
    Aeta_tbl_.clear();
    states_.clear();
    action_tbl_.clear();
    goto_tbl_.clear();
}

void LRBuilder::numberToStr(int num, std::string& str) {
    str.clear();
    while (num) {
        str += '0' + (num % 10);
        num /= 10;
    }
    for (std::string::size_type i = 0; i < str.size() / 2; i++) {
        char t = str[i];
        str[i] = str[str.size() - i - 1];
        str[str.size() - i - 1] = t;
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
    assert(nonterm_count_ > 0);
    first_tbl_.resize(nonterm_count_);
    for (int i = 0; i < (int)first_tbl_.size(); i++) first_tbl_[i].clear();
    do {
        change = false;
        // Look through all productions
        int prod_no, prod_count = (int)grammar_idx_.size() - 1;
        for (prod_no = 0; prod_no < prod_count; prod_no++) {
            int left = grammar_[grammar_idx_[prod_no]];
            assert(left & maskNonterm);
            std::vector<int> right;
            copy(grammar_.begin() + grammar_idx_[prod_no] + 1, grammar_.begin() + grammar_idx_[prod_no + 1],
                 back_inserter(right));
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
    Aeta_tbl_.resize(nonterm_count_);
    for (int i = 0; i < nonterm_count_; i++) {
        Aeta_tbl_[i].clear();
        Aeta_tbl_[i].addValue(i);
    }
    int prod_no, prod_count = (int)grammar_idx_.size() - 1;
    do {
        change = false;
        // Look through all productions
        for (prod_no = 0; prod_no < prod_count; prod_no++) {
            int left = grammar_[grammar_idx_[prod_no]];
            assert(left & maskNonterm);
            if (grammar_idx_[prod_no + 1] > (grammar_idx_[prod_no] + 1))  // Nonempty production
            {
                int right = grammar_[grammar_idx_[prod_no] + 1];
                if (right & maskNonterm)  // First production symbol is nonterminal
                {
                    for (int nonterm = 0; nonterm < nonterm_count_; nonterm++) {
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

void LRBuilder::calcGoto(const std::set<Item>& src, int ch, std::set<Item>& tgt) {
    tgt.clear();
    ValueSet nonkern;

    // Look through source items
    std::set<Item>::const_iterator it = src.begin();
    while (it != src.end()) {
        int prod_no = it->prod_no;
        int next_ch_idx = grammar_idx_[prod_no] + it->pos + 1;
        assert(next_ch_idx <= grammar_idx_[prod_no + 1]);
        if (next_ch_idx < grammar_idx_[prod_no + 1])  // Not final position
        {
            int next_ch = grammar_[next_ch_idx];
            if (next_ch & maskNonterm)  // The next character is nonterminal
                nonkern |= Aeta_tbl_[next_ch & maskId];
            if (next_ch == ch) {
                Item item;
                item.prod_no = prod_no;
                item.pos = it->pos + 1;
                tgt.insert(item);  // Add to goto(src, ch)
            }
        }
        it++;
    }

    // Run through nonkernel items
    int prod_no, prod_count = (int)grammar_idx_.size() - 1;
    for (prod_no = 0; prod_no < prod_count; prod_no++) {
        int left = grammar_[grammar_idx_[prod_no]];
        assert(left & maskNonterm);
        if (nonkern.contains(left & maskId) &&                          // Is production of nonkernel item
            (grammar_idx_[prod_no + 1] > (grammar_idx_[prod_no] + 1)))  // Nonempty production
        {
            int right = grammar_[grammar_idx_[prod_no] + 1];
            if (right == ch) {
                Item item;
                item.prod_no = prod_no;
                item.pos = 1;
                tgt.insert(item);  // Add to goto(src, ch)
            }
        }
    }
}

void LRBuilder::calcClosure(const Item& src, std::set<Item>& closure) {
    std::set<Item> src_set;
    src_set.insert(src);
    calcClosureSet(src_set, closure);
}

void LRBuilder::calcClosureSet(const std::set<Item>& src, std::set<Item>& closure) {
    ValueSet nonkern;
    std::vector<ValueSet> nonterm_la(nonterm_count_);
    closure = src;
    // Look through kernel items
    std::set<Item>::const_iterator it = src.begin();
    while (it != src.end()) {
        int prod_no = it->prod_no;
        int next_ch_idx = grammar_idx_[prod_no] + it->pos + 1;
        assert(next_ch_idx <= grammar_idx_[prod_no + 1]);
        if (next_ch_idx < grammar_idx_[prod_no + 1])  // Not final position
        {
            int next_ch = grammar_[next_ch_idx];
            if (next_ch & maskNonterm)  // The next character is nonterminal
            {
                // A -> alpha . B beta
                nonkern.addValue(next_ch & maskId);

                ValueSet la;
                std::vector<int> seq;
                // seq = beta
                copy(grammar_.begin() + next_ch_idx + 1, grammar_.begin() + grammar_idx_[prod_no + 1],
                     back_inserter(seq));
                calcFirst(seq, la);  // Calculate FIRST(beta);
                if (la.contains(idEmpty)) {
                    la.removeValue(idEmpty);
                    la |= it->la;  // Replace $empty with LA(item)
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
        int prod_no, prod_count = (int)grammar_idx_.size() - 1;
        for (prod_no = 0; prod_no < prod_count; prod_no++) {
            int left = grammar_[grammar_idx_[prod_no]];
            assert(left & maskNonterm);
            if (nonkern.contains(left & maskId) &&                          // Is production of nonkernel item
                (grammar_idx_[prod_no + 1] > (grammar_idx_[prod_no] + 1)))  // Nonempty production
            {
                int right = grammar_[grammar_idx_[prod_no] + 1];
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
                    copy(grammar_.begin() + grammar_idx_[prod_no] + 2, grammar_.begin() + grammar_idx_[prod_no + 1],
                         back_inserter(seq));
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
    int prod_no, prod_count = (int)grammar_idx_.size() - 1;
    for (prod_no = 0; prod_no < prod_count; prod_no++) {
        int left = grammar_[grammar_idx_[prod_no]];
        assert(left & maskNonterm);
        if (nonkern.contains(left & maskId))  // Is production of nonkernel item
        {
            Item item;
            item.prod_no = prod_no;
            item.pos = 0;
            item.la = nonterm_la[left & maskId];
            closure.insert(item);
        }
    }
}

bool LRBuilder::addState(const std::set<Item>& s, int& state_idx) {
    int state, state_count = (int)states_.size();
    for (state = 0; state < state_count; state++) {
        if ((states_[state].size() == s.size()) && equal(states_[state].begin(), states_[state].end(), s.begin())) {
            state_idx = state;  // Return old state index
            return false;
        }
    }
    // Add new state
    states_.push_back(s);
    action_tbl_.push_back(std::vector<int>(token_count_));
    goto_tbl_.push_back(std::vector<int>(nonterm_count_));
    for (int token = 0; token < token_count_; token++) action_tbl_[state_count][token] = -1;
    for (int nonterm = 0; nonterm < nonterm_count_; nonterm++) goto_tbl_[state_count][nonterm] = -1;
    state_idx = state_count;
    return true;
}

void LRBuilder::compressTables() {
    // Compress action table :

    int state, state_count = (int)action_tbl_.size();
    compr_action_list_.clear();
    compr_action_idx_.resize(state_count);
    for (state = 0; state < state_count; state++) {
        // Try to find equal state
        int state2;
        bool found = false;
        for (state2 = 0; state2 < state; state2++) {
            found = equal(action_tbl_[state].begin(), action_tbl_[state].end(), action_tbl_[state2].begin());
            if (found) break;  // Found
        }
        if (!found) {
            const std::vector<int>& line = action_tbl_[state];
            compr_action_idx_[state] = (int)compr_action_list_.size();
            int ch;

            // Are there any reduce actions?
            bool can_reduce = false;
            int allowed_reduce_act = -1;
            int err_count = 0;
            for (ch = 0; ch < token_count_; ch++) {
                int act = line[ch];
                if (act == -1)
                    err_count++;
                else if (!can_reduce && (act & maskReduce)) {
                    allowed_reduce_act = act;
                    can_reduce = true;
                }
            }

            // Find the most frequent action :

            // Build histogram
            std::map<int, int> histo;
            for (ch = 0; ch < token_count_; ch++) {
                int act = line[ch];
                if (!can_reduce || (act != -1)) {
                    int freq = 1;
                    if (can_reduce && (act & maskReduce)) freq += err_count;
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
            for (ch = 0; ch < token_count_; ch++) {
                int act = line[ch];
                if (!can_reduce || (act != -1)) {
                    if (act != most_freq_act) {
                        compr_action_list_.push_back(ch);
                        if (act == idNonassocError) act = -1;
                        compr_action_list_.push_back(act);
                    }
                } else {
                    if (!(most_freq_act & maskReduce)) {
                        compr_action_list_.push_back(ch);
                        compr_action_list_.push_back(allowed_reduce_act);
                    }
                }
            }
            // Add default action
            compr_action_list_.push_back(idDefault);
            compr_action_list_.push_back(most_freq_act);
        } else
            compr_action_idx_[state] = compr_action_idx_[state2];
    }

    // Compress goto table :

    compr_goto_list_.clear();
    compr_goto_idx_.resize(nonterm_count_);
    for (int nonterm = 0; nonterm < nonterm_count_; nonterm++) {
        compr_goto_idx_[nonterm] = (int)compr_goto_list_.size();

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
                compr_goto_list_.push_back(state);
                compr_goto_list_.push_back(state2);
            }
        }
        // Add default state
        compr_goto_list_.push_back(-1);
        compr_goto_list_.push_back(most_freq_state);
    }
}

void LRBuilder::outputChar(std::ostream& outp, int ch) {
    switch (ch) {
        case '\n': outp << "\\n"; break;
        case '\t': outp << "\\t"; break;
        case '\v': outp << "\\v"; break;
        case '\b': outp << "\\b"; break;
        case '\r': outp << "\\r"; break;
        case '\f': outp << "\\f"; break;
        case '\a': outp << "\\a"; break;
        case '\\': outp << "\\\\"; break;
        case '\'': outp << "\\\'"; break;
        case '\"': outp << "\\\""; break;
        default:
            if (((ch >= 1) && (ch <= 0x19)) || (ch == 0x7F))
                outp << "\\x" << ((ch >> 4) & 0xF) << (ch & 0xF);
            else if ((ch >= 1) && (ch <= 0x7E))
                outp << (char)ch;
    }
}

void LRBuilder::outputArray(std::ostream& outp, const std::string& class_name, const std::string& array_name,
                            const std::vector<int>& data) {
    if (data.size() > 0) {
        outp << "int " << class_name << "::" << array_name << "[" << data.size() << "] = {";
        for (std::vector<int>::size_type i = 0; i < data.size(); i++) {
            if ((i & 15) == 0) outp << std::endl << "  ";
            outp << data[i];
            if (i != (data.size() - 1)) outp << ", ";
        }
        outp << std::endl << "};" << std::endl << std::endl;
    } else
        outp << "int " << class_name << "::" << array_name << "[1] = { -1 };" << std::endl << std::endl;
}

void LRBuilder::outputStringArray(std::ostream& outp, const std::string& class_name, const std::string& array_name,
                                  const std::vector<std::string>& data) {
    if (data.size() > 0) {
        outp << "char* " << class_name << "::" << array_name << "[" << data.size() << "] = {";
        for (std::vector<int>::size_type i = 0; i < data.size(); i++) {
            const std::string& str = data[i];
            outp << std::endl << "  \"";
            for (std::string::size_type j = 0; j < str.size(); j++) outputChar(outp, str[j]);
            outp << '\"';
            if (i != (data.size() - 1)) outp << ", ";
        }
        outp << std::endl << "};" << std::endl << std::endl;
    } else
        outp << "char* " << class_name << "::" << array_name
             << "[1] = { "
                " };"
             << std::endl
             << std::endl;
}

void LRBuilder::outputDefinitions(const std::string& tkn_file_name, const std::string& act_file_name) {
    std::ofstream out_tkn(tkn_file_name.c_str());
    if (out_tkn) {
        for (int id = 0x100; id < token_count_; id++) {
            if (token_used_[id]) {
                out_tkn << "static const int tkn_";
                printGrammarSymbol(out_tkn, id);
                out_tkn << " = " << id << ";" << std::endl;
            }
        }
    } else
        std::cout << "Warning: cannot open file \'" << tkn_file_name << "\'!" << std::endl;
    std::ofstream out_act(act_file_name.c_str());
    if (out_act) {
        for (int act = 0; act < action_count_; act++) {
            out_act << "static const int act_";
            printGrammarSymbol(out_act, maskAction | act);
            out_act << " = " << act << ";" << std::endl;
        }
    } else
        std::cout << "Warning: cannot open file \'" << act_file_name << "\'!" << std::endl;
}

void LRBuilder::outputTables(const std::string& file_name) {
    std::ofstream out_tbl(file_name.c_str());
    if (out_tbl) {
        std::vector<int> prod_info;
        int prod_no, prod_count = (int)grammar_idx_.size() - 1;
        for (prod_no = 0; prod_no < prod_count; prod_no++) {
            int left = grammar_[grammar_idx_[prod_no]];
            assert(left & maskNonterm);
            prod_info.push_back(grammar_idx_[prod_no + 1] - grammar_idx_[prod_no] - 1);  // Length
            prod_info.push_back(left);                                                   // Left part
            prod_info.push_back(act_on_reduce_[prod_no]);                                // Action on reduce
        }
        outputArray(out_tbl, class_name_, "m_prod_info", prod_info);

        outputArray(out_tbl, class_name_, "m_action_list", compr_action_list_);
        outputArray(out_tbl, class_name_, "m_action_idx", compr_action_idx_);
        outputArray(out_tbl, class_name_, "m_goto_list", compr_goto_list_);
        outputArray(out_tbl, class_name_, "m_goto_idx", compr_goto_idx_);
        outputStringArray(out_tbl, class_name_, "m_tkn_str", token_strings_);
    } else
        std::cout << "Warning: cannot open file \'" << file_name << "\'!" << std::endl;
}

void LRBuilder::printGrammarSymbol(std::ostream& outp, int id) {
    if ((id >= 1) && (id <= 0xFF)) {
        outp << '\'';
        outputChar(outp, id);
        outp << '\'';
    } else if (id == idEmpty)
        outp << "$empty";
    else if (id == idDefault)
        outp << "$default";
    else {
        std::string name;
        name_tbl_.getIdName(id, name);
        outp << name;
    }
}

void LRBuilder::printActionName(std::ostream& outp, int id) {
    std::string name;
    name_tbl_.getIdName(id | maskAction, name);
    outp << "{" << name << "}";
}

void LRBuilder::printPrecedence(std::ostream& outp, int prec) {
    if (prec != -1) {
        outp << " %prec " << (prec & maskPrec);
        if (prec & maskLeftAssoc)
            outp << " %left";
        else if (prec & maskRightAssoc)
            outp << " %right";
        else
            outp << " %nonassoc";
    }
}

void LRBuilder::printProduction(std::ostream& outp, int prod_no, int pos /*= -1*/) {
    int left = grammar_[grammar_idx_[prod_no]];
    assert(left & maskNonterm);
    std::vector<int> right;
    copy(grammar_.begin() + grammar_idx_[prod_no] + 1, grammar_.begin() + grammar_idx_[prod_no + 1],
         back_inserter(right));
    outp << "    (" << prod_no << ") ";
    printGrammarSymbol(outp, left);
    outp << " -> ";
    int i;
    if (pos != -1) {
        for (i = 0; i < pos; i++) {
            printGrammarSymbol(outp, right[i]);
            outp << " ";
        }
        outp << ". ";
        for (i = pos; i < (int)right.size(); i++) {
            printGrammarSymbol(outp, right[i]);
            outp << " ";
        }
    } else {
        for (i = 0; i < (int)right.size(); i++) {
            printGrammarSymbol(outp, right[i]);
            outp << " ";
        }
    }
}

void LRBuilder::printItemSet(std::ostream& outp, const std::set<Item>& item) {
    std::set<Item>::const_iterator it = item.begin();
    while (it != item.end()) {
        printProduction(outp, it->prod_no, it->pos);
        outp << "[ ";
        int la_ch = it->la.getFirstValue();
        while (la_ch != -1) {
            printGrammarSymbol(outp, la_ch);
            outp << " ";
            la_ch = it->la.getNextValue(la_ch);
        }
        outp << "]" << std::endl;
        it++;
    }
}

void LRBuilder::printTokens(std::ostream& outp) {
    outp << "---=== Tokens : ===---" << std::endl << std::endl;
    for (int id = 0; id < token_count_; id++) {
        if (token_used_[id]) {
            outp << "    ";
            printGrammarSymbol(outp, id);
            outp << " " << id;
            printPrecedence(outp, token_prec_[id]);
            outp << std::endl;
        }
    }
    outp << std::endl;
}

void LRBuilder::printNonterms(std::ostream& outp) {
    outp << "---=== Nonterminals : ===---" << std::endl << std::endl;
    for (int id = maskNonterm; id < (maskNonterm + nonterm_count_); id++) {
        std::string name;
        name_tbl_.getIdName(id, name);
        outp << "    " << name << " " << id << std::endl;
    }
    outp << std::endl;
}

void LRBuilder::printActions(std::ostream& outp) {
    outp << "---=== Actions : ===---" << std::endl << std::endl;
    for (int id = maskAction; id < (maskAction + action_count_); id++) {
        std::string name;
        name_tbl_.getIdName(id, name);
        outp << "    " << name << " " << id << std::endl;
    }
    outp << std::endl;
}

void LRBuilder::printGrammar(std::ostream& outp) {
    outp << "---=== Grammar : ===---" << std::endl << std::endl;
    int prod_no, prod_count = (int)grammar_idx_.size() - 1;
    for (prod_no = 0; prod_no < prod_count; prod_no++) {
        printProduction(outp, prod_no);
        if (act_on_reduce_[prod_no] != -1) {
            outp << " ";
            printActionName(outp, act_on_reduce_[prod_no]);
        }
        printPrecedence(outp, prod_prec_[prod_no]);
        outp << std::endl;
    }
    outp << std::endl;
}

void LRBuilder::printFirstTbl(std::ostream& outp) {
    outp << "---=== FIRST table : ===---" << std::endl << std::endl;
    for (int nonterm = 0; nonterm < nonterm_count_; nonterm++) {
        outp << "    FIRST(";
        printGrammarSymbol(outp, maskNonterm | nonterm);
        outp << ") = { ";
        const ValueSet& first = first_tbl_[nonterm];
        int ch = first.getFirstValue();
        bool colon = false;
        while (ch != -1) {
            if (colon) outp << ", ";
            printGrammarSymbol(outp, ch);
            colon = true;
            ch = first.getNextValue(ch);
        }
        outp << " }" << std::endl;
    }
    outp << std::endl;
}

void LRBuilder::printAetaTbl(std::ostream& outp) {
    outp << "---=== Aeta table : ===---" << std::endl << std::endl;
    for (int nonterm = 0; nonterm < nonterm_count_; nonterm++) {
        outp << "    Aeta(";
        printGrammarSymbol(outp, maskNonterm | nonterm);
        outp << ") = { ";
        const ValueSet& Aeta = Aeta_tbl_[nonterm];
        int ch = Aeta.getFirstValue();
        bool colon = false;
        while (ch != -1) {
            if (colon) outp << ", ";
            printGrammarSymbol(outp, maskNonterm | ch);
            colon = true;
            ch = Aeta.getNextValue(ch);
        }
        outp << " }" << std::endl;
    }
    outp << std::endl;
}

void LRBuilder::printStates(std::ostream& outp) {
    outp << "---=== LALR analyser states : ===---" << std::endl << std::endl;
    int state_idx, state_count = (int)states_.size();
    for (state_idx = 0; state_idx < state_count; state_idx++) {
        outp << "State " << state_idx << ":" << std::endl;
        printItemSet(outp, states_[state_idx]);
        outp << std::endl;

        // Action
        int act_idx = compr_action_idx_[state_idx];
        while (1) {
            outp << "    ";
            int token = compr_action_list_[act_idx++];
            printGrammarSymbol(outp, token);
            outp << ", ";
            int act = compr_action_list_[act_idx++];
            if (act == -1)
                outp << "error" << std::endl;
            else if (act == maskReduce)
                outp << "accept" << std::endl;
            else if (act & maskReduce)
                outp << "reduce using rule " << (act & maskId) << std::endl;
            else
                outp << "shift and goto state " << act << std::endl;
            if (token == idDefault) break;
        }
        outp << std::endl;

        // calcGoto
        for (int nonterm = 0; nonterm < nonterm_count_; nonterm++) {
            int goto_idx = compr_goto_idx_[nonterm];
            while (1) {
                int state_idx2 = compr_goto_list_[goto_idx++];
                int new_state_idx = compr_goto_list_[goto_idx++];
                if ((state_idx2 == -1) || (state_idx2 == state_idx)) {
                    outp << "    ";
                    printGrammarSymbol(outp, maskNonterm | nonterm);
                    outp << ", goto state " << new_state_idx << std::endl;
                    break;
                }
            }
        }
        outp << std::endl;
    }
}

// Error codes:
//     0 - Success;
//     1 - Syntax error;
//     2 - Name redefinition;
//     3 - Left part of the production is not a nonterminal;
//     4 - Undefined action;
//     5 - Not all nonterminals defined;
//     6 - The grammar contains unused productions;
//     7 - Precedende is already defined for token;
//     8 - Undefined token;
//     9 - Precedence is not defined for token;
//    10 - Invalid use of predefined token;
//    11 - Invalid option;

int LRBuilder::errorSyntax(int line_no) {
    std::ostringstream err;
    err << "****Error(" << line_no << "): syntax error.";
    err_string_ = err.str();
    return 1;
}

int LRBuilder::errorNameRedef(int line_no, const std::string& name) {
    std::ostringstream err;
    err << "****Error(" << line_no << "): name <" << name << "> redefinition.";
    err_string_ = err.str();
    return 2;
}

int LRBuilder::errorLeftPartIsNotNonterm(int line_no) {
    std::ostringstream err;
    err << "****Error(" << line_no << "): left part of production is not nonterminal.";
    err_string_ = err.str();
    return 3;
}

int LRBuilder::errorUndefAction(int line_no, const std::string& name) {
    std::ostringstream err;
    err << "****Error(" << line_no << "): undefined action <" << name << ">.";
    err_string_ = err.str();
    return 4;
}

int LRBuilder::errorUndefNonterm(const std::string& name) {
    std::ostringstream err;
    err << "****Error: undefined nonterminal <" << name << ">.";
    err_string_ = err.str();
    return 5;
}

int LRBuilder::errorUnusedProd(const std::string& name) {
    std::ostringstream err;
    err << "****Error: unused production for nonterminal <" << name << ">.";
    err_string_ = err.str();
    return 6;
}

int LRBuilder::errorPrecRedef(int line_no, int id) {
    std::ostringstream err;
    err << "****Error: precedence is already defined for token <";
    printGrammarSymbol(err, id);
    err << ">.";
    err_string_ = err.str();
    return 7;
}

int LRBuilder::errorUndefToken(int line_no, const std::string& name) {
    std::ostringstream err;
    err << "****Error: undefined token <" << name << ">.";
    err_string_ = err.str();
    return 8;
}

int LRBuilder::errorUndefPrec(int line_no, int id) {
    std::ostringstream err;
    err << "****Error: precedence is not defined for token <";
    printGrammarSymbol(err, id);
    err << ">.";
    err_string_ = err.str();
    return 9;
}

int LRBuilder::errorInvUseOfPredefToken(int line_no, int id) {
    std::ostringstream err;
    err << "****Error: invalid use of predefined token <";
    printGrammarSymbol(err, id);
    err << ">.";
    err_string_ = err.str();
    return 10;
}

int LRBuilder::errorInvOption(int line_no, const std::string& opt_id) {
    std::ostringstream err;
    err << "****Error: invalid option '" << opt_id << "'.";
    err_string_ = err.str();
    return 11;
}

int LRBuilder::errorInvUseOfActName(int line_no, const std::string& name) {
    std::ostringstream err;
    err << "****Error: invalid use of action name '" << name << "'.";
    err_string_ = err.str();
    return 12;
}
