#include "util/format.h"

const char* util::detail::format(const char* p, const char* last, std::string& str, std::string_view arg) {
    const char* p0 = p;
    while (p != last) {
        if (*p == '{' || *p == '}') {
            str.append(p0, p - p0);
            if (++p == last) { break; }
            if (*(p - 1) == '{' && *p != '{') {
                do {
                    if (*p++ == '}') {
                        str += arg;
                        break;
                    }
                } while (p != last);
                break;
            } else {
                p0 = p;
            }
        }
        ++p;
    }
    return p;
}
