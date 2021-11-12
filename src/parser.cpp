#include "parser.h"

#include "valset.h"

namespace lex_detail {
#include "lex_analyzer.inl"
}

Parser::Parser(std::istream& input, std::string file_name, Grammar& grammar)
    : input_(input), file_name_(std::move(file_name)), sc_stack_({lex_detail::sc_initial}), grammar_(grammar) {}

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
    grammar_.name_tbl.insertName("$empty", idEmpty);
    grammar_.name_tbl.insertName("$default", idDefault);
    grammar_.name_tbl.insertName("$error", idError);
    grammar_.token_used[idEnd] = true;
    grammar_.token_used[idError] = true;

    size_t file_sz = static_cast<size_t>(input_.seekg(0, std::ios_base::end).tellg());
    lex_state_.text.resize(file_sz + 1);

    // Read the whole file
    input_.seekg(0);
    input_.read(lex_state_.text.data(), file_sz);
    lex_state_.text.back() = '\0';

    int tt = -1;
    lex_state_.unread_text = lex_state_.text.data();
    current_line_ = lex_state_.unread_text;

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
                if (tt != tt_id) return logSyntaxError(tt);
                std::string token_name = std::get<std::string>(tkn_.val);
                tt = -1;
                // Add token to the name table
                if (!grammar_.name_tbl.insertName(token_name, grammar_.token_count).second) {
                    return logError() << "name is already used.";
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
                if (tt != tt_id) return logSyntaxError(tt);
                std::string action_name = std::get<std::string>(tkn_.val);
                tt = -1;
                // Add action to the name table
                if (!grammar_.name_tbl.insertName(action_name, maskAction | grammar_.action_count).second) {
                    return logError() << "name is already used.";
                }
                grammar_.action_count++;
                if (grammar_.action_count > ValueSet::kMaxValue) throw std::runtime_error("too many actions");
            } break;
            case tt_option:  // '%option' definition
            {
                tt = lex();
                if (tt != tt_id) return logSyntaxError(tt);
                std::string opt_id = std::get<std::string>(tkn_.val);
                tt = lex();
                if (tt != tt_string) return logSyntaxError(tt);
                options_.emplace(std::move(opt_id), std::get<std::string>(tkn_.val));
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
                        std::string token_name = std::get<std::string>(tkn_.val);
                        bool success = true;
                        std::tie(id, success) = grammar_.name_tbl.insertName(token_name, grammar_.token_count);
                        if (success) {
                            grammar_.token_used.push_back(true);
                            grammar_.token_prec.push_back(-1);
                            grammar_.token_count++;
                            if (grammar_.token_count > ValueSet::kMaxValue) {
                                throw std::runtime_error("too many tokens");
                            }
                        } else if (id & maskAction) {
                            return logError() << "name is already used for actions.";
                        }
                    } else if (tt == tt_symb) {
                        id = std::get<unsigned>(tkn_.val);
                        grammar_.token_used[id] = true;
                    } else {
                        break;
                    }

                    if (grammar_.token_prec[id] != -1) { return logError() << "token precedence is already defined."; }
                    if (assoc == tt_left)
                        grammar_.token_prec[id] = maskLeftAssoc | prec;
                    else if (assoc == tt_right)
                        grammar_.token_prec[id] = maskRightAssoc | prec;
                    else
                        grammar_.token_prec[id] = prec;
                }
                prec++;
            } break;
            default: return logSyntaxError(tt);
        }
    }

    // clear grammar
    grammar_.grammar.clear();
    grammar_.grammar_idx.clear();
    grammar_.act_on_reduce.clear();
    grammar_.prod_prec.clear();
    // Load grammar
    grammar_.nonterm_count = 1;
    ValueSet nonterm_used, nonterm_defined;
    // Add augmenting production
    grammar_.grammar_idx.push_back(0);
    grammar_.grammar.push_back(maskNonterm);
    grammar_.grammar.push_back(maskNonterm | 1);
    grammar_.grammar.push_back(idEnd);
    grammar_.act_on_reduce.push_back(0);
    grammar_.prod_prec.push_back(-1);
    grammar_.name_tbl.insertName("$accept", maskNonterm);

    while (1) {
        // Read left part of the production
        tt = lex();
        if ((tt == tt_sep) || (tt == 0))
            break;
        else if (tt != tt_id)
            return logSyntaxError(tt);
        std::string left_part_name = std::get<std::string>(tkn_.val);
        // Try to find name in the name table
        auto [left_part_id, success] = grammar_.name_tbl.insertName(left_part_name,
                                                                    maskNonterm | grammar_.nonterm_count);
        if (success) {
            grammar_.nonterm_count++;
        } else if (!(left_part_id & maskNonterm)) {  // Check for nonterminal
            return logError() << "name is already used for tokens or actions.";
        }
        nonterm_defined.addValue(left_part_id & maskId);  // Add to the set of defined nonterminals

        // Eat up ':'
        if (lex() != ':') return logSyntaxError(tt);

        // Read production
        bool stop = false;
        std::vector<int> prod;
        int prec = -1;
        do {
            tt = lex();
            switch (tt) {
                case tt_prec:  // Set production precedence
                {
                    int id = -1;
                    // Get token
                    tt = lex();
                    if (tt == tt_token_id) {
                        std::string token_name = std::get<std::string>(tkn_.val);
                        if (auto found_id = grammar_.name_tbl.findName(token_name);
                            !(*found_id & (maskNonterm | maskAction))) {
                            id = *found_id;
                        } else {
                            return logError() << "undefined token.";
                        }
                    } else if (tt == tt_symb) {
                        id = std::get<unsigned>(tkn_.val);
                    } else {
                        return logSyntaxError(tt);
                    }

                    if (!grammar_.token_used[id] || (grammar_.token_prec[id] == -1)) {
                        return logError() << "token precedence is not defined.";
                    }
                    prec = grammar_.token_prec[id];
                } break;
                case tt_symb:  // Single symbol
                {
                    int id = std::get<unsigned>(tkn_.val);
                    prod.push_back(id);
                    grammar_.token_used[id] = true;
                } break;
                case tt_action_id: {  // Action
                    // Add action on reduce
                    std::string act_name = std::get<std::string>(tkn_.val);
                    if (auto found_id = grammar_.name_tbl.findName(act_name); found_id && (*found_id & maskAction)) {
                        prod.push_back(*found_id);
                    } else {
                        return logError() << "undefined action.";
                    }
                } break;
                case tt_token_id:
                case tt_error_id: {  // Token
                    // Add action on reduce
                    std::string token_name = std::get<std::string>(tkn_.val);
                    if (auto found_id = grammar_.name_tbl.findName(token_name);
                        found_id && !(*found_id & (maskNonterm | maskAction))) {
                        prod.push_back(*found_id);
                    } else {
                        return logError() << "undefined token.";
                    }
                } break;
                case tt_id: {  // Nonterminal
                    std::string name = std::get<std::string>(tkn_.val);
                    auto [id, success] = grammar_.name_tbl.insertName(name, maskNonterm | grammar_.nonterm_count);
                    if (success) {
                        grammar_.nonterm_count++;
                        if (grammar_.nonterm_count > ValueSet::kMaxValue) {
                            throw std::runtime_error("too many nonterminals");
                        }
                    } else if (!(left_part_id & maskNonterm)) {
                        return logError() << "name is already used for tokens or actions.";
                    }
                    prod.push_back(id);
                    nonterm_used.addValue(id & maskId);
                } break;
                case '|':
                case ';': {
                    // Calculate default precedence
                    if (prec == -1) {
                        for (auto it = prod.rbegin(); it != prod.rend(); ++it) {
                            if (!(*it & (maskAction | maskNonterm))) {  // Is token
                                assert(grammar_.token_used[*it]);
                                prec = grammar_.token_prec[*it];
                                break;
                            }
                        }
                    }

                    if (!prod.empty()) {
                        // Add productions for not end actions
                        for (auto it = prod.begin(); it != prod.end() - 1; ++it) {
                            if (*it & maskAction) {
                                std::string name = '@' + std::to_string(grammar_.nonterm_count);
                                int nonterm = maskNonterm | grammar_.nonterm_count++;
                                if (grammar_.nonterm_count > ValueSet::kMaxValue) {
                                    throw std::runtime_error("too many nonterminals");
                                }
                                grammar_.name_tbl.insertName(std::move(name), nonterm);
                                grammar_.grammar_idx.push_back((int)grammar_.grammar.size());
                                grammar_.grammar.push_back(nonterm);
                                grammar_.act_on_reduce.push_back(*it & maskId);
                                grammar_.prod_prec.push_back(-1);
                                *it = nonterm;
                            }
                        }
                    }

                    // Save production action
                    if (!prod.empty() && (prod.back() & maskAction)) {
                        // Action is at the end of production
                        grammar_.act_on_reduce.push_back(prod.back() & maskId);
                        prod.pop_back();
                    } else {
                        grammar_.act_on_reduce.push_back(0);
                    }
                    grammar_.prod_prec.push_back(prec);
                    grammar_.grammar_idx.push_back((int)grammar_.grammar.size());
                    grammar_.grammar.push_back(left_part_id);
                    grammar_.grammar.insert(grammar_.grammar.end(), prod.begin(), prod.end());

                    if (tt == '|') {
                        prod.clear();
                        prec = -1;
                    } else
                        stop = true;
                } break;
                default: return logSyntaxError(tt);
            }
        } while (!stop);
    }

    grammar_.grammar_idx.push_back((int)grammar_.grammar.size());

    // Add start nonterminal to the set of used nonterminals
    if (grammar_.grammar.size() > 2) nonterm_used.addValue(1);

    // Check grammar
    for (unsigned id : nonterm_defined - nonterm_used) {
        std::cerr << file_name_ << ": warning: unused nonterminal `" << grammar_.name_tbl.getName(maskNonterm | id)
                  << "`." << std::endl;
    }
    if (ValueSet undef = nonterm_used - nonterm_defined; !undef.empty()) {
        for (unsigned id : undef) {
            std::cerr << file_name_ << ": error: undefined nonterminal `" << grammar_.name_tbl.getName(maskNonterm | id)
                      << "`." << std::endl;
        }
        return -1;
    }
    return 0;
}

