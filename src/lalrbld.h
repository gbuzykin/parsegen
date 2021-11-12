#pragma once

#include "nametbl.h"
#include "valset.h"

#include <iostream>
#include <map>
#include <unordered_map>
#include <vector>

const int idEnd = 0x000;
const int idEmpty = 0x100;
const int idDefault = 0x101;
const int idError = 0x102;

const int maskNonterm = 0x1000;
const int maskAction = 0x2000;
const int maskId = 0x0FFF;

const int maskLeftAssoc = 0x1000;
const int maskRightAssoc = 0x2000;
const int maskPrec = 0x0FFF;

const int maskReduce = 0x1000;
const int idNonassocError = 0x2000;

// LR table builder class
class LRBuilder {
 public:
    const std::string& getErrorString() const { return err_string_; };
    int loadGrammar(std::istream&);
    void buildAnalizer();
    void compressTables(std::vector<int>& action_idx, std::vector<int>& action_list, std::vector<int>& goto_idx,
                        std::vector<int>& goto_list);
    std::vector<int> getProdInfo();
    std::vector<std::pair<std::string, int>> getUsedTokenList();
    std::vector<std::string> getActionList();
    std::vector<std::string> getTokenText();
    void printTokens(std::ostream& outp);
    void printNonterms(std::ostream& outp);
    void printActions(std::ostream& outp);
    void printGrammar(std::ostream& outp);
    void printFirstTbl(std::ostream& outp);
    void printAetaTbl(std::ostream& outp);
    void printStates(std::ostream& outp, std::vector<int>& action_idx, std::vector<int>& action_list,
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

    std::unordered_map<std::string, std::string> options_;

    std::string err_string_;
    int token_count_ = 0;
    int nonterm_count_ = 0;
    int action_count_ = 0;

    NameTable name_tbl_;
    std::vector<bool> token_used_;
    std::vector<std::string> token_strings_;
    std::vector<int> grammar_;
    std::vector<int> grammar_idx_;
    std::vector<int> act_on_reduce_;
    std::vector<int> token_prec_;
    std::vector<int> prod_prec_;

    std::vector<ValueSet> first_tbl_;
    std::vector<ValueSet> Aeta_tbl_;

    std::vector<State> states_;
    std::vector<std::vector<int>> action_tbl_;
    std::vector<std::vector<int>> goto_tbl_;

    void numberToStr(int num, std::string& str);
    void calcFirst(const std::vector<int>& seq, ValueSet& first);
    void genFirstTbl();
    void genAetaTbl();
    void calcGoto(const State&, int, State&);
    void calcClosure(const StateItemPos& pos, int tk, State& closure);
    void calcClosureSet(const State& src, State& closure);
    bool addState(const State&, int&);

    [[nodiscard]] std::string grammarSymbolText(int id);
    [[nodiscard]] std::string actionNameText(int id);
    [[nodiscard]] std::string precedenceText(int prec);

    void printProduction(std::ostream&, int, int pos = -1);
    void printItemSet(std::ostream&, const State&);

    int errorSyntax(int);
    int errorNameRedef(int, const std::string&);
    int errorLeftPartIsNotNonterm(int);
    int errorUndefAction(int, const std::string&);
    int errorUndefNonterm(const std::string&);
    int errorUnusedProd(const std::string&);
    int errorPrecRedef(int, int);
    int errorUndefToken(int, const std::string&);
    int errorUndefPrec(int, int);
    int errorInvUseOfPredefToken(int, int);
    int errorInvOption(int, const std::string&);
    int errorInvUseOfActName(int, const std::string&);
};
