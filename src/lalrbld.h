#pragma once

#include "grammar.h"
#include "valset.h"

#include <iostream>
#include <map>
#include <vector>

const int kShiftBit = 1;
const int kActionTblFlagCount = 1;

// LR table builder class
class LRBuilder {
 public:
    explicit LRBuilder(const Grammar& grammar) : grammar_(grammar) {}
    void buildAnalizer();
    void compressTables(std::vector<int>& action_idx, std::vector<int>& action_list, std::vector<int>& goto_idx,
                        std::vector<int>& goto_list);
    unsigned getSRConflictCount() const { return sr_conflict_count_; }
    unsigned getRRConflictCount() const { return rr_conflict_count_; }
    void printFirstTbl(std::ostream& outp);
    void printAetaTbl(std::ostream& outp);
    void printStates(std::ostream& outp, std::vector<int>& action_idx, std::vector<int>& action,
                     std::vector<int>& goto_idx, std::vector<int>& goto_list);

 protected:
    struct Position {
        unsigned n_prod, pos;
        friend bool operator==(const Position& p1, const Position& p2) {
            return p1.n_prod == p2.n_prod && p1.pos == p2.pos;
        };
        friend bool operator<(const Position& p1, const Position& p2) {
            return p1.n_prod < p2.n_prod || (p1.n_prod == p2.n_prod && p1.pos < p2.pos);
        };
    };

    struct StateItem {
        ValueSet la;
        std::vector<const StateItem*> accept_la;
    };

    using State = std::map<Position, StateItem>;

    const Grammar& grammar_;

    unsigned sr_conflict_count_ = 0;
    unsigned rr_conflict_count_ = 0;

    std::vector<ValueSet> first_tbl_;
    std::vector<ValueSet> Aeta_tbl_;

    std::vector<State> states_;
    std::vector<std::vector<int>> action_tbl_;
    std::vector<std::vector<int>> goto_tbl_;

    ValueSet calcFirst(const std::vector<unsigned>& seq, unsigned pos = 0);
    State calcGoto(const State& s, unsigned symb);
    State calcClosure(const State& s);
    std::pair<unsigned, bool> addState(const State& s);
    void buildFirstTbl();
    void buildAetaTbl();

    void printItemSet(std::ostream& outp, const State& s);
};
