#include "parser.h"

#include "valset.h"

#include <sstream>

// Include tables
#include "lex_tbl.inl"

int Parser::parse() {
    grammar_.name_tbl.clear();

    // Initialize prodefined tokens
    grammar_.token_count = 259;  // Characters and three specials: $empty, $default, error
    grammar_.token_used.resize(grammar_.token_count);
    grammar_.token_prec.resize(grammar_.token_count);
    for (int i = 0; i < (int)grammar_.token_prec.size(); i++) {
        grammar_.token_used[i] = false;
        grammar_.token_prec[i] = -1;
    }
    grammar_.name_tbl.insertName("$end", idEnd);
    grammar_.name_tbl.insertName("error", idError);
    grammar_.token_used[idEnd] = true;
    grammar_.token_used[idError] = true;

    int tt = -1;

    // Load definitions
    grammar_.action_count = 1;
    int prec = 0;
    while (1) {
        if (tt == -1) tt = lex();  // Get next token

        if (tt == tt_sep)  // End of definition section
            break;
        switch (tt) {
            case tt_token:  // '%token' definition
            {
                // Get token name
                tt = lex();
                if (tt != tt_id) return errorSyntax(line_no_);
                std::string token_name = lval_.str;
                // Add token to the name table
                if (!grammar_.name_tbl.insertName(token_name, grammar_.token_count).second) {
                    return errorNameRedef(line_no_, token_name);
                }
                // Get token string
                tt = lex();
                std::string token_string;
                if (tt == tt_string) {
                    token_string = lval_.str;
                    tt = -1;
                }
                grammar_.token_used.push_back(true);
                grammar_.token_prec.push_back(-1);
                grammar_.token_count++;
                if (grammar_.token_count > ValueSet::kMaxValue) throw std::runtime_error("too many tokens");
            } break;
            case tt_action:  // '%action' definition
            {
                // Get action name
                tt = lex();
                if (tt != tt_id) return errorSyntax(line_no_);
                std::string action_name = lval_.str;
                tt = -1;
                // Add action to the name table
                if (!grammar_.name_tbl.insertName(action_name, maskAction | grammar_.action_count).second) {
                    return errorNameRedef(line_no_, action_name);
                }
                grammar_.action_count++;
                if (grammar_.action_count > ValueSet::kMaxValue) throw std::runtime_error("too many actions");
            } break;
            case tt_option:  // '%option' definition
            {
                tt = lex();
                if (tt != tt_id) return errorSyntax(line_no_);
                std::string opt_id(lval_.str);
                tt = lex();
                if (tt != tt_string) return errorSyntax(line_no_);
                options_.emplace(std::move(opt_id), lval_.str);
                tt = -1;
            } break;
            case tt_left:      // '%left' definition
            case tt_right:     // '%right' definition
            case tt_nonassoc:  // '%nonassoc' definition
            {
                int assoc = tt;
                while (1) {
                    int id;
                    // Get token
                    tt = lex();
                    if (tt == tt_id) {
                        std::string token_name = lval_.str;
                        bool success = true;
                        std::tie(id, success) = grammar_.name_tbl.insertName(token_name, grammar_.token_count);
                        if (success) {
                            grammar_.token_used.push_back(true);
                            grammar_.token_prec.push_back(-1);
                            grammar_.token_count++;
                            if (grammar_.token_count > ValueSet::kMaxValue) {
                                throw std::runtime_error("too many tokens");
                            }
                        } else if (id == idError) {
                            return errorInvUseOfPredefToken(line_no_, id);
                        }
                    } else if (tt == tt_symb) {
                        id = lval_.i;
                        grammar_.token_used[id] = true;
                    } else {
                        break;
                    }

                    if (grammar_.token_prec[id] != -1) return errorPrecRedef(line_no_, id);
                    if (assoc == tt_left)
                        grammar_.token_prec[id] = maskLeftAssoc | prec;
                    else if (assoc == tt_right)
                        grammar_.token_prec[id] = maskRightAssoc | prec;
                    else
                        grammar_.token_prec[id] = prec;
                }
                prec++;
            } break;
            default: return errorSyntax(line_no_);
        }
    }

    // clear grammar
    grammar_.grammar.clear();
    grammar_.grammar_idx.clear();
    grammar_.act_on_reduce.clear();
    grammar_.prod_prec.clear();
    // Load grammar
    int act_nonterm_count = 0;
    grammar_.nonterm_count = 1;
    ValueSet nonterm_used, nonterm_defined;
    // Add augmenting production
    grammar_.grammar_idx.push_back((int)grammar_.grammar.size());
    grammar_.grammar.push_back(maskNonterm);
    grammar_.grammar.push_back(maskNonterm | 1);
    grammar_.grammar.push_back(idEnd);
    grammar_.grammar_idx.push_back((int)grammar_.grammar.size());
    grammar_.act_on_reduce.push_back(0);
    grammar_.prod_prec.push_back(-1);
    grammar_.name_tbl.insertName("$accept", maskNonterm);

    while (1) {
        // Read left part of the production
        tt = lex();
        if ((tt == tt_sep) || (tt == 0))
            break;
        else if (tt != tt_id)
            return errorSyntax(line_no_);
        std::string left_part_name = lval_.str;
        // Try to find name in the name table
        auto [left_part_id, success] = grammar_.name_tbl.insertName(left_part_name,
                                                                    maskNonterm | grammar_.nonterm_count);
        if (success) {
            grammar_.nonterm_count++;
        } else if (!(left_part_id & maskNonterm)) {  // Check for nonterminal
            return errorLeftPartIsNotNonterm(line_no_);
        }
        nonterm_defined.addValue(left_part_id & maskId);  // Add to the set of defined nonterminals

        // Eat up ':'
        if (lex() != ':') return errorSyntax(line_no_);

        // Read production
        bool stop = false;
        std::vector<int> act_nonterms, act_ids;
        grammar_.grammar.push_back(left_part_id);
        int prec = -1;
        do {
            tt = lex();
            switch (tt) {
                case tt_prec:  // Set production precedence
                {
                    int id = -1;
                    // Get token
                    tt = lex();
                    if (tt == tt_id) {
                        std::string token_name = lval_.str;
                        if (auto found_id = grammar_.name_tbl.findName(token_name); found_id) {
                            id = *found_id;
                            if (id & maskNonterm) { return errorUndefToken(line_no_, token_name); }
                        } else {
                            return errorUndefToken(line_no_, token_name);
                        }
                    } else if (tt == tt_symb) {
                        id = lval_.i;
                    } else {
                        return errorSyntax(line_no_);
                    }

                    if (!grammar_.token_used[id] || (grammar_.token_prec[id] == -1))
                        return errorUndefPrec(line_no_, id);
                    prec = grammar_.token_prec[id];
                } break;
                case tt_symb:  // Single symbol
                {
                    int id = lval_.i;
                    grammar_.grammar.push_back(id);
                    grammar_.token_used[id] = true;
                } break;
                case tt_act:  // Action
                {
                    // Add action on reduce
                    std::string act_name = lval_.str;
                    if (auto found_id = grammar_.name_tbl.findName(act_name); found_id) {
                        int id = maskNonterm | grammar_.nonterm_count;
                        grammar_.grammar.push_back(id);
                        act_nonterms.push_back(id);
                        act_ids.push_back(*found_id & maskId);
                    } else {
                        return errorUndefAction(line_no_, act_name);
                    }
                    grammar_.nonterm_count++;
                    if (grammar_.nonterm_count > ValueSet::kMaxValue) throw std::runtime_error("too many nonterminals");
                } break;
                case tt_id:  // Identifier
                {
                    std::string name = lval_.str;
                    auto [id, success] = grammar_.name_tbl.insertName(name, maskNonterm | grammar_.nonterm_count);
                    if (success) {
                        grammar_.nonterm_count++;
                        if (grammar_.nonterm_count > ValueSet::kMaxValue) {
                            throw std::runtime_error("too many nonterminals");
                        }
                    } else if (id & maskAction) {
                        return errorInvUseOfActName(line_no_, name);
                    }
                    grammar_.grammar.push_back(id);
                    if (id & maskNonterm) {  // Add to the set of used nonterminals
                        nonterm_used.addValue(id & maskId);
                    }
                } break;
                case '|':
                case ';': {
                    int i;
                    // Save production action
                    if ((act_nonterms.size() > 0) &&
                        (grammar_.grammar[grammar_.grammar.size() - 1] == act_nonterms[act_nonterms.size() - 1])) {
                        // Action is at the end of production
                        grammar_.act_on_reduce.push_back(act_ids[act_ids.size() - 1]);
                        grammar_.grammar.pop_back();
                        act_nonterms.pop_back();
                        act_ids.pop_back();
                        grammar_.nonterm_count--;
                    } else
                        grammar_.act_on_reduce.push_back(0);
                    // Calculate default precedence
                    if (prec == -1) {
                        for (i = (int)(grammar_.grammar.size() - 1);
                             i > (int)grammar_.grammar_idx[grammar_.grammar_idx.size() - 1]; i--) {
                            int id = grammar_.grammar[i];
                            if (!(id & maskNonterm))  // Is token
                            {
                                assert(grammar_.token_used[id]);
                                prec = grammar_.token_prec[id];
                                break;
                            }
                        }
                    }
                    grammar_.prod_prec.push_back(prec);
                    // Add productions for not end actions
                    for (i = 0; i < (int)act_nonterms.size(); i++) {
                        std::string name = std::to_string(++act_nonterm_count);
                        grammar_.name_tbl.insertName("@" + name, act_nonterms[i]);
                        grammar_.grammar_idx.push_back((int)grammar_.grammar.size());
                        grammar_.grammar.push_back(act_nonterms[i]);
                        grammar_.act_on_reduce.push_back(act_ids[i]);
                        grammar_.prod_prec.push_back(-1);
                    }
                    act_nonterms.clear();
                    act_ids.clear();
                    grammar_.grammar_idx.push_back((int)grammar_.grammar.size());
                    if (tt == '|') {
                        prec = -1;
                        grammar_.grammar.push_back(left_part_id);  // Add next production
                    } else
                        stop = true;
                } break;
                default: return errorSyntax(line_no_);
            }
        } while (!stop);
    }

    // Add start nonterminal to the set of used nonterminals
    if (grammar_.grammar.size() > 2) nonterm_used.addValue(1);

    // Check grammar
    ValueSet undef = nonterm_used - nonterm_defined;
    ValueSet unused = nonterm_defined - nonterm_used;
    if (!undef.empty()) {
        return errorUndefNonterm(std::string(grammar_.name_tbl.getName(maskNonterm | undef.getFirstValue())));
    } else if (!unused.empty()) {
        return errorUnusedProd(std::string(grammar_.name_tbl.getName(maskNonterm | unused.getFirstValue())));
    }
    return 0;
}

