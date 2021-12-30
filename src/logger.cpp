#include "logger.h"

#include "parser.h"
#include "util/utf8.h"

#include <iostream>

#define TERM_COLOR(c) "\033[1;" c "m"
#define TERM_NORMAL   "\033[0m"
#ifndef WIN32
#    include <unistd.h>
static bool isTty() { return isatty(fileno(stderr)); }
#else
static bool isTty() { return false; }
#endif

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
        while (p_from != line.end() && std::isspace(static_cast<unsigned char>(*p_from))) { ++p_from; }
    } else {
        p_from += first - 1;
    }

    auto p_to = line.end();
    if (p_from != line.end()) {
        if (last == 0) {
            while (--p_to != p_from && std::isspace(static_cast<unsigned char>(*p_to))) {}
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
    uint32_t code = 0;
    for (auto p = line.begin(), p1 = p; (p1 = util::from_utf8(p, line.end(), &code)) > p; p = p1) {
        if (code == '\t') {  // Convert tab into spaces
            auto align_up = [](unsigned v, unsigned base) { return (v + base - 1) & ~(base - 1); };
            unsigned tab_pos = align_up(col + 1, tab_size);
            while (col < tab_pos) { tab2space_line.push_back(' '), ++col; }
        } else if (std::isspace(*p)) {
            tab2space_line.push_back(' '), ++col;
        } else {
            while (p < p1) { tab2space_line.push_back(*p++); }
            ++col;
        }
        if (p1 <= p_from) { mark_limits[0] = col; }
        if (p1 <= p_to) { mark_limits[1] = col; }
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
        case MsgType::kDebug: return ": " TERM_COLOR("0;33") "debug: " TERM_NORMAL;
        case MsgType::kInfo: return ": " TERM_COLOR("0;36") "info: " TERM_NORMAL;
        case MsgType::kWarning: return ": " TERM_COLOR("0;35") "warning: " TERM_NORMAL;
        case MsgType::kError: return ": " TERM_COLOR("0;31") "error: " TERM_NORMAL;
        case MsgType::kFatal: return ": " TERM_COLOR("0;31") "fatal error: " TERM_NORMAL;
    }
    return "";
}

struct ColorFilter {
    std::string_view msg;
    friend std::ostream& operator<<(std::ostream& os, const ColorFilter& cf) {
        const auto *p1 = cf.msg.data(), *pend = p1 + cf.msg.size();
        if (!isTty()) {
            for (const auto* p2 = cf.msg.data(); p2 != pend; ++p2) {
                if (*p2 == '\033') {
                    os.write(p1, p2 - p1);
                    for (++p2; p2 != pend && *p2 != 'm'; ++p2) {}
                    p1 = p2 + 1;
                }
            }
        }
        os.write(p1, pend - p1);
        return os;
    }
};

}  // namespace

void LoggerSimple::printMessage(std::string_view msg) {
    std::cerr << ColorFilter{TERM_COLOR("1;37")} << header_ << ColorFilter{typeString(getType())} << ColorFilter{msg}
              << std::endl;
}

void LoggerExtended::printMessage(std::string_view msg) {
    std::string n_line = std::to_string(loc_.ln);
    std::cerr << ColorFilter{TERM_COLOR("1;37")} << parser_.getFileName() << ':' << n_line
              << ':' + std::to_string(loc_.col_first) << ColorFilter{typeString(getType())} << ColorFilter{msg}
              << std::endl;

    std::string left_padding(n_line.size() + 1, ' ');
    auto [tab2space_line, mark] = markInputLine(parser_.getCurrentLine(), loc_.col_first, loc_.col_last);
    std::cerr << ' ' << n_line << " | " << tab2space_line << std::endl;
    std::cerr << left_padding << " | " << ColorFilter{TERM_COLOR("0;32")} << mark << ColorFilter{TERM_NORMAL}
              << std::endl;
}
