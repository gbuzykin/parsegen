#include "lalr_builder.h"

#include "logger.h"

#include <uxs/algorithm.h>
#include <uxs/io/oflatbuf.h>

void LalrBuilder::build() {
    buildFirstTable();
    buildAetaTable();

    // Build LR(0) states :

    std::vector<unsigned> pending_states;
    std::vector<std::vector<Action>> action_tbl;
    std::vector<std::vector<unsigned>> goto_tbl;

    states_.reserve(100);
    action_tbl.reserve(100);
    goto_tbl.reserve(100);
    pending_states.reserve(100);

    auto add_state = [&states = states_, &grammar = grammar_, &action_tbl, &goto_tbl](PositionSet s) {
        auto [it, found] = uxs::find_if(states, [&s](const auto& s2) {
            return s2.size() == s.size() &&
                   std::equal(s2.begin(), s2.end(), s.begin(),
                              [](const auto& i1, const auto& i2) { return i1.first == i2.first; });
        });
        if (found) { return std::make_pair(static_cast<unsigned>(it - states.begin()), false); }
        // Add new state
        states.emplace_back(std::move(s));
        action_tbl.emplace_back(grammar.getTokenCount());
        goto_tbl.emplace_back(grammar.getNontermCount(), 0);
        return std::make_pair(static_cast<unsigned>(states.size()) - 1, true);
    };

    // Add initial states
    for (const auto& sc : grammar_.getStartConditions()) {
        pending_states.push_back(
            add_state(makeSinglePositionSet(Position{sc.second, 0}, LookAheadSet::empty_t())).first);
    }

    do {
        unsigned n_state = pending_states.back();
        pending_states.pop_back();
        // Goto for nonterminals
        for (unsigned n = 0; n < grammar_.getNontermCount(); ++n) {
            auto new_state = calcGoto(states_[n_state], makeNontermId(n));
            if (!new_state.empty()) {
                auto [n_new_state, success] = add_state(std::move(new_state));
                if (success) { pending_states.push_back(n_new_state); }
                goto_tbl[n_state][n] = n_new_state;
            }
        }
        // Goto for tokens
        for (unsigned symb = 0; symb < grammar_.getTokenCount(); ++symb) {
            if (grammar_.getTokenInfo(symb).is_used) {
                auto new_state = calcGoto(states_[n_state], symb);
                if (!new_state.empty()) {
                    auto [n_new_state, success] = add_state(std::move(new_state));
                    if (success) { pending_states.push_back(n_new_state); }
                    action_tbl[n_state][symb] = {Action::Type::kShift, n_new_state};
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
            auto closure = calcClosure(makeSinglePositionSet(pos, kTokenDefault));
            for (const auto& [closure_pos, closure_la_set] : closure) {
                const auto& prod = grammar_.getProductionInfo(closure_pos.n_prod);
                if (closure_pos.pos > prod.rhs.size()) {
                    throw std::runtime_error("invalid position");
                } else if (closure_pos.pos == prod.rhs.size()) {
                    continue;
                }
                unsigned goto_state = 0;
                unsigned next_symb = prod.rhs[closure_pos.pos];
                if (isNonterm(next_symb)) {
                    goto_state = goto_tbl[n_state][getIndex(next_symb)];
                } else if (action_tbl[n_state][next_symb].type == Action::Type::kShift) {
                    goto_state = action_tbl[n_state][next_symb].val;
                }
                if (goto_state == 0) { throw std::runtime_error("invalid goto state"); }

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
    auto get_prod_text = [this](unsigned n_prod) {
        uxs::oflatbuf production_text;
        grammar_.printProduction(production_text, n_prod, std::nullopt);
        return std::string(production_text.data(), production_text.size());
    };

    for (unsigned n_state = 0; n_state < states_.size(); ++n_state) {
        for (const auto& [pos, la_set] : calcClosure(states_[n_state])) {
            const auto& prod = grammar_.getProductionInfo(pos.n_prod);
            if (pos.pos > prod.rhs.size()) {
                throw std::runtime_error("invalid position");
            } else if (pos.pos != prod.rhs.size()) {  // Not final position
                continue;
            }
            for (unsigned symb : la_set.la) {
                Action& action = action_tbl[n_state][symb];
                if (action.val == 0) {
                    action = {Action::Type::kReduce, pos.n_prod};
                } else if (action.type == Action::Type::kShift) {
                    // Shift-Reduce conflict
                    const auto& token_info = grammar_.getTokenInfo(symb);
                    int token_prec = token_info.prec;
                    int prod_prec = prod.prec;
                    if (token_prec >= 0 && prod_prec >= 0) {
                        if (prod_prec > token_prec) {
                            action = {Action::Type::kReduce, pos.n_prod};
                        } else if (token_prec == prod_prec) {
                            if (token_info.assoc == Assoc::kLeft) {
                                action = {Action::Type::kReduce, pos.n_prod};
                            } else if (token_info.assoc == Assoc::kNone) {
                                action = {Action::Type::kError};
                            }
                        }
                    } else {
                        logger::warning(grammar_.getFileName())
                            .println("shift/reduce conflict for `{}` production before `{}` look-ahead token",
                                     get_prod_text(pos.n_prod), grammar_.symbolText(symb));
                        ++sr_conflict_count_;
                    }
                } else {  // Reduce-Reduce conflict
                    logger::warning(grammar_.getFileName())
                        .println("reduce/reduce conflict for `{}` and `{}` productions before `{}` look-ahead token",
                                 get_prod_text(action.val), get_prod_text(pos.n_prod), grammar_.symbolText(symb));
                    ++rr_conflict_count_;
                }
            }
        }
    }

    makeCompressedTables(action_tbl, goto_tbl);
}

void LalrBuilder::makeCompressedTables(const std::vector<std::vector<Action>>& action_tbl,
                                       const std::vector<std::vector<unsigned>>& goto_tbl) {
    // Compress action table :

    std::size_t row_size_max = 0, row_size_avg = 0, row_count = 0;
    compr_action_tbl_.index.resize(action_tbl.size());
    compr_action_tbl_.data.reserve(10000);
    for (unsigned n_state = 0; n_state < action_tbl.size(); ++n_state) {
        // Try to find the equal state
        auto equal_it = std::find_if(action_tbl.begin(), action_tbl.begin() + n_state,
                                     [&state = action_tbl[n_state]](const auto& state2) {
                                         return std::equal(state2.begin(), state2.end(), state.begin());
                                     });
        if (equal_it != action_tbl.begin() + n_state) {
            compr_action_tbl_.index[n_state] = compr_action_tbl_.index[equal_it - action_tbl.begin()];
            continue;
        }

        // If reduce action is possible for this state we can replace all
        // error actions with any of possible reduce actions.
        // The error will be reported later after all reductions are made.

        // Build histograms for shift and reduce actions, count error actions as well
        unsigned error_count = 0;
        std::optional<Action> possible_reduce_action;
        std::vector<unsigned> shift_histo(action_tbl.size(), 0);
        std::vector<unsigned> reduce_histo(grammar_.getProductionCount(), 0);
        for (const auto& action : action_tbl[n_state]) {
            switch (action.type) {
                case Action::Type::kError: ++error_count; break;
                case Action::Type::kShift: ++shift_histo[action.val]; break;
                case Action::Type::kReduce: {
                    ++reduce_histo[action.val];
                    if (!possible_reduce_action) { possible_reduce_action = action; }
                } break;
            }
        }

        // Find the most frequent table element including all error actions if they will
        // be converted to any of reduce actions
        auto shift_max_it = std::max_element(shift_histo.begin(), shift_histo.end());
        Action most_freq_action{Action::Type::kShift, static_cast<unsigned>(shift_max_it - shift_histo.begin())};
        if (possible_reduce_action) {
            auto reduce_max_it = std::max_element(reduce_histo.begin(), reduce_histo.end());
            if (*reduce_max_it + error_count > *shift_max_it) {
                most_freq_action = {Action::Type::kReduce, static_cast<unsigned>(reduce_max_it - reduce_histo.begin())};
            }
        } else if (error_count > *shift_max_it) {
            most_freq_action = {Action::Type::kError};
        }

        // Build compressed table
        std::size_t current_table_size = compr_action_tbl_.data.size();
        compr_action_tbl_.index[n_state] = static_cast<unsigned>(current_table_size);
        for (unsigned symb = 0; symb < grammar_.getTokenCount(); ++symb) {
            const auto& action = action_tbl[n_state][symb];
            if (!possible_reduce_action || action.type != Action::Type::kError) {
                if (action != most_freq_action) { compr_action_tbl_.data.emplace_back(symb, action); }
            } else if (most_freq_action.type == Action::Type::kShift) {
                compr_action_tbl_.data.emplace_back(static_cast<int>(symb), *possible_reduce_action);
            }
        }
        // Add default action
        compr_action_tbl_.data.emplace_back(-1, most_freq_action);

        std::size_t row_size = compr_action_tbl_.data.size() - current_table_size;
        row_size_max = std::max(row_size_max, row_size), row_size_avg += row_size, ++row_count;
    }

    row_size_avg /= row_count;

    logger::info(grammar_.getFileName()).println(" - action table row size: max {}, avg {}", row_size_max, row_size_avg);

    // Compress goto table :

    row_size_max = 0, row_size_avg = 0, row_count = 0;
    compr_goto_tbl_.index.resize(grammar_.getNontermCount());
    compr_goto_tbl_.data.reserve(10000);
    for (unsigned n = 0; n < grammar_.getNontermCount(); ++n) {
        // Build histogram
        std::vector<unsigned> histo(goto_tbl.size(), 0);
        for (unsigned n_state = 0; n_state < goto_tbl.size(); ++n_state) {
            unsigned n_new_state = goto_tbl[n_state][n];
            if (n_new_state > 0) { ++histo[n_new_state]; }
        }

        // Find the most frequent state
        auto max_it = std::max_element(histo.begin(), histo.end());
        unsigned n_most_freq_state = static_cast<unsigned>(max_it - histo.begin());

        // Build compressed table
        std::size_t current_table_size = compr_goto_tbl_.data.size();
        compr_goto_tbl_.index[n] = static_cast<unsigned>(current_table_size);
        for (unsigned n_state = 0; n_state < goto_tbl.size(); ++n_state) {
            unsigned n_new_state = goto_tbl[n_state][n];
            if (n_new_state > 0 && n_new_state != n_most_freq_state) {
                compr_goto_tbl_.data.emplace_back(static_cast<int>(n_state), n_new_state);
            }
        }
        compr_goto_tbl_.data.emplace_back(-1, n_most_freq_state);

        std::size_t row_size = compr_goto_tbl_.data.size() - current_table_size;
        row_size_max = std::max(row_size_max, row_size), row_size_avg += row_size, ++row_count;
    }

    row_size_avg /= row_count;

    logger::info(grammar_.getFileName()).println(" - goto table row size: max {}, avg {}", row_size_max, row_size_avg);
}

ValueSet LalrBuilder::calcFirst(const std::vector<unsigned>& seq, unsigned pos) {
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

    // If FIRST(production) includes the empty character add `$empty` to FIRST(lhs)
    if (is_empty_included) { first.addValue(kTokenEmpty); }
    return first;
}

LalrBuilder::PositionSet LalrBuilder::calcGoto(const PositionSet& s, unsigned symb) {
    ValueSet nonkern;
    PositionSet s_next;

    // Look through source items
    for (const auto& [pos, la_set] : s) {
        const auto& prod = grammar_.getProductionInfo(pos.n_prod);
        if (pos.pos > prod.rhs.size()) {
            throw std::runtime_error("invalid position");
        } else if (pos.pos < prod.rhs.size()) {
            unsigned next_symb = prod.rhs[pos.pos];
            if (isNonterm(next_symb)) { nonkern |= Aeta_tbl_[getIndex(next_symb)]; }
            if (next_symb == symb) { s_next.emplace(Position{pos.n_prod, pos.pos + 1}, LookAheadSet::empty_t()); }
        }
    }

    // Run through nonkernel items
    for (unsigned n_prod = 0; n_prod < grammar_.getProductionCount(); ++n_prod) {
        const auto& prod = grammar_.getProductionInfo(n_prod);
        assert(isNonterm(prod.lhs));
        if (nonkern.contains(getIndex(prod.lhs)) && !prod.rhs.empty() && prod.rhs[0] == symb) {
            s_next.emplace(Position{n_prod, 1}, LookAheadSet::empty_t());
        }
    }

    return s_next;
}

LalrBuilder::PositionSet LalrBuilder::calcClosure(const PositionSet& s) {
    ValueSet nonkern;
    std::vector<ValueSet> nonterm_la(grammar_.getNontermCount());

    // Look through kernel items
    for (const auto& [pos, la_set] : s) {
        const auto& prod = grammar_.getProductionInfo(pos.n_prod);
        if (pos.pos > prod.rhs.size()) {
            throw std::runtime_error("invalid position");
        } else if (pos.pos == prod.rhs.size()) {
            continue;
        }
        unsigned next_symb = prod.rhs[pos.pos];
        if (isNonterm(next_symb)) {
            // A -> alpha . B beta
            nonkern.addValue(getIndex(next_symb));
            ValueSet first = calcFirst(prod.rhs, pos.pos + 1);  // Calculate FIRST(beta);
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
            assert(isNonterm(prod.lhs));
            if (nonkern.contains(getIndex(prod.lhs)) && !prod.rhs.empty() && isNonterm(prod.rhs[0])) {
                unsigned n_right = getIndex(prod.rhs[0]);
                // A -> . B beta
                if (!nonkern.contains(n_right)) {
                    nonkern.addValue(n_right);
                    change = true;
                }
                ValueSet first = calcFirst(prod.rhs, 1);  // Calculate FIRST(beta);
                if (first.contains(kTokenEmpty)) {
                    first.removeValue(kTokenEmpty);
                    first |= nonterm_la[getIndex(prod.lhs)];
                }
                ValueSet old_la = nonterm_la[n_right];
                nonterm_la[n_right] |= first;
                if (nonterm_la[n_right] != old_la) { change = true; }
            }
        }
    } while (change);

    auto closure = s;

    // Add nonkernel items
    for (unsigned n_prod = 0; n_prod < grammar_.getProductionCount(); ++n_prod) {
        unsigned lhs = grammar_.getProductionInfo(n_prod).lhs;
        assert(isNonterm(lhs));
        if (nonkern.contains(getIndex(lhs))) {  // Is production of nonkernel item
            closure.emplace(Position{n_prod, 0}, nonterm_la[getIndex(lhs)]);
        }
    }

    return closure;
}

void LalrBuilder::buildFirstTable() {
    first_tbl_.resize(grammar_.getNontermCount());

    bool change;
    do {
        change = false;
        // Look through all productions
        for (const auto& prod : grammar_.getProductions()) {
            assert(isNonterm(prod.lhs));
            unsigned n_left = getIndex(prod.lhs);
            ValueSet first = calcFirst(prod.rhs);
            // Append FIRST(lhs) with FIRST(rhs)
            ValueSet old = first_tbl_[n_left];
            first_tbl_[n_left] |= first;
            if (first_tbl_[n_left] != old) { change = true; }
        }
    } while (change);
}

void LalrBuilder::buildAetaTable() {
    Aeta_tbl_.resize(grammar_.getNontermCount());

    for (unsigned n = 0; n < Aeta_tbl_.size(); ++n) { Aeta_tbl_[n].addValue(n); }

    bool change;
    do {
        change = false;
        for (const auto& prod : grammar_.getProductions()) {
            assert(isNonterm(prod.lhs));
            if (!prod.rhs.empty()) {
                if (isNonterm(prod.rhs[0])) {
                    unsigned n_right = getIndex(prod.rhs[0]);
                    for (auto& Aeta : Aeta_tbl_) {
                        if (Aeta.contains(getIndex(prod.lhs)) && !Aeta.contains(n_right)) {
                            Aeta.addValue(n_right);
                            change = true;
                        }
                    }
                }
            }
        }
    } while (change);
}

void LalrBuilder::printFirstTable(uxs::iobuf& outp) {
    uxs::println(outp, "---=== FIRST table : ===---").endl();
    for (unsigned n = 0; n < first_tbl_.size(); ++n) {
        uxs::print(outp, "    FIRST({}) = {{ ", grammar_.getSymbolName(makeNontermId(n)));
        bool colon = false;
        for (unsigned symb : first_tbl_[n]) {
            if (colon) { uxs::print(outp, ", "); }
            outp.write(grammar_.symbolText(symb));
            colon = true;
        }
        uxs::println(outp, " }}");
    }
    outp.endl();
}

void LalrBuilder::printAetaTable(uxs::iobuf& outp) {
    uxs::println(outp, "---=== Aeta table : ===---").endl();
    for (unsigned n = 0; n < Aeta_tbl_.size(); ++n) {
        uxs::print(outp, "    Aeta({}) = {{ ", grammar_.getSymbolName(makeNontermId(n)));
        bool colon = false;
        for (unsigned symb : Aeta_tbl_[n]) {
            if (colon) { uxs::print(outp, ", "); }
            outp.write(grammar_.getSymbolName(makeNontermId(symb)));
            colon = true;
        }
        uxs::println(outp, " }}");
    }
    outp.endl();
}

void LalrBuilder::printStates(uxs::iobuf& outp) {
    uxs::println(outp, "---=== LALR analyser states : ===---").endl();
    for (unsigned n_state = 0; n_state < states_.size(); n_state++) {
        uxs::println(outp, "State {}:", n_state);
        for (const auto& [pos, la_set] : states_[n_state]) {
            uxs::print(outp, "    ({}) ", pos.n_prod);
            grammar_.printProduction(outp, pos.n_prod, pos.pos);
            uxs::print(outp, " [");
            for (unsigned symb : la_set.la) { outp.put(' ').write(grammar_.symbolText(symb)); }
            uxs::println(outp, " ]");
        }
        outp.endl();

        auto print_action = [&grammar = grammar_, &outp](unsigned token, const Action& action) {
            uxs::print(outp, "    ").write(grammar.symbolText(token));
            uxs::print(outp, ", ");
            switch (action.type) {
                case Action::Type::kShift: uxs::println(outp, "shift and goto state {}", action.val); break;
                case Action::Type::kError: uxs::println(outp, "error"); break;
                case Action::Type::kReduce: {
                    if (action.val > 0) {
                        uxs::println(outp, "reduce using rule {}", action.val);
                    } else {
                        uxs::println(outp, "accept");
                    }
                } break;
            }
        };

        // Action
        auto it = compr_action_tbl_.data.begin() + compr_action_tbl_.index[n_state];
        for (; it->first >= 0; ++it) { print_action(static_cast<unsigned>(it->first), it->second); }
        print_action(kTokenDefault, it->second);
        outp.endl();

        // Goto
        for (unsigned n = 0; n < compr_goto_tbl_.index.size(); ++n) {
            auto it = std::find_if(
                compr_goto_tbl_.data.begin() + compr_goto_tbl_.index[n], compr_goto_tbl_.data.end(),
                [n_state](const auto& item) { return item.first < 0 || item.first == static_cast<int>(n_state); });
            uxs::println(outp, "    {}, goto state {}", grammar_.getSymbolName(makeNontermId(n)), it->second);
        }
        outp.endl();
    }
}