int Parser::lex() {
    int ret;

    while (1) {
        reset();
        while (1) {
            // Get the next character
            int ch = getChar();
            int idx = symb_to_idx_[ch];
            if (idx == -1) state_ = -1;
            // Get the next state
            while (state_ != -1) {
                int l = base_[state_] + idx;
                if (check_[l] == state_) {
                    state_ = next_[l];
                    break;
                } else
                    state_ = def_[state_];  // Continue with default state
            }
            if (state_ == -1) {
                ungetChar();
                break;
            }
            text_.push_back(ch);
            state_stack_.push_back(state_);
        }

        int found_pattern = -1;
        int looking_for_trail_begin = -1;
        // Unroll downto last accepting state
        while (state_stack_.size() > 0) {
            state_ = state_stack_.back();
            for (int act_idx = accept_idx_[state_]; act_idx < accept_idx_[state_ + 1]; act_idx++) {
                int act = accept_list_[act_idx];
                int act_type = act >> 14;
                switch (act_type) {
                    case 0: found_pattern = act & 0x3FFF; goto finalize;
                    case 1:
                        if (act == looking_for_trail_begin) goto finalize;
                        break;
                    case 2:
                        looking_for_trail_begin = act ^ 0xC000;
                        found_pattern = act & 0x3FFF;
                        break;
                }
            }
            text_.pop_back();
            state_stack_.pop_back();
            ungetChar();
        }
        if (found_pattern == -1) text_.push_back(getChar());  // Default pattern
    finalize:
        text_.push_back('\0');
        // Call pattern handler
        if (!onPatternMatched(found_pattern, ret)) break;
    }
    return ret;
}

