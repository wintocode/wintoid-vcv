#pragma once

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

namespace wintoid {
namespace text_entry {

// Parsers for the text Rack's parameter field hands back. They accept the
// strings the quantities display, so an unedited field round-trips.

inline const char* skip_space(const char* p)
{
    while (*p && std::isspace(static_cast<unsigned char>(*p)))
        ++p;
    return p;
}

inline bool parse_number(const char*& p, float& value)
{
    p = skip_space(p);
    char* end = nullptr;
    value = std::strtof(p, &end);
    if (end == p || !std::isfinite(value))
        return false;
    p = end;
    return true;
}

inline std::string lower_trimmed(const char* p)
{
    p = skip_space(p);
    std::string rest;
    for (; *p; ++p)
        rest += static_cast<char>(
            std::tolower(static_cast<unsigned char>(*p)));
    while (!rest.empty()
           && std::isspace(static_cast<unsigned char>(rest.back())))
        rest.pop_back();
    return rest;
}

// "440", "440 Hz", "2.5k", "2.00 kHz" (units case-insensitive) -> hertz.
inline bool parse_frequency_hz(const std::string& text, float& hz)
{
    const char* p = text.c_str();
    float value = 0.f;
    if (!parse_number(p, value))
        return false;

    const std::string unit = lower_trimmed(p);
    if (unit.empty() || unit == "hz")
        hz = value;
    else if (unit == "k" || unit == "khz")
        hz = value * 1000.f;
    else
        return false;
    return std::isfinite(hz);
}

// "3:2", "1:4", "0.25:1", "1.5" -> positive frequency ratio.
inline bool parse_ratio(const std::string& text, float& ratio)
{
    const char* p = text.c_str();
    float numerator = 0.f;
    if (!parse_number(p, numerator))
        return false;

    float denominator = 1.f;
    p = skip_space(p);
    if (*p == ':') {
        ++p;
        if (!parse_number(p, denominator))
            return false;
    }
    if (!lower_trimmed(p).empty() || numerator <= 0.f || denominator <= 0.f)
        return false;

    ratio = numerator / denominator;
    return std::isfinite(ratio) && ratio > 0.f;
}

} // namespace text_entry
} // namespace wintoid
