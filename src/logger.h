#pragma once

#include <uxs/format.h>  // NOLINT

struct TokenLoc {
    unsigned ln = 0;
    unsigned col_first = 0, col_last = 0;
};

class Parser;

namespace logger {

enum class MsgType : unsigned { kDebug = 0, kInfo, kWarning, kError, kFatal };

template<typename LoggerTy>
class Logger {
 public:
    explicit Logger(MsgType type) : type_(type) {}

    MsgType getType() const { return type_; }
    std::string_view getMessage() const { return std::string_view(buf_.data(), buf_.size()); }
    void clear() { buf_.clear(); }

    template<typename... Args>
    LoggerTy& println(uxs::format_string<Args...> fmt, const Args&... args) {
        uxs::basic_vformat(buf_, fmt.get(), uxs::make_format_args(args...));
        return static_cast<LoggerTy&>(*this).show();
    }

 private:
    MsgType type_;
    uxs::inline_dynbuffer buf_;
};

class LoggerSimple : public Logger<LoggerSimple> {
 public:
    explicit LoggerSimple(MsgType type) : Logger<LoggerSimple>(type), header_("parsegen") {}
    LoggerSimple(MsgType type, std::string_view hdr) : Logger<LoggerSimple>(type), header_(hdr) {}

    LoggerSimple& show();

 private:
    std::string_view header_;
};

class LoggerExtended : public Logger<LoggerExtended> {
 public:
    LoggerExtended(MsgType type, const Parser& parser, const TokenLoc& l)
        : Logger<LoggerExtended>(type), parser_(parser), loc_(l) {}

    LoggerExtended& show();

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