bool Parser::onPatternMatched(int pat, int& token_type) {
    switch (pat) {
        case pat_act:  // Action identifier
            str_.clear();
            for (int i = 1; i < (int)(text_.size() - 2); i++) str_.push_back(text_[i]);
            str_.push_back(0);  // Terminate string
            lval_.str = &str_[0];
            token_type = tt_act;
            return false;  // Return integer
        case pat_int:
            lval_.i = str_to_int(&text_[0]);
            token_type = tt_int;
            return false;  // Return integer
        case pat_string_begin:
            str_.clear();
            pushStartCondition(sc_string);
            break;  // Continue
        case pat_symb_begin:
            str_.clear();
            pushStartCondition(sc_symb);
            break;                                                                              // Continue
        case pat_symb: str_.push_back(text_[0]); break;                                         // Continue
        case pat_string_cont: str_.insert(str_.end(), text_.begin(), text_.end() - 1); break;   // Continue
        case pat_string_es_a: str_.push_back('\a'); break;                                      // Continue
        case pat_string_es_b: str_.push_back('\b'); break;                                      // Continue
        case pat_string_es_f: str_.push_back('\f'); break;                                      // Continue
        case pat_string_es_n: str_.push_back('\n'); break;                                      // Continue
        case pat_string_es_r: str_.push_back('\r'); break;                                      // Continue
        case pat_string_es_t: str_.push_back('\t'); break;                                      // Continue
        case pat_string_es_v: str_.push_back('\v'); break;                                      // Continue
        case pat_string_es_bslash: str_.push_back('\\'); break;                                 // Continue
        case pat_string_es_dquot: str_.push_back('\"'); break;                                  // Continue
        case pat_string_es_hex: str_.push_back((hdig(text_[2]) << 4) | hdig(text_[3])); break;  // Continue
        case pat_string_es_oct:
            str_.push_back((dig(text_[1]) << 6) | (dig(text_[2]) << 3) | dig(text_[3]));
            break;                                           // Continue
        case pat_string_nl: token_type = -1; return false;   // Error
        case pat_string_eof: token_type = -1; return false;  // Error
        case pat_string_end:
            popStartCondition();
            str_.push_back(0);  // Terminate string
            lval_.str = &str_[0];
            token_type = tt_string;
            return false;  // Return string
        case pat_symb_end:
            popStartCondition();
            if (str_.size() == 1) {
                lval_.i = (int)str_[0] & 0xFF;
                token_type = tt_symb;
            } else
                token_type = -1;  // Error
            return false;         // Return string
        case pat_id:
            lval_.str = &text_[0];
            token_type = tt_id;
            return false;                                           // Return identifier
        case pat_token: token_type = tt_token; return false;        // Return "%token" keyword
        case pat_action: token_type = tt_action; return false;      // Return "%action" keyword
        case pat_option: token_type = tt_option; return false;      // Return "%option" keyword
        case pat_left: token_type = tt_left; return false;          // Return "%left" keyword
        case pat_right: token_type = tt_right; return false;        // Return "%right" keyword
        case pat_nonassoc: token_type = tt_nonassoc; return false;  // Return "%nonassoc" keyword
        case pat_prec: token_type = tt_prec; return false;          // Return "%prec" keyword
        case pat_sep: token_type = tt_sep; return false;            // Return separator "%%"
        case pat_comment: {
            int ch;
            do { ch = getChar(); } while ((ch != 0) && (ch != '\n'));  // Eat up comment
            line_no_++;
        } break;                                                               // Continue
        case pat_whitespace: break;                                            // Continue
        case pat_nl: line_no_++; break;                                        // Continue
        case pat_other_char: token_type = (int)text_[0] & 0xFF; return false;  // Return character
        case pat_eof: token_type = 0; return false;                            // Return end of file
        default: token_type = (int)text_[0] & 0xFF; return false;              // Return character
    }

    if ((sc_ == sc_symb) && (str_.size() > 1)) {
        token_type = -1;
        return false;  // Error
    }
    return true;
}

