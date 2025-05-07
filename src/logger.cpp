#include "logger.h"

#include "parser.h"

using namespace logger;

namespace {

std::pair<std::string, std::string> markInputLine(std::string_view line, unsigned first, unsigned last) {
    // Note: `first` - left marking boundary, starts from 1; value 0 - no boundary
    // Note: `last` - right marking inclusive boundary, starts from 1; value 0 - no boundary
    assert(first == 0 || last == 0 || first <= last);
    assert(first <= line.size() + 1 && last <= line.size() + 1);

    // If a boundary is not specified use position of the first not space character

    auto p_from = line.begin();
    if (first == 0) {
        while (p_from != line.end() && uxs::is_space(*p_from)) { ++p_from; }
    } else {
        p_from += first - 1;
    }

    auto p_to = line.end();
    if (p_from != line.end()) {
        if (last == 0) {
            while (--p_to != p_from && uxs::is_space(*p_to)) {}
        } else {
            p_to = std::max(p_from, line.begin() + last - 1);
        }
    } else if (first == 0) {
        return {"", ""};
    }

    std::string tab2space_line;
    tab2space_line.reserve(256);

    const unsigned tab_size = 4;
    unsigned col = 0, mark_limits[2] = {0, 0};
    std::uint32_t code = 0;
    for (auto p = line.begin(), next = p; uxs::from_utf8(p, line.end(), next, code) != 0; p = next) {
        if (code == '\t') {  // Convert tab into spaces
            auto align_up = [](unsigned v, unsigned base) { return (v + base - 1) & ~(base - 1); };
            const unsigned tab_pos = align_up(col + 1, tab_size);
            while (col < tab_pos) { tab2space_line.push_back(' '), ++col; }
        } else if (uxs::is_space(code)) {
            tab2space_line.push_back(' '), ++col;
        } else {
            do { tab2space_line.push_back(*p++); } while (p != next);
            ++col;
        }
        if (next <= p_from) { mark_limits[0] = col; }
        if (next <= p_to) { mark_limits[1] = col; }
    }

    std::string mark;
    mark.reserve(256);
    mark.append(mark_limits[0], ' ');
    mark += first == 0 ? '~' : '^';
    mark.append(mark_limits[1] - mark_limits[0], '~');
    return {tab2space_line, mark};
}

std::string_view typeString(MsgType type) {
    switch (type) {
        case MsgType::kDebug: return ": \033[1;33mdebug: \033[0m";
        case MsgType::kInfo: return ": \033[0;34minfo: \033[0m";
        case MsgType::kWarning: return ": \033[0;35mwarning: \033[0m";
        case MsgType::kError: return ": \033[0;31merror: \033[0m";
        case MsgType::kFatal: return ": \033[0;31mfatal error: \033[0m";
        default: return "";
    }
}

}  // namespace

LoggerSimple& LoggerSimple::show() {
    uxs::println(uxs::stdbuf::log(), "\033[1;37m{}{}{}", header_, typeString(getType()), getMessage());
    clear();
    return *this;
}

LoggerExtended& LoggerExtended::show() {
    std::string n_line = uxs::to_string(loc_.ln);
    uxs::println(uxs::stdbuf::log(), "\033[1;37m{}:{}:{}{}{}", parser_.getFileName(), n_line, loc_.col_first,
                 typeString(getType()), getMessage());

    std::string left_padding(n_line.size(), ' ');
    auto [tab2space_line, mark] = markInputLine(parser_.getCurrentLine(), loc_.col_first, loc_.col_last);
    uxs::println(uxs::stdbuf::log(), " {} | {}", n_line, tab2space_line);
    uxs::println(uxs::stdbuf::log(), " {} | \033[0;32m{}\033[0m", left_padding, mark);
    clear();
    return *this;
}
