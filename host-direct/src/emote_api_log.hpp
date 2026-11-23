#ifndef EMOTE_API_LOG_HPP
#define EMOTE_API_LOG_HPP

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace emote_api_log {

enum class Operation {
    FadeInTimeline,
    FadeOutTimeline,
    Pass,
    PlayTimeline,
    SetColor,
    SetCoord,
    SetGrayscale,
    SetOuterForce,
    SetOuterRot,
    SetRot,
    SetScale,
    SetTimelineBlendRatio,
    SetVariable,
    SetVariableDiff,
    Skip,
    Step,
    StopTimeline,
    Frame,
};

struct Entry {
    Operation op = Operation::Pass;
    std::string label;
    std::string key;
    std::array<float, 4> args{};
    std::uint32_t flags = 0;
};

namespace detail {

constexpr std::size_t max_token_size = 255;
constexpr std::size_t max_log_size = 1024 * 1024;
constexpr std::size_t max_entries = 16 * 1024;

enum class Token { End, Label, Key, Real, Word };

struct Command {
    std::string_view name;
    Operation op;
    std::array<Token, 5> fields;
};

inline constexpr Command commands[] = {
    {"FadeInTimeline", Operation::FadeInTimeline, {Token::Label, Token::Real, Token::Real}},
    {"FadeOutTimeline", Operation::FadeOutTimeline, {Token::Label, Token::Real, Token::Real}},
    {"Pass", Operation::Pass, {}},
    {"PlayTimeline", Operation::PlayTimeline, {Token::Label, Token::Word}},
    {"SetColor", Operation::SetColor, {Token::Word, Token::Real, Token::Real}},
    {"SetCoord", Operation::SetCoord, {Token::Real, Token::Real, Token::Real, Token::Real}},
    {"SetGrayscale", Operation::SetGrayscale, {Token::Real, Token::Real, Token::Real}},
    {"SetOuterForce", Operation::SetOuterForce, {Token::Label, Token::Real, Token::Real, Token::Real, Token::Real}},
    {"SetOuterRot", Operation::SetOuterRot, {Token::Real, Token::Real, Token::Real}},
    {"SetRot", Operation::SetRot, {Token::Real, Token::Real, Token::Real}},
    {"SetScale", Operation::SetScale, {Token::Real, Token::Real, Token::Real}},
    {"SetTimelineBlendRatio", Operation::SetTimelineBlendRatio, {Token::Label, Token::Real, Token::Real, Token::Real, Token::Word}},
    {"SetVariable", Operation::SetVariable, {Token::Label, Token::Real, Token::Real, Token::Real}},
    {"SetVariableDiff", Operation::SetVariableDiff, {Token::Key, Token::Label, Token::Real, Token::Real, Token::Real}},
    {"Skip", Operation::Skip, {}},
    {"Step", Operation::Step, {}},
    {"StopTimeline", Operation::StopTimeline, {Token::Label}},
    {"CalcLayerFrameInfo", Operation::Frame, {Token::Real}},
};

inline std::string_view take_token(std::string_view& line) noexcept {
    constexpr std::string_view space = " \t\v\f";
    const auto start = line.find_first_not_of(space);
    if (start == std::string_view::npos) {
        line = {};
        return {};
    }
    line.remove_prefix(start);
    const auto token = line.substr(0, line.find_first_of(space));
    line.remove_prefix(token.size());
    return token;
}

inline bool parse_real(std::string_view token, float& value) noexcept {
    char text[max_token_size + 1];
    token.copy(text, token.size());
    text[token.size()] = 0;
    char* end = nullptr;
    value = std::strtof(text, &end);
    return end == text + token.size() && std::isfinite(value);
}

inline bool parse_word(std::string_view token, std::uint32_t& value) noexcept {
    const bool negative = token.front() == '-';
    if (negative || token.front() == '+') token.remove_prefix(1);
    if (token.empty()) return false;
    const auto* const end = token.data() + token.size();
    std::uint32_t magnitude = 0;
    const auto parsed = std::from_chars(token.data(), end, magnitude, 10);
    if (parsed.ec != std::errc{} || parsed.ptr != end) return false;
    if (negative && magnitude > 0x80000000u) return false;
    value = negative ? std::uint32_t{0} - magnitude : magnitude;
    return true;
}

inline std::optional<Entry> parse_entry(const Command& command, std::string_view line) {
    Entry entry;
    entry.op = command.op;
    std::size_t arg = 0;
    for (const Token field : command.fields) {
        if (field == Token::End) break;
        const auto token = take_token(line);
        if (token.empty() || token.size() > max_token_size) return std::nullopt;
        switch (field) {
            case Token::Label: entry.label.assign(token); break;
            case Token::Key: entry.key.assign(token); break;
            case Token::Real:
                if (!parse_real(token, entry.args[arg++])) return std::nullopt;
                break;
            case Token::Word:
                if (!parse_word(token, entry.flags)) return std::nullopt;
                break;
            case Token::End: break;
        }
    }
    return entry;
}

inline std::optional<Entry> parse_line(std::string_view line) {
    const auto name = take_token(line);
    for (const auto& command : commands) {
        if (command.name == name) return parse_entry(command, line);
    }
    return std::nullopt;
}

}  // namespace detail

class Log {
public:
    bool recording() const noexcept { return mode_ == Mode::Record; }
    bool replaying() const noexcept { return mode_ == Mode::Replay; }
    void start_recording() noexcept { mode_ = Mode::Record; }
    void start_replay() noexcept {
        cursor_ = 0;
        mode_ = Mode::Replay;
    }
    void stop() noexcept { mode_ = Mode::Idle; }
    void clear() {
        std::vector<Entry>().swap(entries_);
        label_bytes_ = 0;
        cursor_ = 0;
        stop();
    }

    // Skip unrecognized records like the driver. Leave the previous log intact
    // when the replacement exceeds PSV memory limits.
    bool replace(std::string_view text) {
        if (text.size() > detail::max_log_size) return false;
        text = text.substr(0, text.find('\0'));
        std::vector<Entry> parsed;
        std::size_t label_bytes = 0;
        while (!text.empty()) {
            const auto end = text.find_first_of("\r\n");
            if (auto entry = detail::parse_line(text.substr(0, end))) {
                if (parsed.size() == detail::max_entries) return false;
                label_bytes += entry->label.size() + entry->key.size();
                parsed.push_back(std::move(*entry));
            }
            if (end == std::string_view::npos) break;
            text.remove_prefix(end + 1);
        }
        entries_ = std::move(parsed);
        label_bytes_ = label_bytes;
        cursor_ = 0;
        stop();
        return true;
    }

    void record(Entry entry) {
        if (!recording() || entries_.size() == detail::max_entries) return;
        if (entry.label.size() > detail::max_token_size || entry.key.size() > detail::max_token_size) return;
        const auto label_bytes = entry.label.size() + entry.key.size();
        if (label_bytes > detail::max_log_size - label_bytes_) return;
        for (const float value : entry.args) {
            if (!std::isfinite(value)) return;
        }
        entries_.push_back(std::move(entry));
        label_bytes_ += label_bytes;
    }

    // The caller dispatches operations through Frame or end of stream.
    std::optional<Entry> next() {
        if (!replaying()) return std::nullopt;
        if (cursor_ < entries_.size()) return entries_[cursor_++];
        stop();
        return std::nullopt;
    }

private:
    enum class Mode { Idle, Record, Replay };

    std::vector<Entry> entries_;
    Mode mode_ = Mode::Idle;
    std::size_t cursor_ = 0;
    std::size_t label_bytes_ = 0;
};

}  // namespace emote_api_log

#endif