// Error codes:
//     0 - Success;
//     1 - Syntax error;
//     2 - Name redefinition;
//     3 - Left part of the production is not a nonterminal;
//     4 - Undefined action;
//     5 - Not all nonterminals defined;
//     6 - The grammar contains unused productions;
//     7 - Precedende is already defined for token;
//     8 - Undefined token;
//     9 - Precedence is not defined for token;
//    10 - Invalid use of predefined token;
//    11 - Invalid option;

int Parser::errorSyntax(int line_no) {
    std::ostringstream err;
    err << "****Error(" << line_no << "): syntax error.";
    err_string_ = err.str();
    return 1;
}

int Parser::errorNameRedef(int line_no, const std::string& name) {
    std::ostringstream err;
    err << "****Error(" << line_no << "): name <" << name << "> redefinition.";
    err_string_ = err.str();
    return 2;
}

int Parser::errorLeftPartIsNotNonterm(int line_no) {
    std::ostringstream err;
    err << "****Error(" << line_no << "): left part of production is not nonterminal.";
    err_string_ = err.str();
    return 3;
}

int Parser::errorUndefAction(int line_no, const std::string& name) {
    std::ostringstream err;
    err << "****Error(" << line_no << "): undefined action <" << name << ">.";
    err_string_ = err.str();
    return 4;
}

