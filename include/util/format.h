#pragma once

#include <string>
#include <utility>

namespace util {

template<unsigned base>
constexpr int dig(char ch) {
    return static_cast<int>(ch - '0');
}

template<>
constexpr int dig<16>(char ch) {
    if ((ch >= 'a') && (ch <= 'f')) { return static_cast<int>(ch - 'a') + 10; }
    if ((ch >= 'A') && (ch <= 'F')) { return static_cast<int>(ch - 'A') + 10; }
    return static_cast<int>(ch - '0');
}

namespace detail {

const char* format(const char* p, const char* last, std::string& str, std::string_view arg);

template<typename Ty>
auto format(const char* p, const char* last, std::string& str, Ty&& arg)
    -> decltype(std::to_string(std::forward<Ty>(arg)), p + 1) {
    return detail::format(p, last, str, std::to_string(std::forward<Ty>(arg)));
}

template<typename Ty1, typename Ty2, typename... Ts>
const char* format(const char* p, const char* last, std::string& str, Ty1&& arg1, Ty2&& arg2, Ts&&... other) {
    p = detail::format(p, last, str, std::forward<Ty1>(arg1));
    return detail::format(p, last, str, std::forward<Ty2>(arg2), std::forward<Ts>(other)...);
}

}  // namespace detail

template<typename... Ts>
std::string format(std::string_view fmt, Ts&&... args) {
    std::string str;
    str.reserve(256);
    const char* p = fmt.data();
    if constexpr (sizeof...(args) != 0) {
        p = detail::format(p, fmt.data() + fmt.size(), str, std::forward<Ts>(args)...);
    }
    str.append(p, fmt.size() - static_cast<size_t>(p - fmt.data()));
    return str;
}

}  // namespace util
