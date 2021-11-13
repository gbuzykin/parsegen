#pragma once

#include <sstream>
#include <string>

namespace util {
template<typename InputIt>
InputIt from_utf8(InputIt in, InputIt in_end, uint32_t* pcode) {
    if (in >= in_end) { return in; }
    uint32_t code = static_cast<uint8_t>(*in);
    if ((code & 0xC0) == 0xC0) {
        constexpr uint32_t mask_tbl[] = {0xFF, 0x1F, 0xF, 0x7};
        constexpr const uint32_t count_tbl[] = {1, 1, 1, 1, 2, 2, 3, 0};
        uint32_t count = count_tbl[(code >> 3) & 7];  // continuation byte count
        if (in_end - in <= count) { return in; }
        code &= mask_tbl[count];
        while (count > 0) {
            code = (code << 6) | ((*++in) & 0x3F);
            --count;
        }
    }
    *pcode = code;
    return ++in;
}
}  // namespace util

struct TokenLoc {
    unsigned ln = 0;
    unsigned col_first = 0, col_last = 0;
};

class Parser;

namespace logger {

enum class MsgType { kDebug = 0, kInfo, kWarning, kError, kFatal };

template<typename LoggerTy>
class Logger {
 public:
    explicit Logger(MsgType type) : type_(type) {}
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    MsgType getType() const { return type_; }
    std::string getString() const { return ss_.str(); }

    template<typename Ty>
    LoggerTy& operator<<(const Ty& v) {
        ss_ << v;
        return static_cast<LoggerTy&>(*this);
    }

 private:
    MsgType type_ = MsgType::kDebug;
    std::stringstream ss_;
};

class LoggerSimple : public Logger<LoggerSimple> {
 public:
    explicit LoggerSimple(MsgType type) : Logger<LoggerSimple>(type), header_("parsegen") {}
    LoggerSimple(MsgType type, std::string_view hdr) : Logger<LoggerSimple>(type), header_(hdr) {}
    ~LoggerSimple() { printMessage(getString()); }
    void printMessage(std::string_view msg);

 private:
    std::string_view header_;
};

class LoggerExtended : public Logger<LoggerExtended> {
 public:
    LoggerExtended(MsgType type, const Parser& parser, const TokenLoc& l)
        : Logger<LoggerExtended>(type), parser_(parser), loc_(l) {}
    ~LoggerExtended() { printMessage(getString()); }
    void printMessage(std::string_view msg);

 private:
    const Parser& parser_;
    const TokenLoc& loc_;
};

inline LoggerSimple debug() { return LoggerSimple(MsgType::kDebug); }
inline LoggerSimple info() { return LoggerSimple(MsgType::kInfo); }
inline LoggerSimple warning() { return LoggerSimple(MsgType::kWarning); }
inline LoggerSimple error() { return LoggerSimple(MsgType::kError); }
inline LoggerSimple fatal() { return LoggerSimple(MsgType::kFatal); }

inline LoggerSimple debug(std::string_view hdr) { return LoggerSimple(MsgType::kDebug, hdr); }
inline LoggerSimple info(std::string_view hdr) { return LoggerSimple(MsgType::kInfo, hdr); }
inline LoggerSimple warning(std::string_view hdr) { return LoggerSimple(MsgType::kWarning, hdr); }
inline LoggerSimple error(std::string_view hdr) { return LoggerSimple(MsgType::kError, hdr); }
inline LoggerSimple fatal(std::string_view hdr) { return LoggerSimple(MsgType::kFatal, hdr); }

inline LoggerExtended debug(const Parser& parser, const TokenLoc& l) {
    return LoggerExtended(MsgType::kDebug, parser, l);
}
inline LoggerExtended info(const Parser& parser, const TokenLoc& l) {
    return LoggerExtended(MsgType::kInfo, parser, l);
}
inline LoggerExtended warning(const Parser& parser, const TokenLoc& l) {
    return LoggerExtended(MsgType::kWarning, parser, l);
}
inline LoggerExtended error(const Parser& parser, const TokenLoc& l) {
    return LoggerExtended(MsgType::kError, parser, l);
}
inline LoggerExtended fatal(const Parser& parser, const TokenLoc& l) {
    return LoggerExtended(MsgType::kFatal, parser, l);
}

}  // namespace logger