int Parser::errorUndefNonterm(const std::string& name) {
    std::ostringstream err;
    err << "****Error: undefined nonterminal <" << name << ">.";
    err_string_ = err.str();
    return 5;
}

int Parser::errorUnusedProd(const std::string& name) {
    std::ostringstream err;
    err << "****Error: unused production for nonterminal <" << name << ">.";
    err_string_ = err.str();
    return 6;
}

int Parser::errorPrecRedef(int line_no, int id) {
    std::ostringstream err;
    err << "****Error: precedence is already defined for token <" << grammar_.grammarSymbolText(id) << ">.";
    err_string_ = err.str();
    return 7;
}

int Parser::errorUndefToken(int line_no, const std::string& name) {
    std::ostringstream err;
    err << "****Error: undefined token <" << name << ">.";
    err_string_ = err.str();
    return 8;
}

int Parser::errorUndefPrec(int line_no, int id) {
    std::ostringstream err;
    err << "****Error: precedence is not defined for token <" << grammar_.grammarSymbolText(id) << ">.";
    err_string_ = err.str();
    return 9;
}

int Parser::errorInvUseOfPredefToken(int line_no, int id) {
    std::ostringstream err;
    err << "****Error: invalid use of predefined token <" << grammar_.grammarSymbolText(id) << ">.";
    err_string_ = err.str();
    return 10;
}

int Parser::errorInvOption(int line_no, const std::string& opt_id) {
    std::ostringstream err;
    err << "****Error: invalid option '" << opt_id << "'.";
    err_string_ = err.str();
    return 11;
}

int Parser::errorInvUseOfActName(int line_no, const std::string& name) {
    std::ostringstream err;
    err << "****Error: invalid use of action name '" << name << "'.";
    err_string_ = err.str();
    return 12;
}

int Parser::str_to_int(const char* str) {
    int ret = 0, sign = 0;
    if (*str == '-') {
        str++;
        sign = 1;
    } else if (*str == '+')
        str++;
    while (*str) {
        ret = 10 * ret + dig(*str);
        str++;
    }
    return (sign) ? (-ret) : ret;
}
