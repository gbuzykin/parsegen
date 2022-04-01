#include "parser.h"

#include "util/algorithm.h"
#include "valset.h"

namespace lex_detail {
#include "lex_analyzer.inl"
}

namespace {
const char* findEol(const char* text, const char* boundary) {
    return std::find_if(text, boundary, [](char ch) { return ch == '\n' || ch == '\0'; });
}
std::string_view getNextLine(const char* text, const char* boundary) {
    return std::string_view(text, findEol(text, boundary) - text);
}
}  // namespace

Parser::Parser(util::iobuf& input, std::string file_name, Grammar& grammar)
    : input_(input), file_name_(std::move(file_name)), grammar_(grammar) {}

bool Parser::parse() {
    std::streampos pos = input_.seek(0, util::seekdir::kEnd);
    if (pos < 0) { return false; }
    size_t file_sz = static_cast<size_t>(pos);
    text_ = std::make_unique<char[]>(file_sz);

    // Read the whole file
    input_.seek(0);
    size_t n_read = input_.read(util::as_span(text_.get(), file_sz));
    in_ctx_.first = text_top_ = text_.get();
    in_ctx_.last = text_.get() + n_read;
    current_line_ = getNextLine(in_ctx_.first, in_ctx_.last);

    lex_state_stack_.reserve(256);
    lex_state_stack_.push_back(lex_detail::sc_initial);

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
                    logger::error(*this, tkn_.loc).format("start condition is already defined");
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
                    logger::error(*this, tkn_.loc).format("token is already defined");
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
                    logger::error(*this, tkn_.loc).format("action is already defined");
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
                        logger::error(*this, tkn_.loc).format("token precedence is already defined");
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
                logger::error(*this, tkn_.loc).format("name is already used for tokens");
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
                    logger::error(*this, tkn_.loc).format("undefined start condition");
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
                                        logger::error(*this, tkn_.loc).format("undefined token");
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
                                logger::error(*this, tkn_.loc).format("token precedence is not defined");
                                return false;
                            }
                        } break;
                        case tt_id: {  // Nonterminal
                            auto id = grammar_.addNonterm(std::string(std::get<std::string_view>(tkn_.val))).first;
                            if (!isNonterm(id)) {
                                logger::error(*this, tkn_.loc).format("name is already used for tokens or actions");
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
                                logger::error(*this, tkn_.loc).format("undefined token");
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
                                logger::error(*this, tkn_.loc).format("undefined action");
                                return false;
                            }
                        } break;
                        case '|':
                        case ';': {
                            if (has_start_condition) {
                                has_start_condition = false;
                                if (rhs.empty() || !isToken(rhs.back())) {
                                    logger::error(*this, tkn_.loc)
                                        .format("start production must be terminated with a token");
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
        logger::error(file_name_).format("no productions defined");
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
                .format("implicit start production for `{}` start condition must be terminated with a token", sc.first);
            return false;
        }
        if (nonterm_used.contains(getIndex(prod.lhs))) {
            logger::error(file_name_).format("left part of start production must not be used in other productions");
            return false;
        }
    }

    for (unsigned n : nonterm_defined - nonterm_used) {
        if (!util::any_of(start_conditions, [&grammar = grammar_, n](const auto& sc) {
                return grammar.getProductionInfo(sc.second).lhs == makeNontermId(n);
            })) {
            logger::warning(file_name_).format("unused nonterminal `{}`", grammar_.getSymbolName(makeNontermId(n)));
        }
    }
    if (ValueSet undef = nonterm_used - nonterm_defined; !undef.empty()) {
        logger::error(file_name_)
            .format("undefined nonterminal `{}`", grammar_.getSymbolName(makeNontermId(*undef.begin())));
        return false;
    }
    return true;
}

