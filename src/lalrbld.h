#pragma once

#include "nametbl.h"
#include "valset.h"

#include <iostream>
#include <set>
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

struct Item {
    int prod_no, pos;
    ValueSet la;
    std::vector<const Item*> accept_la;
    friend bool operator<(const Item& i1, const Item& i2) {
        if (i1.prod_no < i2.prod_no) return true;
        if ((i1.prod_no == i2.prod_no) && (i1.pos < i2.pos)) return true;
        return false;
    };
    friend bool operator==(const Item& i1, const Item& i2) {
        return ((i1.prod_no == i2.prod_no) && (i1.pos == i2.pos));
    };
    friend bool operator!=(const Item& i1, const Item& i2) {
        return ((i1.prod_no != i2.prod_no) || (i1.pos != i2.pos));
    };
};

// LR table builder class
class LRBuilder {
 public:
    const std::string& getErrorString() const { return err_string_; };
    int loadGrammar(std::istream&);
    void buildAnalizer();

 protected:
    std::string err_string_;
    bool output_report_ = false;
    std::string class_name_;
    std::string tkn_file_name_;
    std::string act_file_name_;
    std::string tbl_file_name_;
    std::string report_file_name_;
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

    std::vector<std::set<Item>> states_;
    std::vector<std::vector<int>> action_tbl_;
    std::vector<std::vector<int>> goto_tbl_;
    std::vector<int> compr_action_list_;
    std::vector<int> compr_action_idx_;
    std::vector<int> compr_goto_list_;
    std::vector<int> compr_goto_idx_;

    void numberToStr(int num, std::string& str);
    void calcFirst(const std::vector<int>& seq, ValueSet& first);
    void genFirstTbl();
    void genAetaTbl();
    void calcGoto(const std::set<Item>&, int, std::set<Item>&);
    void calcClosure(const Item& src, std::set<Item>& closure);
    void calcClosureSet(const std::set<Item>& src, std::set<Item>& closure);
    bool addState(const std::set<Item>&, int&);
    void compressTables();
    void outputChar(std::ostream&, int);
    void outputArray(std::ostream&, const std::string&, const std::string&, const std::vector<int>&);
    void outputStringArray(std::ostream&, const std::string&, const std::string&, const std::vector<std::string>&);
    void outputDefinitions(const std::string&, const std::string&);
    void outputTables(const std::string&);

    void printGrammarSymbol(std::ostream&, int);
    void printActionName(std::ostream&, int);
    void printPrecedence(std::ostream&, int);
    void printProduction(std::ostream&, int, int pos = -1);
    void printItemSet(std::ostream&, const std::set<Item>&);
    void printTokens(std::ostream&);
    void printNonterms(std::ostream&);
    void printActions(std::ostream&);
    void printGrammar(std::ostream&);
    void printFirstTbl(std::ostream&);
    void printAetaTbl(std::ostream&);
    void printStates(std::ostream&);

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
