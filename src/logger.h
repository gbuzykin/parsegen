#pragma once

#include "uxs/format.h"

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
    const std::string& getMessage() const { return msg_; }

    template<typename... Args>
    LoggerTy& format(uxs::format_string<Args...> fmt, const Args&... args) {
        msg_ = uxs::vformat(fmt.get(), uxs::make_format_args(args...));
        return static_cast<LoggerTy&>(*this);
    }

 private:
    MsgType type_;
    std::string msg_;
};

class LoggerSimple : public Logger<LoggerSimple> {
 public:
    explicit LoggerSimple(MsgType type) : Logger<LoggerSimple>(type), header_("parsegen") {}
    LoggerSimple(MsgType type, std::string_view hdr) : Logger<LoggerSimple>(type), header_(hdr) {}
    ~LoggerSimple() { printMessage(getMessage()); }
    void printMessage(std::string_view msg);

 private:
    std::string_view header_;
};

class LoggerExtended : public Logger<LoggerExtended> {
 public:
    LoggerExtended(MsgType type, const Parser& parser, const TokenLoc& l)
        : Logger<LoggerExtended>(type), parser_(parser), loc_(l) {}
    ~LoggerExtended() { printMessage(getMessage()); }
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
