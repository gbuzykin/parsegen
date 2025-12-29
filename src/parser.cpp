#include "parser.h"

#include <uxs/algorithm.h>

namespace lex_detail {
#include "lex_analyzer.inl"
}

namespace {
template<typename CharT>
CharT* findEol(CharT* text, CharT* boundary) {
    return std::find_if(text, boundary, [](char ch) { return ch == '\n' || ch == '\0'; });
}
std::string_view getNextLine(const char* text, const char* boundary) {
    return std::string_view(text, findEol(text, boundary) - text);
}
}  // namespace

Parser::Parser(uxs::iobuf& input, std::string file_name, Grammar& grammar)
    : input_(input), file_name_(std::move(file_name)), grammar_(grammar) {}

bool Parser::parse() {
    auto pos = input_.seek(0, uxs::seekdir::end);
    if (pos == uxs::iobuf::traits_type::npos()) { return false; }
    std::size_t file_sz = static_cast<std::size_t>(pos);
    text_ = std::make_unique<char[]>(file_sz);

    // Read the whole file
    input_.seek(0);
    std::size_t n_read = input_.read(std::span(text_.get(), file_sz));
    first_ = text_.get();
    last_ = text_.get() + n_read;
    current_line_ = getNextLine(first_, last_);

    state_stack_.reserve(256);
    state_stack_.push_back(lex_detail::sc_initial);

    // Add default start condition
    grammar_.addStartCondition("initial");

    // Load definitions
    int prec = 0;
    int tt = lex();
    do {
        switch (tt) {
            case tt_start: {  // Start condition definition
                if ((tt = lex()) != tt_id) {
                    logSyntaxError(tt);
                    return false;
                }
                if (!grammar_.addStartCondition(std::string(std::get<std::string_view>(tkn_.val)))) {
                    logger::error(*this, tkn_.loc).println("start condition is already defined");
                    return false;
                }
                tt = lex();
            } break;
            case tt_token: {  // Token definition
                if ((tt = lex()) != tt_id) {
                    logSyntaxError(tt);
                    return false;
                }
                if (!grammar_.addToken(std::string(std::get<std::string_view>(tkn_.val))).second) {
                    logger::error(*this, tkn_.loc).println("token is already defined");
                    return false;
                }
                tt = lex();
            } break;
            case tt_action: {  // Action definition
                if ((tt = lex()) != tt_id) {
                    logSyntaxError(tt);
                    return false;
                }
                if (!grammar_.addAction(std::string(std::get<std::string_view>(tkn_.val))).second) {
                    logger::error(*this, tkn_.loc).println("action is already defined");
                    return false;
                }
                tt = lex();
            } break;
            case tt_left:
            case tt_right:
            case tt_nonassoc: {  // Precedence definition
                Assoc assoc = Assoc::kNone;
                if (tt == tt_left) {
                    assoc = Assoc::kLeft;
                } else if (tt == tt_right) {
                    assoc = Assoc::kRight;
                }
                while (true) {
                    unsigned id = 0;
                    switch (tt = lex()) {
                        case tt_id:
                        case tt_internal_id: {
                            id = grammar_.addToken(std::string(std::get<std::string_view>(tkn_.val))).first;
                        } break;
                        case tt_symb: {
                            id = std::get<unsigned>(tkn_.val);
                        } break;
                    }
                    if (id == 0) { break; }

                    if (!grammar_.setTokenPrecAndAssoc(id, prec, assoc)) {
                        logger::error(*this, tkn_.loc).println("token precedence is already defined");
                        return false;
                    }
                }
                prec++;
            } break;
            case tt_option: {  // Option
                if ((tt = lex()) != tt_id) {
                    logSyntaxError(tt);
                    return false;
                }
                std::string_view name = std::get<std::string_view>(tkn_.val);
                if ((tt = lex()) != tt_string) {
                    logSyntaxError(tt);
                    return false;
                }
                options_.emplace(name, std::get<std::string_view>(tkn_.val));
                tt = lex();
            } break;
            case tt_sep: break;
            default: logSyntaxError(tt); return false;
        }
    } while (tt != tt_sep);

    // Load grammar
    do {
        // Read left hand side of the production
        if ((tt = lex()) == tt_id) {
            unsigned lhs = grammar_.addNonterm(std::string(std::get<std::string_view>(tkn_.val))).first;
            if (!isNonterm(lhs)) {
                logger::error(*this, tkn_.loc).println("name is already used for tokens");
                return false;
            }

            bool has_start_condition = false;
            if ((tt = lex()) == '<') {
                has_start_condition = true;
                if ((tt = lex()) != tt_id) {
                    logSyntaxError(tt);
                    return false;
                }

                if (!grammar_.setStartConditionProd(std::get<std::string_view>(tkn_.val),
                                                    grammar_.getProductionCount())) {
                    logger::error(*this, tkn_.loc).println("undefined start condition");
                    return false;
                }

                if ((tt = lex()) != '>') {
                    logSyntaxError(tt);
                    return false;
                }

                tt = lex();
            }

            if (tt != ':') {
                logSyntaxError(tt);
                return false;
            }

            do {
                // Read right hand side of the production
                int prec = -1;
                std::vector<unsigned> rhs;
                rhs.reserve(16);
                do {
                    switch (tt = lex()) {
                        case tt_prec: {  // Production precedence
                            unsigned id = 0;
                            switch (tt = lex()) {
                                case tt_token_id:
                                case tt_internal_id: {
                                    if (auto found_id = grammar_.findSymbolName(std::get<std::string_view>(tkn_.val));
                                        found_id && isToken(*found_id)) {
                                        id = *found_id;
                                    } else {
                                        logger::error(*this, tkn_.loc).println("undefined token");
                                        return false;
                                    }
                                } break;
                                case tt_symb: {
                                    id = std::get<unsigned>(tkn_.val);
                                } break;
                                default: logSyntaxError(tt); return false;
                            }

                            prec = grammar_.getTokenInfo(id).prec;
                            if (prec < 0) {
                                logger::error(*this, tkn_.loc).println("token precedence is not defined");
                                return false;
                            }
                        } break;
                        case tt_id: {  // Nonterminal
                            auto id = grammar_.addNonterm(std::string(std::get<std::string_view>(tkn_.val))).first;
                            if (!isNonterm(id)) {
                                logger::error(*this, tkn_.loc).println("name is already used for tokens or actions");
                                return false;
                            }
                            rhs.push_back(id);
                        } break;
                        case tt_token_id:
                        case tt_predef_id: {  // Token
                            if (tt == tt_predef_id && std::get<std::string_view>(tkn_.val) != "$error") {
                                logSyntaxError(tt);
                                return false;
                            }
                            if (auto found_id = grammar_.findSymbolName(std::get<std::string_view>(tkn_.val));
                                found_id && isToken(*found_id)) {
                                rhs.push_back(*found_id);
                            } else {
                                logger::error(*this, tkn_.loc).println("undefined token");
                                return false;
                            }
                        } break;
                        case tt_symb: {  // Single symbol
                            rhs.push_back(std::get<unsigned>(tkn_.val));
                        } break;
                        case tt_action_id: {  // Action
                            if (auto found_id = grammar_.findActionName(std::get<std::string_view>(tkn_.val));
                                found_id && isAction(*found_id)) {
                                rhs.push_back(*found_id);
                            } else {
                                logger::error(*this, tkn_.loc).println("undefined action");
                                return false;
                            }
                        } break;
                        case '|':
                        case ';': {
                            if (has_start_condition) {
                                has_start_condition = false;
                                if (rhs.empty() || !isToken(rhs.back())) {
                                    logger::error(*this, tkn_.loc)
                                        .println("start production must be terminated with a token");
                                    return false;
                                }
                            }
                            grammar_.addProduction(lhs, std::move(rhs), prec);
                        } break;
                        default: logSyntaxError(tt); return false;
                    }
                } while (tt != '|' && tt != ';');
            } while (tt != ';');
        } else if (tt != tt_sep) {
            logSyntaxError(tt);
            return false;
        }
    } while (tt != tt_sep);

    if (!grammar_.getProductionCount()) {
        logger::error(file_name_).println("no productions defined");
        return false;
    }

    // Check grammar
    const auto& nonterm_used = grammar_.getUsedNonterms();
    const auto& nonterm_defined = grammar_.getDefinedNonterms();
    const auto& start_conditions = grammar_.getStartConditions();
    for (const auto& sc : start_conditions) {
        const auto& prod = grammar_.getProductionInfo(sc.second);
        if (prod.rhs.empty() || !isToken(prod.rhs.back())) {
            logger::error(file_name_)
                .println("implicit start production for `{}` start condition must be terminated with a token", sc.first);
            return false;
        }
        if (nonterm_used.contains(getIndex(prod.lhs))) {
            logger::error(file_name_).println("left part of start production must not be used in other productions");
            return false;
        }
    }

    for (unsigned n : nonterm_defined - nonterm_used) {
        if (!uxs::any_of(start_conditions, [&grammar = grammar_, n](const auto& sc) {
                return grammar.getProductionInfo(sc.second).lhs == makeNontermId(n);
            })) {
            logger::warning(file_name_).println("unused nonterminal `{}`", grammar_.getSymbolName(makeNontermId(n)));
        }
    }
    if (ValueSet undef = nonterm_used - nonterm_defined; !undef.empty()) {
        logger::error(file_name_)
            .println("undefined nonterminal `{}`", grammar_.getSymbolName(makeNontermId(*undef.begin())));
        return false;
    }
    return true;
}