int Parser::lex() {
    tkn_.n_col = n_col_;

    while (true) {
        if (lex_state_.pat_length > 0 && lex_state_.text[0] == '\n') {
            current_line_ = lex_state_.unread_text;
            tkn_.n_col = n_col_ = 1;
            ++n_line_;
        }
        int pat = lex_detail::lex(lex_state_, sc_stack_.back());
        n_col_ += static_cast<unsigned>(lex_state_.pat_length);

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
            case lex_detail::pat_escape_other: escape = lex_state_.text[1]; break;
            case lex_detail::pat_escape_hex: {
                escape = hdig(lex_state_.text[2]);
                if (lex_state_.pat_length > 3) { *escape = (*escape << 4) + hdig(lex_state_.text[3]); }
            } break;
            case lex_detail::pat_escape_oct: {
                escape = dig(lex_state_.text[1]);
                if (lex_state_.pat_length > 2) { *escape = (*escape << 3) + dig(lex_state_.text[2]); }
                if (lex_state_.pat_length > 3) { *escape = (*escape << 3) + dig(lex_state_.text[3]); }
            } break;

            // ------ strings
            case lex_detail::pat_string: {
                tkn_.val = std::string();
                sc_stack_.push_back(lex_detail::sc_string);
            } break;
            case lex_detail::pat_string_seq: {
                std::get<std::string>(tkn_.val).append(lex_state_.text.data(), lex_state_.pat_length);
            } break;
            case lex_detail::pat_string_close: {
                sc_stack_.pop_back();
                return tt_string;
            } break;

            // ------ symbols
            case lex_detail::pat_symb: {
                tkn_.val = 0;
                sc_stack_.push_back(lex_detail::sc_symb);
            } break;
            case lex_detail::pat_symb_other: {
                tkn_.val = static_cast<unsigned char>(lex_state_.text[0]);
            } break;
            case lex_detail::pat_symb_close: {
                sc_stack_.pop_back();
                return tt_symb;
            } break;

            // ------ identifiers
            case lex_detail::pat_id: {
                tkn_.val = std::string(lex_state_.text.data(), lex_state_.pat_length);
                return tt_id;
            } break;
            case lex_detail::pat_error_id: {
                tkn_.val = std::string(lex_state_.text.data(), lex_state_.pat_length);
                return tt_error_id;
            } break;
            case lex_detail::pat_token_id: {  // [id]
                tkn_.val = std::string(lex_state_.text.data() + 1, lex_state_.pat_length - 2);
                return tt_token_id;
            } break;
            case lex_detail::pat_action_id: {  // {id}
                tkn_.val = std::string(lex_state_.text.data() + 1, lex_state_.pat_length - 2);
                return tt_action_id;
            } break;

            // ------ comment
            case lex_detail::pat_comment: {  // Eat up comment
                auto* p = lex_state_.unread_text;
                while (*p != '\n' && *p != '\0') { ++p; }
                lex_state_.unread_text = p;
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
            case lex_detail::pat_other: return static_cast<unsigned char>(lex_state_.text[0]);
            case lex_detail::pat_eof: return tt_eof;
            case lex_detail::pat_whitespace: tkn_.n_col = n_col_; break;
            case lex_detail::pat_nl: break;
            case lex_detail::pat_unterminated_token: return tt_unterminated_token;
            default: return -1;
        }

        // Process escape character
        if (escape) {
            if (sc_stack_.back() == lex_detail::sc_string) {
                std::get<std::string>(tkn_.val).push_back(*escape);
            } else {
                tkn_.val = static_cast<unsigned char>(*escape);
            }
        }
    }
    return tt_eof;
}

