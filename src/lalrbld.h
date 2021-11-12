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
    void printFirstTbl(std::ostream& outp);
    void printAetaTbl(std::ostream& outp);
    void printStates(std::ostream& outp, std::vector<int>& action_idx, std::vector<int>& action,
                     std::vector<int>& goto_idx, std::vector<int>& goto_list);

 protected:
    struct StateItemPos {
        StateItemPos(int in_prod_no, int in_pos) : prod_no(in_prod_no), pos(in_pos) {}
        int prod_no, pos;
        friend bool operator==(const StateItemPos& p1, const StateItemPos& p2) {
            return p1.prod_no == p2.prod_no && p1.pos == p2.pos;
        };
        friend bool operator<(const StateItemPos& p1, const StateItemPos& p2) {
            return p1.prod_no < p2.prod_no || (p1.prod_no == p2.prod_no && p1.pos < p2.pos);
        };
    };

    struct StateItem {
        StateItem() = default;
        explicit StateItem(const ValueSet& in_la) : la(in_la) {}
        ValueSet la;
        std::vector<const StateItem*> accept_la;
    };

    using State = std::map<StateItemPos, StateItem>;

    const Grammar& grammar_;

    std::vector<ValueSet> first_tbl_;
    std::vector<ValueSet> Aeta_tbl_;

    std::vector<State> states_;
    std::vector<std::vector<int>> action_tbl_;
    std::vector<std::vector<int>> goto_tbl_;

    void calcFirst(const std::vector<int>& seq, ValueSet& first);
    void genFirstTbl();
    void genAetaTbl();
    void calcGoto(const State&, int, State&);
    void calcClosure(const StateItemPos& pos, State& closure);
    void calcClosureSet(const State& src, State& closure);
    bool addState(const State&, int&);

    void printItemSet(std::ostream&, const State&);
};
