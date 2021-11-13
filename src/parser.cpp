#include "parser.h"

#include "valset.h"

#include <algorithm>

namespace lex_detail {
#include "lex_analyzer.inl"
}

namespace {
char* findEol(char* unread, char* boundary) {
    return std::find_if(unread, boundary, [](char ch) { return ch == '\n' || ch == '\0'; });
}
std::string_view getNextLine(char* unread, char* boundary) {
    return std::string_view(unread, findEol(unread, boundary) - unread);
}
}  // namespace

Parser::Parser(std::istream& input, std::string file_name, Grammar& grammar)
    : input_(input), file_name_(std::move(file_name)), lex_state_stack_({lex_detail::sc_initial}), grammar_(grammar) {}

bool Parser::parse() {
    size_t file_sz = static_cast<size_t>(input_.seekg(0, std::ios_base::end).tellg());
    text_ = std::make_unique<char[]>(file_sz + 1);

    // Read the whole file
    input_.seekg(0);
    input_.read(text_.get(), file_sz);
    text_[input_.gcount()] = '\0';
    lex_ctx_ = lex_detail::CtxData{text_.get(), text_.get(), text_.get(), text_.get() + input_.gcount() + 1};
    current_line_ = getNextLine(lex_ctx_.in_next, lex_ctx_.in_boundary);

    // Load definitions
    int prec = 0;
    int tt = lex();
    do {
        switch (tt) {
            case tt_token: {  // Token definition
                if ((tt = lex()) != tt_id) {
                    logSyntaxError(tt);
                    return false;
                }
                if (!grammar_.addToken(std::string(std::get<std::string_view>(tkn_.val))).second) {
                    logger::error(*this, tkn_.loc) << "name is already used";
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
                    logger::error(*this, tkn_.loc) << "name is already used";
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
                        logger::error(*this, tkn_.loc) << "token precedence is already defined";
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
        // Read left part of the production
        if ((tt = lex()) == tt_id) {
            unsigned left = grammar_.addNonterm(std::string(std::get<std::string_view>(tkn_.val))).first;
            if (!isNonterm(left)) {
                logger::error(*this, tkn_.loc) << "name is already used for tokens or actions";
                return false;
            }

            if (lex() != ':') {
                logSyntaxError(tt);
                return false;
            }

            do {
                // Read right part of the production
                int prec = -1;
                std::vector<unsigned> right;
                right.reserve(16);
                do {
                    switch (tt = lex()) {
                        case tt_prec: {  // Production precedence
                            unsigned id = 0;
                            switch (tt = lex()) {
                                case tt_token_id:
                                case tt_internal_id: {
                                    if (auto found_id = grammar_.findName(std::get<std::string_view>(tkn_.val));
                                        found_id && isToken(*found_id)) {
                                        id = *found_id;
                                    } else {
                                        logger::error(*this, tkn_.loc) << "undefined token";
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
                                logger::error(*this, tkn_.loc) << "token precedence is not defined";
                                return false;
                            }
                        } break;
                        case tt_id: {  // Nonterminal
                            auto id = grammar_.addNonterm(std::string(std::get<std::string_view>(tkn_.val))).first;
                            if (!isNonterm(id)) {
                                logger::error(*this, tkn_.loc) << "name is already used for tokens or actions";
                                return false;
                            }
                            right.push_back(id);
                        } break;
                        case tt_token_id:
                        case tt_predef_id: {  // Token
                            if (tt == tt_predef_id && std::get<std::string_view>(tkn_.val) != "$error") {
                                logSyntaxError(tt);
                                return false;
                            }
                            if (auto found_id = grammar_.findName(std::get<std::string_view>(tkn_.val));
                                found_id && isToken(*found_id)) {
                                right.push_back(*found_id);
                            } else {
                                logger::error(*this, tkn_.loc) << "undefined token";
                                return false;
                            }
                        } break;
                        case tt_symb: {  // Single symbol
                            right.push_back(std::get<unsigned>(tkn_.val));
                        } break;
                        case tt_action_id: {  // Action
                            if (auto found_id = grammar_.findName(std::get<std::string_view>(tkn_.val));
                                found_id && isAction(*found_id)) {
                                right.push_back(*found_id);
                            } else {
                                logger::error(*this, tkn_.loc) << "undefined action";
                                return false;
                            }
                        } break;
                        case '|':
                        case ';': {
                            grammar_.addProduction(left, std::move(right), prec);
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

    // Check grammar
    const auto& nonterm_used = grammar_.getUsedNonterms();
    const auto& nonterm_defined = grammar_.getDefinedNonterms();
    for (unsigned n : nonterm_defined - nonterm_used) {
        logger::warning(file_name_) << "unused nonterminal \'" << grammar_.getName(makeNontermId(n)) << "\'";
    }
    if (ValueSet undef = nonterm_used - nonterm_defined; !undef.empty()) {
        for (unsigned n : undef) {
            logger::error(file_name_) << "undefined nonterminal \'" << grammar_.getName(makeNontermId(n)) << "\'";
        }
        return false;
    }
    return true;
}

int Parser::lex() {
    const char* str_start = nullptr;
    tkn_.loc = {n_line_, n_col_, n_col_};

    while (true) {
        if (lex_ctx_.out_last > text_.get() && *(lex_ctx_.out_last - 1) == '\n') {
            current_line_ = getNextLine(lex_ctx_.in_next, lex_ctx_.in_boundary);
            ++n_line_, n_col_ = 1;
            tkn_.loc = {n_line_, n_col_, n_col_};
        }
        lex_ctx_.out_first = lex_ctx_.out_last;
        int pat = lex_detail::lex(lex_ctx_, lex_state_stack_);
        unsigned lexeme_len = static_cast<unsigned>(lex_ctx_.out_last - lex_ctx_.out_first);
        n_col_ += lexeme_len;
        tkn_.loc.col_last = n_col_ - 1;

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
            case lex_detail::pat_escape_other: escape = lex_ctx_.out_first[1]; break;
            case lex_detail::pat_escape_hex: {
                escape = hdig(lex_ctx_.out_first[2]);
                if (lexeme_len > 3) { *escape = (*escape << 4) + hdig(lex_ctx_.out_first[3]); }
            } break;
            case lex_detail::pat_escape_oct: {
                escape = dig(lex_ctx_.out_first[1]);
                if (lexeme_len > 2) { *escape = (*escape << 3) + dig(lex_ctx_.out_first[2]); }
                if (lexeme_len > 3) { *escape = (*escape << 3) + dig(lex_ctx_.out_first[3]); }
            } break;

            // ------ strings
            case lex_detail::pat_string: {
                str_start = lex_ctx_.out_last;
                lex_state_stack_.push_back(lex_detail::sc_string);
            } break;
            case lex_detail::pat_string_seq: break;
            case lex_detail::pat_string_close: {
                tkn_.val = std::string_view(str_start, lex_ctx_.out_first - str_start);
                lex_state_stack_.pop_back();
                return tt_string;
            } break;

            // ------ symbols
            case lex_detail::pat_symb: {
                tkn_.val = 0;
                lex_state_stack_.push_back(lex_detail::sc_symb);
            } break;
            case lex_detail::pat_symb_other: {
                tkn_.val = static_cast<unsigned char>(*lex_ctx_.out_first);
            } break;
            case lex_detail::pat_symb_close: {
                lex_state_stack_.pop_back();
                return tt_symb;
            } break;

            // ------ identifiers
            case lex_detail::pat_id: {
                tkn_.val = std::string_view(lex_ctx_.out_first, lexeme_len);
                return tt_id;
            } break;
            case lex_detail::pat_predef_id: {
                tkn_.val = std::string_view(lex_ctx_.out_first, lexeme_len);
                return tt_predef_id;
            } break;
            case lex_detail::pat_internal_id: {
                tkn_.val = std::string_view(lex_ctx_.out_first, lexeme_len);
                return tt_internal_id;
            } break;
            case lex_detail::pat_token_id: {  // [id]
                tkn_.val = std::string_view(lex_ctx_.out_first + 1, lexeme_len - 2);
                return tt_token_id;
            } break;
            case lex_detail::pat_action_id: {  // {id}
                tkn_.val = std::string_view(lex_ctx_.out_first + 1, lexeme_len - 2);
                return tt_action_id;
            } break;

            // ------ comment
            case lex_detail::pat_comment: {  // Eat up comment
                lex_ctx_.in_next = findEol(lex_ctx_.in_next, lex_ctx_.in_boundary);
            } break;

                // ------ other
            case lex_detail::pat_token: return tt_token;
            case lex_detail::pat_action: return tt_action;
            case lex_detail::pat_option: return tt_option;
            case lex_detail::pat_left: return tt_left;
            case lex_detail::pat_right: return tt_right;
            case lex_detail::pat_nonassoc: return tt_nonassoc;
            case lex_detail::pat_prec: return tt_prec;
            case lex_detail::pat_sep: return tt_sep;
            case lex_detail::pat_other: return static_cast<unsigned char>(*lex_ctx_.out_first);
            case lex_detail::pat_eof: return tt_eof;
            case lex_detail::pat_whitespace: tkn_.loc.col_first = n_col_; break;
            case lex_detail::pat_nl: break;
            case lex_detail::pat_unterminated_token: return tt_unterminated_token;
            default: return -1;
        }

        // Process escape character
        if (escape) {
            if (lex_state_stack_.back() == lex_detail::sc_string) {
                *lex_ctx_.out_first = *escape;
                lex_ctx_.out_last = lex_ctx_.out_first + 1;
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
        case tt_unterminated_token: msg = "unterminated token"; break;
        default: msg = "unexpected token"; break;
    }
    logger::error(*this, tkn_.loc) << msg;
}