int Parser::logSyntaxError(int tt) {
    switch (tt) {
        case tt_eof: return logError() << "unexpected end of file";
        case tt_unterminated_token: return logError() << "unterminated token here";
    }
    return logError() << "unexpected token here";
}

void Parser::printError(const std::string& msg) {
    std::cerr << file_name_ << ":" << n_line_ << ":" << tkn_.n_col << ": error: " << msg << std::endl;

    const auto* line_end = current_line_;
    while (*line_end != '\n' && *line_end != '\0') { ++line_end; }
    if (line_end == current_line_) { return; }

    uint32_t code = 0;
    const unsigned tab_size = 4;
    unsigned col = 0, current_col = 0;
    std::string tab2space_line, n_line = std::to_string(n_line_);
    tab2space_line.reserve(line_end - current_line_);
    for (auto p = current_line_, p1 = p; (p1 = detail::from_utf8(p, line_end, &code)) > p; p = p1) {
        if (code == '\t') {
            auto align_up = [](unsigned v, unsigned base) { return (v + base - 1) & ~(base - 1); };
            unsigned tab_pos = align_up(col + 1, tab_size);
            while (col < tab_pos) { tab2space_line.push_back(' '), ++col; }
        } else {
            while (p < p1) { tab2space_line.push_back(*p++); }
            ++col;
        }
        if (p - current_line_ < tkn_.n_col) { current_col = col; }
    }

    std::cerr << " " << n_line << " | " << tab2space_line << std::endl;
    std::cerr << std::string(n_line.size() + 1, ' ') << " | " << std::string(current_col, ' ') << "^" << std::endl;
}