int Parser::lex() {
    char* str_start = nullptr;
    char* str_end = nullptr;
    tkn_.loc = {ln_, col_, col_};

    auto print_unterm_token_msg = [this] { logger::error(*this, tkn_.loc).println("unterminated token"); };
    auto print_zero_escape_char_msg = [this] {
        logger::error(*this, tkn_.loc).println("zero escape character is not allowed");
    };
    auto print_multiple_characters_not_allowed_msg = [this] {
        logger::error(*this, tkn_.loc).println("multiple characters are not allowed");
    };

    while (true) {
        const char* first = first_;
        const char* lexeme = first;
        if (first > text_.get() && *(first - 1) == '\n') {
            current_line_ = getNextLine(first, last_);
            ++ln_, col_ = 1;
            tkn_.loc = {ln_, col_, col_};
        }
        int pat = 0;
        std::size_t llen = 0;
        while (true) {
            const char* last = last_;
            if (state_stack_.avail() < static_cast<std::size_t>(last - first)) { last = first + state_stack_.avail(); }
            auto* sptr = state_stack_.endp();
            pat = lex_detail::lex(first, last, &sptr, &llen, last != last_ ? lex_detail::flag_has_more : 0);
            state_stack_.setsize(sptr - state_stack_.data());
            if (pat >= lex_detail::predef_pat_default) { break; }
            if (last == last_) {
                int sc = state_stack_.back();
                tkn_.loc.col_last = tkn_.loc.col_first;
                if (sc != lex_detail::sc_string && sc != lex_detail::sc_symb) { return tt_eof; }
                print_unterm_token_msg();
                return tt_lexical_error;
            }
            // enlarge state stack and continue analysis
            state_stack_.reserve(llen);
            first = last;
        }
        first_ += llen, col_ += static_cast<unsigned>(llen);
        tkn_.loc.col_last = col_ - 1;

        std::optional<char> escape;
        switch (pat) {
            // ------ escape sequences
            case lex_detail::pat_escape_a: escape = '\a'; break;
            case lex_detail::pat_escape_b: escape = '\b'; break;
            case lex_detail::pat_escape_f: escape = '\f'; break;
            case lex_detail::pat_escape_n: escape = '\n'; break;
            case lex_detail::pat_escape_r: escape = '\r'; break;
            case lex_detail::pat_escape_t: escape = '\t'; break;
            case lex_detail::pat_escape_v: escape = '\v'; break;
            case lex_detail::pat_escape_other: escape = lexeme[1]; break;
            case lex_detail::pat_escape_hex: {
                escape = uxs::dig_v(lexeme[2]);
                if (llen > 3) { *escape = (*escape << 4) + uxs::dig_v(lexeme[3]); }
                if (!*escape) {
                    print_zero_escape_char_msg();
                    return tt_lexical_error;
                }
            } break;
            case lex_detail::pat_escape_oct: {
                escape = uxs::dig_v(lexeme[1]);
                if (llen > 2) { *escape = (*escape << 3) + uxs::dig_v(lexeme[2]); }
                if (llen > 3) { *escape = (*escape << 3) + uxs::dig_v(lexeme[3]); }
                if (!*escape) {
                    print_zero_escape_char_msg();
                    return tt_lexical_error;
                }
            } break;

            // ------ strings
            case lex_detail::pat_string: {
                str_start = str_end = first_;
                state_stack_.push_back(lex_detail::sc_string);
            } break;
            case lex_detail::pat_string_seq: {
                if (str_end != lexeme) { std::copy(lexeme, lexeme + llen, str_end); }
                str_end += llen;
            } break;
            case lex_detail::pat_string_close: {
                tkn_.val = std::string_view(str_start, str_end - str_start);
                state_stack_.pop_back();
                return tt_string;
            } break;

            // ------ symbols
            case lex_detail::pat_symb: {
                tkn_.val = 0u;
                state_stack_.push_back(lex_detail::sc_symb);
            } break;
            case lex_detail::pat_symb_other: {
                if (std::get<unsigned>(tkn_.val)) {
                    print_multiple_characters_not_allowed_msg();
                    return tt_lexical_error;
                }
                tkn_.val = static_cast<unsigned char>(*lexeme);
            } break;
            case lex_detail::pat_symb_close: {
                state_stack_.pop_back();
                return tt_symb;
            } break;

            // ------ identifiers
            case lex_detail::pat_id: {
                tkn_.val = std::string_view(lexeme, llen);
                return tt_id;
            } break;
            case lex_detail::pat_predef_id: {
                tkn_.val = std::string_view(lexeme, llen);
                return tt_predef_id;
            } break;
            case lex_detail::pat_internal_id: {
                tkn_.val = std::string_view(lexeme, llen);
                return tt_internal_id;
            } break;
            case lex_detail::pat_token_id: {  // [id]
                tkn_.val = std::string_view(lexeme + 1, llen - 2);
                return tt_token_id;
            } break;
            case lex_detail::pat_action_id: {  // {id}
                tkn_.val = std::string_view(lexeme + 1, llen - 2);
                return tt_action_id;
            } break;

            // ------ comment
            case lex_detail::pat_comment: {  // Eat up comment
                first_ = findEol(first_, last_);
            } break;

            // ------ other
            case lex_detail::pat_start: return tt_start;
            case lex_detail::pat_token: return tt_token;
            case lex_detail::pat_action: return tt_action;
            case lex_detail::pat_option: return tt_option;
            case lex_detail::pat_left: return tt_left;
            case lex_detail::pat_right: return tt_right;
            case lex_detail::pat_nonassoc: return tt_nonassoc;
            case lex_detail::pat_prec: return tt_prec;
            case lex_detail::pat_sep: return tt_sep;
            case lex_detail::pat_other: return static_cast<unsigned char>(*lexeme);
            case lex_detail::pat_whitespace: tkn_.loc.col_first = col_; break;
            case lex_detail::pat_nl: break;
            case lex_detail::pat_unexpected_nl: {
                print_unterm_token_msg();
                return tt_lexical_error;
            } break;
            default: return tt_eof;
        }

        // Process escape character
        if (escape) {
            if (state_stack_.back() == lex_detail::sc_string) {
                *str_end++ = *escape;
            } else if (!std::get<unsigned>(tkn_.val)) {
                tkn_.val = static_cast<unsigned char>(*escape);
            } else {
                print_multiple_characters_not_allowed_msg();
                return tt_lexical_error;
            }
        }
    }
    return tt_eof;
}

void Parser::logSyntaxError(int tt) const {
    std::string_view msg;
    switch (tt) {
        case tt_eof: msg = "unexpected end of file"; break;
        case tt_lexical_error: return;
        default: msg = "unexpected token"; break;
    }
    logger::error(*this, tkn_.loc).println("{}", msg);
}
