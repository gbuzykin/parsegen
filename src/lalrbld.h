#pragma once

#include "grammar.h"
#include "valset.h"

#include <map>
#include <tuple>
#include <vector>

// LR table builder class
class LRBuilder {
 public:
    struct Action {
        enum class Type { kShift = 0, kReduce, kError };
        Type type = Type::kError;
        unsigned val = 0;
        friend bool operator==(const Action& a1, const Action& a2) { return a1.type == a2.type && a1.val == a2.val; }
        friend bool operator!=(const Action& a1, const Action& a2) { return !(a1 == a2); }
    };

    template<typename Ty>
    struct CompressedTable {
        std::vector<unsigned> index;
        std::vector<std::pair<int, Ty>> data;
    };

    explicit LRBuilder(const Grammar& grammar) : grammar_(grammar) {}

    void build();
    unsigned getStateCount() const { return static_cast<unsigned>(states_.size()); }
    unsigned getSRConflictCount() const { return sr_conflict_count_; }
    unsigned getRRConflictCount() const { return rr_conflict_count_; }
    const CompressedTable<Action>& getCompressedActionTable() { return compr_action_tbl_; }
    const CompressedTable<unsigned>& getCompressedGotoTable() { return compr_goto_tbl_; }
    void printFirstTable(uxs::iobuf& outp);
    void printAetaTable(uxs::iobuf& outp);
    void printStates(uxs::iobuf& outp);

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

    struct LookAheadSet {
        struct empty_t {};
        LookAheadSet(empty_t) {}
        explicit LookAheadSet(unsigned id) { la.addValue(id); }
        explicit LookAheadSet(const ValueSet& in_la) : la(in_la) {}
        ValueSet la;
        std::vector<const LookAheadSet*> accept_la_from;
    };

    using PositionSet = std::map<Position, LookAheadSet>;
    template<typename... Args>
    static PositionSet makeSinglePositionSet(const Position& p, Args&&... args) {
        PositionSet s;
        s.emplace(std::piecewise_construct, std::forward_as_tuple(p),
                  std::forward_as_tuple(std::forward<Args>(args)...));
        return s;
    }

    const Grammar& grammar_;

    unsigned sr_conflict_count_ = 0;
    unsigned rr_conflict_count_ = 0;

    std::vector<ValueSet> first_tbl_;
    std::vector<ValueSet> Aeta_tbl_;

    std::vector<PositionSet> states_;
    CompressedTable<Action> compr_action_tbl_;
    CompressedTable<unsigned> compr_goto_tbl_;

    void makeCompressedTables(const std::vector<std::vector<Action>>& action_tbl,
                              const std::vector<std::vector<unsigned>>& goto_tbl);
    ValueSet calcFirst(const std::vector<unsigned>& seq, unsigned pos = 0);
    PositionSet calcGoto(const PositionSet& s, unsigned symb);
    PositionSet calcClosure(const PositionSet& s);
    void buildFirstTable();
    void buildAetaTable();
};