int Parser::lex() {
    const char* str_start = nullptr;
    tkn_.loc = {in_ctx_.ln, in_ctx_.col, in_ctx_.col};

    while (true) {
        unsigned llen = 0;
        const char* lexeme = in_ctx_.first;
        if (lexeme > text_.get() && *(lexeme - 1) == '\n') {
            current_line_ = getNextLine(lexeme, in_ctx_.last);
            ++in_ctx_.ln, in_ctx_.col = 1;
            tkn_.loc = {in_ctx_.ln, in_ctx_.col, in_ctx_.col};
        }
        int pat = lex_detail::lex(lexeme, in_ctx_.last, lex_state_stack_, llen, false);
        if (pat == lex_detail::err_end_of_input) {
            int sc = lex_state_stack_.back();
            tkn_.loc.col_last = tkn_.loc.col_first;
            if (sc == lex_detail::sc_string || sc == lex_detail::sc_symb) { return tt_unterm_token; }
            return tt_eof;
        }
        in_ctx_.first += llen, in_ctx_.col += llen;
        tkn_.loc.col_last = in_ctx_.col - 1;

        auto store_id_text = [&text = text_top_](const char* first, unsigned len) {
            text = std::copy(first, first + len, text);
            return std::string_view(text - len, len);
        };

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
                escape = util::dig_v<16>(lexeme[2]);
                if (llen > 3) { *escape = (*escape << 4) + util::dig_v<16>(lexeme[3]); }
            } break;
            case lex_detail::pat_escape_oct: {
                escape = util::dig_v<8>(lexeme[1]);
                if (llen > 2) { *escape = (*escape << 3) + util::dig_v<8>(lexeme[2]); }
                if (llen > 3) { *escape = (*escape << 3) + util::dig_v<8>(lexeme[3]); }
            } break;

            // ------ strings
            case lex_detail::pat_string: {
                str_start = text_top_;
                lex_state_stack_.push_back(lex_detail::sc_string);
            } break;
            case lex_detail::pat_string_seq: {
                text_top_ = std::copy(lexeme, lexeme + llen, text_top_);
            } break;
            case lex_detail::pat_string_close: {
                tkn_.val = std::string_view(str_start, text_top_ - str_start);
                lex_state_stack_.pop_back();
                return tt_string;
            } break;

            // ------ symbols
            case lex_detail::pat_symb: {
                tkn_.val = 0;
                lex_state_stack_.push_back(lex_detail::sc_symb);
            } break;
            case lex_detail::pat_symb_other: {
                tkn_.val = static_cast<unsigned char>(*lexeme);
            } break;
            case lex_detail::pat_symb_close: {
                lex_state_stack_.pop_back();
                return tt_symb;
            } break;

            // ------ identifiers
            case lex_detail::pat_id: {
                tkn_.val = store_id_text(lexeme, llen);
                return tt_id;
            } break;
            case lex_detail::pat_predef_id: {
                tkn_.val = store_id_text(lexeme, llen);
                return tt_predef_id;
            } break;
            case lex_detail::pat_internal_id: {
                tkn_.val = store_id_text(lexeme, llen);
                return tt_internal_id;
            } break;
            case lex_detail::pat_token_id: {  // [id]
                tkn_.val = store_id_text(lexeme + 1, llen - 2);
                return tt_token_id;
            } break;
            case lex_detail::pat_action_id: {  // {id}
                tkn_.val = store_id_text(lexeme + 1, llen - 2);
                return tt_action_id;
            } break;

            // ------ comment
            case lex_detail::pat_comment: {  // Eat up comment
                in_ctx_.first = findEol(in_ctx_.first, in_ctx_.last);
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
            case lex_detail::pat_whitespace: tkn_.loc.col_first = in_ctx_.col; break;
            case lex_detail::pat_nl: break;
            case lex_detail::pat_unterm_token: return tt_unterm_token;
            default: return -1;
        }

        // Process escape character
        if (escape) {
            if (lex_state_stack_.back() == lex_detail::sc_string) {
                *text_top_++ = *escape;
            } else {
                tkn_.val = static_cast<unsigned char>(*escape);
            }
        }
    }
    return tt_eof;
}

void Parser::logSyntaxError(int tt) const {
    std::string_view msg;
    switch (tt) {
        case tt_eof: msg = "unexpected end of file"; break;
        case tt_unterm_token: msg = "unterminated token"; break;
        default: msg = "unexpected token"; break;
    }
    logger::error(*this, tkn_.loc).format(msg);
}
