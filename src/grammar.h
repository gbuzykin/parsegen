#pragma once

#include "nametbl.h"
#include "valset.h"

#include <memory>
#include <optional>
#include <vector>

enum {
    kTokenEnd = 0,
    kCharCount = 0x100,
    kTokenEmpty = kCharCount,
    kTokenDefault,
    kTokenError,
    kNontermAccept = 0x1000,
};

enum class Assoc { kNone = 0, kLeft, kRight };

constexpr bool isNonterm(unsigned id) { return id & 0x1000; }
constexpr bool isAction(unsigned id) { return id & 0x2000; }
constexpr bool isToken(unsigned id) { return !(id & 0x3000); }
constexpr unsigned getIndex(unsigned id) { return id & ~0x3000; }
constexpr unsigned makeNontermId(unsigned index) { return 0x1000 + index; }
constexpr unsigned makeActionId(unsigned index) { return 0x2000 + index; }

class Grammar {
 public:
    struct TokenInfo {
        bool is_used = false;
        int prec = -1;
        Assoc assoc = Assoc::kNone;
    };

    struct ProductionInfo {
        ProductionInfo(unsigned in_lhs, unsigned in_action) : lhs(in_lhs), action(in_action), prec(-1) {}
        ProductionInfo(unsigned in_lhs, std::vector<unsigned> in_rhs, unsigned in_action, int in_prec)
            : lhs(in_lhs), rhs(std::move(in_rhs)), action(in_action), prec(in_prec) {}
        unsigned lhs;
        std::vector<unsigned> rhs;
        unsigned action;
        int prec;
    };

    Grammar();
    std::pair<unsigned, bool> addToken(std::string name);
    std::pair<unsigned, bool> addNonterm(std::string name);
    std::pair<unsigned, bool> addAction(std::string name);
    bool setTokenPrecAndAssoc(unsigned id, int prec, Assoc assoc);
    ProductionInfo& addProduction(unsigned lhs, std::vector<unsigned> rhs, int prec);

    unsigned getTokenCount() const { return static_cast<unsigned>(tokens_.size()); }
    const TokenInfo& getTokenInfo(unsigned id) const { return tokens_[id]; }
    unsigned getNontermCount() const { return nonterm_count_; }
    unsigned getProductionCount() const { return static_cast<unsigned>(productions_.size()); }
    const std::vector<ProductionInfo>& getProductions() const { return productions_; }
    const ProductionInfo& getProductionInfo(unsigned n_prod) const { return productions_[n_prod]; }
    std::optional<unsigned> findName(std::string_view name) const { return name_tbl_.findName(name); }
    std::string_view getName(unsigned id) const;
    std::vector<std::pair<std::string_view, unsigned>> getTokenList();
    std::vector<std::pair<std::string_view, unsigned>> getActionList();
    const ValueSet& getDefinedNonterms() const { return defined_nonterms_; }
    const ValueSet& getUsedNonterms() const { return used_nonterms_; }

    void printTokens(std::ostream& outp) const;
    void printNonterms(std::ostream& outp) const;
    void printActions(std::ostream& outp) const;
    void printGrammar(std::ostream& outp) const;
    void printProduction(std::ostream& outp, unsigned n_prod, std::optional<unsigned> pos) const;
    [[nodiscard]] std::string symbolText(unsigned id) const;

 private:
    unsigned nonterm_count_ = 1;
    unsigned action_count_ = 1;
    std::vector<TokenInfo> tokens_;
    std::vector<ProductionInfo> productions_;
    ValueSet defined_nonterms_;
    ValueSet used_nonterms_;
    NameTable name_tbl_;

    [[nodiscard]] std::string decoratedSymbolText(unsigned id) const;
};
