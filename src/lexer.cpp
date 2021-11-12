#include "lexer.h"

// Include tables
#include "lex_tbl.inl"

Lexer::Lexer(std::istream* in /*= 0*/) : input_(0), sc_(sc_initial), state_(sc_initial), line_no_(1) {
    switchStream(in);
}

std::istream* Lexer::switchStream(std::istream* in /*= 0*/) {
    std::istream* old_stream = input_;
    if (in)
        input_ = in;
    else
        input_ = &std::cin;
    return old_stream;
}

int Lexer::lex() {
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

bool Lexer::onPatternMatched(int pat, int& token_type) {
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

    if ((getStartCondition() == sc_symb) && (str_.size() > 1)) {
        token_type = -1;
        return false;  // Error
    }
    return true;
}

int Lexer::str_to_int(const char* str) {
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
