#include "aero/asset/ValueConverters.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <variant>

#include "aero/core/StringUtils.hpp"

namespace aero {

namespace {

[[nodiscard]] float CoerceUnifiedValueToFloat32(const UnifiedValue& v) noexcept {
    if (const auto* f = std::get_if<FloatValue>(&v)) {
        return std::isfinite(f->value) ? f->value : 0.0f;
    }
    if (const auto* i = std::get_if<std::int64_t>(&v)) {
        return static_cast<float>(*i);
    }
    if (const auto* ev = std::get_if<EnumValue>(&v)) {
        return static_cast<float>(ev->value);
    }
    if (const auto* b = std::get_if<bool>(&v)) {
        return *b ? 1.0f : 0.0f;
    }
    if (const auto* sv = std::get_if<StringValue>(&v)) {
        std::string_view s = str::Trim(sv->view());
        if (str::EqualsIgnoreCase(s, "true")) return 1.0f;
        if (str::EqualsIgnoreCase(s, "false")) return 0.0f;
        if (!s.empty() && (s.back() == 'f' || s.back() == 'F')) {
            s.remove_suffix(1u);
        }
        return str::ParseFloat32Or(s, 0.0f);
    }
    return 0.0f;
}

struct MathExprParser {
    std::string_view expr{};
    std::size_t pos = 0u;
    const UnifiedValue* args = nullptr;
    std::size_t argCount = 0u;
    float paramVal = 0.0f;
    bool ok = true;

    void SkipWs() noexcept {
        while (pos < expr.size() && str::detail::IsAsciiSpace(expr[pos])) {
            ++pos;
        }
    }

    float ReadArg(std::size_t idx) const noexcept {
        if (args && idx < argCount) {
            return CoerceUnifiedValueToFloat32(args[idx]);
        }
        return 0.0f;
    }

    float ParseTernary(int depth) noexcept {
        if (!ok || depth > 32) {
            ok = false;
            return 0.0f;
        }
        const float cond = ParseLogicalOr(depth + 1);
        SkipWs();
        if (pos < expr.size() && expr[pos] == '?') {
            ++pos;
            const float trueBranch = ParseTernary(depth + 1);
            SkipWs();
            if (pos >= expr.size() || expr[pos] != ':') {
                ok = false;
                return 0.0f;
            }
            ++pos;
            const float falseBranch = ParseTernary(depth + 1);
            return (std::fabs(cond) > 1e-6f) ? trueBranch : falseBranch;
        }
        return cond;
    }

    float ParseLogicalOr(int depth) noexcept {
        float lhs = ParseLogicalAnd(depth + 1);
        while (ok) {
            SkipWs();
            if (pos + 1u < expr.size() && expr[pos] == '|' && expr[pos + 1u] == '|') {
                pos += 2u;
                const float rhs = ParseLogicalAnd(depth + 1);
                lhs = (std::fabs(lhs) > 1e-6f || std::fabs(rhs) > 1e-6f) ? 1.0f : 0.0f;
            } else {
                break;
            }
        }
        return lhs;
    }

    float ParseLogicalAnd(int depth) noexcept {
        float lhs = ParseEquality(depth + 1);
        while (ok) {
            SkipWs();
            if (pos + 1u < expr.size() && expr[pos] == '&' && expr[pos + 1u] == '&') {
                pos += 2u;
                const float rhs = ParseEquality(depth + 1);
                lhs = (std::fabs(lhs) > 1e-6f && std::fabs(rhs) > 1e-6f) ? 1.0f : 0.0f;
            } else {
                break;
            }
        }
        return lhs;
    }

    float ParseEquality(int depth) noexcept {
        float lhs = ParseRelational(depth + 1);
        while (ok) {
            SkipWs();
            if (pos + 1u < expr.size() && expr[pos] == '=' && expr[pos + 1u] == '=') {
                pos += 2u;
                const float rhs = ParseRelational(depth + 1);
                lhs = (std::fabs(lhs - rhs) <= 1e-5f) ? 1.0f : 0.0f;
            } else if (pos + 1u < expr.size() && expr[pos] == '!' && expr[pos + 1u] == '=') {
                pos += 2u;
                const float rhs = ParseRelational(depth + 1);
                lhs = (std::fabs(lhs - rhs) > 1e-5f) ? 1.0f : 0.0f;
            } else {
                break;
            }
        }
        return lhs;
    }

    float ParseRelational(int depth) noexcept {
        float lhs = ParseAdditive(depth + 1);
        while (ok) {
            SkipWs();
            if (pos + 1u < expr.size() && expr[pos] == '<' && expr[pos + 1u] == '=') {
                pos += 2u;
                const float rhs = ParseAdditive(depth + 1);
                lhs = (lhs <= rhs + 1e-6f) ? 1.0f : 0.0f;
            } else if (pos + 1u < expr.size() && expr[pos] == '>' && expr[pos + 1u] == '=') {
                pos += 2u;
                const float rhs = ParseAdditive(depth + 1);
                lhs = (lhs + 1e-6f >= rhs) ? 1.0f : 0.0f;
            } else if (pos < expr.size() && expr[pos] == '<') {
                ++pos;
                const float rhs = ParseAdditive(depth + 1);
                lhs = (lhs < rhs) ? 1.0f : 0.0f;
            } else if (pos < expr.size() && expr[pos] == '>') {
                ++pos;
                const float rhs = ParseAdditive(depth + 1);
                lhs = (lhs > rhs) ? 1.0f : 0.0f;
            } else {
                break;
            }
        }
        return lhs;
    }

    float ParseAdditive(int depth) noexcept {
        float lhs = ParseMultiplicative(depth + 1);
        while (ok) {
            SkipWs();
            if (pos < expr.size() && expr[pos] == '+') {
                ++pos;
                lhs += ParseMultiplicative(depth + 1);
            } else if (pos < expr.size() && expr[pos] == '-') {
                ++pos;
                lhs -= ParseMultiplicative(depth + 1);
            } else {
                break;
            }
        }
        return lhs;
    }

    float ParseMultiplicative(int depth) noexcept {
        float lhs = ParseUnary(depth + 1);
        while (ok) {
            SkipWs();
            if (pos < expr.size() && expr[pos] == '*') {
                ++pos;
                lhs *= ParseUnary(depth + 1);
            } else if (pos < expr.size() && expr[pos] == '/') {
                ++pos;
                const float rhs = ParseUnary(depth + 1);
                lhs = (std::fabs(rhs) > 1e-12f) ? (lhs / rhs) : 0.0f;
            } else if (pos < expr.size() && expr[pos] == '%') {
                ++pos;
                const float rhs = ParseUnary(depth + 1);
                lhs = (std::fabs(rhs) > 1e-12f) ? std::fmod(lhs, rhs) : 0.0f;
            } else {
                break;
            }
        }
        return lhs;
    }

    float ParseUnary(int depth) noexcept {
        if (!ok || depth > 36) {
            ok = false;
            return 0.0f;
        }
        SkipWs();
        if (pos < expr.size()) {
            if (expr[pos] == '+') {
                ++pos;
                return ParseUnary(depth + 1);
            }
            if (expr[pos] == '-') {
                ++pos;
                return -ParseUnary(depth + 1);
            }
            if (expr[pos] == '!') {
                ++pos;
                const float v = ParseUnary(depth + 1);
                return (std::fabs(v) <= 1e-6f) ? 1.0f : 0.0f;
            }
        }
        return ParsePrimary(depth + 1);
    }

    float ParsePrimary(int depth) noexcept {
        SkipWs();
        if (pos >= expr.size()) {
            ok = false;
            return 0.0f;
        }

        // 1. Parenthesized sub-expression
        if (expr[pos] == '(') {
            ++pos;
            const float val = ParseTernary(depth + 1);
            SkipWs();
            if (pos >= expr.size() || expr[pos] != ')') {
                ok = false;
                return 0.0f;
            }
            ++pos;
            return val;
        }

        // 2. Indexed placeholder {0}, {1}, ...
        if (expr[pos] == '{') {
            ++pos;
            SkipWs();
            std::size_t idx = 0u;
            bool anyDigit = false;
            while (pos < expr.size() && std::isdigit(static_cast<unsigned char>(expr[pos]))) {
                anyDigit = true;
                idx = idx * 10u + static_cast<std::size_t>(expr[pos] - '0');
                ++pos;
            }
            SkipWs();
            // Skip optional format suffix inside {0:...} if any
            if (pos < expr.size() && expr[pos] == ':') {
                while (pos < expr.size() && expr[pos] != '}') {
                    ++pos;
                }
            }
            if (!anyDigit || pos >= expr.size() || expr[pos] != '}') {
                ok = false;
                return 0.0f;
            }
            ++pos;
            return ReadArg(idx);
        }

        // 3. @VALUE / @PARAMETER variables
        if (expr[pos] == '@') {
            ++pos;
            const std::size_t idStart = pos;
            while (pos < expr.size() &&
                   (std::isalnum(static_cast<unsigned char>(expr[pos])) || expr[pos] == '_')) {
                ++pos;
            }
            const std::string_view name = expr.substr(idStart, pos - idStart);
            if (str::EqualsIgnoreCase(name, "PARAMETER") || str::EqualsIgnoreCase(name, "PARAM")) {
                return paramVal;
            }
            return ReadArg(0u);
        }

        // 4. Numeric literal (32-bit float only)
        if (std::isdigit(static_cast<unsigned char>(expr[pos])) || expr[pos] == '.') {
            const std::size_t start = pos;
            bool seenDot = false;
            bool seenDigit = false;
            while (pos < expr.size()) {
                const char c = expr[pos];
                if (std::isdigit(static_cast<unsigned char>(c))) {
                    seenDigit = true;
                    ++pos;
                } else if (c == '.' && !seenDot) {
                    seenDot = true;
                    ++pos;
                } else if ((c == 'e' || c == 'E') && seenDigit) {
                    ++pos;
                    if (pos < expr.size() && (expr[pos] == '+' || expr[pos] == '-')) {
                        ++pos;
                    }
                } else {
                    break;
                }
            }
            if (pos < expr.size() && (expr[pos] == 'f' || expr[pos] == 'F')) {
                ++pos;
            }
            if (!seenDigit) {
                ok = false;
                return 0.0f;
            }
            std::string_view numSlice = expr.substr(start, pos - start);
            if (!numSlice.empty() && (numSlice.back() == 'f' || numSlice.back() == 'F')) {
                numSlice.remove_suffix(1u);
            }
            return str::ParseFloat32Or(numSlice, 0.0f);
        }

        // 5. Identifier (function call or named constant/variable)
        if (std::isalpha(static_cast<unsigned char>(expr[pos])) || expr[pos] == '_') {
            const std::size_t idStart = pos;
            while (pos < expr.size() &&
                   (std::isalnum(static_cast<unsigned char>(expr[pos])) || expr[pos] == '_')) {
                ++pos;
            }
            const std::string_view ident = expr.substr(idStart, pos - idStart);
            SkipWs();

            if (pos < expr.size() && expr[pos] == '(') {
                ++pos;
                float fnArgs[16]{};
                std::size_t fnArgCount = 0u;
                SkipWs();
                if (pos < expr.size() && expr[pos] != ')') {
                    while (ok) {
                        const float v = ParseTernary(depth + 1);
                        if (fnArgCount < 16u) {
                            fnArgs[fnArgCount++] = v;
                        }
                        SkipWs();
                        if (pos < expr.size() && expr[pos] == ',') {
                            ++pos;
                            continue;
                        }
                        break;
                    }
                }
                if (pos >= expr.size() || expr[pos] != ')') {
                    ok = false;
                    return 0.0f;
                }
                ++pos;

                if (str::EqualsIgnoreCase(ident, "min")) {
                    if (fnArgCount == 0u) return 0.0f;
                    float m = fnArgs[0];
                    for (std::size_t i = 1u; i < fnArgCount; ++i) m = std::min(m, fnArgs[i]);
                    return m;
                }
                if (str::EqualsIgnoreCase(ident, "max")) {
                    if (fnArgCount == 0u) return 0.0f;
                    float m = fnArgs[0];
                    for (std::size_t i = 1u; i < fnArgCount; ++i) m = std::max(m, fnArgs[i]);
                    return m;
                }
                if (str::EqualsIgnoreCase(ident, "abs")) {
                    return (fnArgCount >= 1u) ? std::fabs(fnArgs[0]) : 0.0f;
                }
                if (str::EqualsIgnoreCase(ident, "clamp")) {
                    if (fnArgCount >= 3u) {
                        const float lo = std::min(fnArgs[1], fnArgs[2]);
                        const float hi = std::max(fnArgs[1], fnArgs[2]);
                        return std::clamp(fnArgs[0], lo, hi);
                    }
                    return (fnArgCount >= 1u) ? fnArgs[0] : 0.0f;
                }
                if (str::EqualsIgnoreCase(ident, "floor")) {
                    return (fnArgCount >= 1u) ? std::floor(fnArgs[0]) : 0.0f;
                }
                if (str::EqualsIgnoreCase(ident, "ceil")) {
                    return (fnArgCount >= 1u) ? std::ceil(fnArgs[0]) : 0.0f;
                }
                if (str::EqualsIgnoreCase(ident, "round")) {
                    return (fnArgCount >= 1u) ? std::round(fnArgs[0]) : 0.0f;
                }
                if (str::EqualsIgnoreCase(ident, "trunc")) {
                    return (fnArgCount >= 1u) ? std::trunc(fnArgs[0]) : 0.0f;
                }
                if (str::EqualsIgnoreCase(ident, "pow")) {
                    if (fnArgCount >= 2u) {
                        if (fnArgs[0] < 0.0f && std::floor(fnArgs[1]) != fnArgs[1]) {
                            return 0.0f;
                        }
                        const float r = std::pow(fnArgs[0], fnArgs[1]);
                        return std::isfinite(r) ? r : 0.0f;
                    }
                    return 0.0f;
                }
                if (str::EqualsIgnoreCase(ident, "sqrt")) {
                    return (fnArgCount >= 1u && fnArgs[0] > 0.0f) ? std::sqrt(fnArgs[0]) : 0.0f;
                }
                if (str::EqualsIgnoreCase(ident, "sin")) {
                    return (fnArgCount >= 1u) ? std::sin(fnArgs[0]) : 0.0f;
                }
                if (str::EqualsIgnoreCase(ident, "cos")) {
                    return (fnArgCount >= 1u) ? std::cos(fnArgs[0]) : 0.0f;
                }
                if (str::EqualsIgnoreCase(ident, "tan")) {
                    return (fnArgCount >= 1u) ? std::tan(fnArgs[0]) : 0.0f;
                }
                if (str::EqualsIgnoreCase(ident, "lerp")) {
                    return (fnArgCount >= 3u)
                        ? (fnArgs[0] + (fnArgs[1] - fnArgs[0]) * fnArgs[2])
                        : 0.0f;
                }
                if (str::EqualsIgnoreCase(ident, "sign")) {
                    if (fnArgCount == 0u) return 0.0f;
                    return (fnArgs[0] > 0.0f) ? 1.0f : ((fnArgs[0] < 0.0f) ? -1.0f : 0.0f);
                }
                ok = false;
                return 0.0f;
            }

            if (str::EqualsIgnoreCase(ident, "VALUE") || ident == "x" || ident == "X") {
                return ReadArg(0u);
            }
            if (str::EqualsIgnoreCase(ident, "PARAMETER") || str::EqualsIgnoreCase(ident, "PARAM")) {
                return paramVal;
            }
            if (str::EqualsIgnoreCase(ident, "PI")) {
                return 3.14159265f;
            }
            if (ident == "E") {
                return 2.71828183f;
            }
            if (str::EqualsIgnoreCase(ident, "true")) {
                return 1.0f;
            }
            if (str::EqualsIgnoreCase(ident, "false")) {
                return 0.0f;
            }
        }

        ok = false;
        return 0.0f;
    }
};

void AppendUint64Padded(std::string& out, std::uint64_t val, std::size_t minDigits) {
    char buf[32]{};
    const auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), val);
    if (ec != std::errc{}) {
        out.append(std::max<std::size_t>(1u, minDigits), '0');
        return;
    }
    const std::size_t len = static_cast<std::size_t>(ptr - buf);
    if (len < minDigits) {
        out.append(minDigits - len, '0');
    }
    out.append(buf, len);
}

void AppendFixedFloat32(
    std::string& out,
    float numVal,
    std::size_t minIntDigits,
    std::size_t minFracDigits,
    std::size_t maxFracDigits) {
    if (!std::isfinite(numVal)) {
        out += "0";
        return;
    }
    const std::size_t clampedMaxFrac = std::min<std::size_t>(maxFracDigits, 6u);
    const std::size_t clampedMinFrac = std::min<std::size_t>(minFracDigits, clampedMaxFrac);

    std::uint64_t scale = 1u;
    for (std::size_t i = 0u; i < clampedMaxFrac; ++i) {
        scale *= 10u;
    }

    bool neg = (numVal < 0.0f);
    const float absVal = std::fabs(numVal);
    const float rawScaled = absVal * static_cast<float>(scale);
    if (!std::isfinite(rawScaled) || rawScaled >= 9.0e18f) {
        char expBuf[64]{};
        const int n = std::snprintf(expBuf, sizeof(expBuf), "%g", static_cast<double>(numVal));
        if (n > 0) {
            out.append(expBuf, static_cast<std::size_t>(std::min<int>(n, static_cast<int>(sizeof(expBuf) - 1))));
        } else {
            out += "0";
        }
        return;
    }
    const auto scaled = static_cast<std::uint64_t>(std::llround(rawScaled));
    if (scaled == 0u) {
        neg = false;
    }

    const std::uint64_t whole = scaled / scale;
    std::uint64_t frac = scaled % scale;

    if (neg) {
        out.push_back('-');
    }
    if (whole == 0u && minIntDigits == 0u && clampedMaxFrac == 0u) {
        out.push_back('0');
        return;
    }
    AppendUint64Padded(out, whole, std::max<std::size_t>(1u, minIntDigits));

    if (clampedMaxFrac > 0u) {
        char fracBuf[16]{};
        for (std::size_t idx = clampedMaxFrac; idx > 0u; --idx) {
            fracBuf[idx - 1u] = static_cast<char>('0' + (frac % 10u));
            frac /= 10u;
        }
        std::size_t actualFracLen = clampedMaxFrac;
        while (actualFracLen > clampedMinFrac && fracBuf[actualFracLen - 1u] == '0') {
            --actualFracLen;
        }
        if (actualFracLen > 0u) {
            out.push_back('.');
            out.append(fracBuf, actualFracLen);
        }
    }
}

std::string FormatSingleBindingArgument(
    const UnifiedValue& rawVal,
    std::string_view spec,
    const ExecutionDomainContext* domains) {
    spec = str::Trim(spec);

    // Unwrap RealScript heap String ObjectRef when available
    std::string_view strView{};
    bool hasString = false;
    if (const auto* sv = std::get_if<StringValue>(&rawVal)) {
        strView = sv->view();
        hasString = true;
    } else if (const auto* objRef = std::get_if<ObjectRef>(&rawVal)) {
        if (objRef->kind == ObjectKind::String && domains) {
            if (const auto resolvedSv = domains->stringViewAcrossDomains(*objRef)) {
                strView = *resolvedSv;
                hasString = true;
            }
        }
    }

    if (std::holds_alternative<std::monostate>(rawVal) ||
        std::holds_alternative<NullObject>(rawVal)) {
        return {};
    }

    if (spec.empty()) {
        if (hasString) {
            return std::string(strView);
        }
        if (const auto* b = std::get_if<bool>(&rawVal)) {
            return *b ? "true" : "false";
        }
        if (const auto* iv = std::get_if<std::int64_t>(&rawVal)) {
            std::string s;
            if (*iv < 0) {
                s.push_back('-');
                const auto mag = static_cast<std::uint64_t>(-(*iv + 1)) + 1u;
                AppendUint64Padded(s, mag, 1u);
            } else {
                AppendUint64Padded(s, static_cast<std::uint64_t>(*iv), 1u);
            }
            return s;
        }
        if (const auto* fv = std::get_if<FloatValue>(&rawVal)) {
            std::string s;
            AppendFixedFloat32(s, fv->value, 1u, 0u, 4u);
            return s;
        }
        if (const auto* ev = std::get_if<EnumValue>(&rawVal)) {
            return std::to_string(ev->value);
        }
        return rs_rt::valueToString(rawVal);
    }

    // Try extracting numeric value (including numeric strings if a numeric specifier was supplied)
    bool hasNum = false;
    float numVal = 0.0f;
    if (const auto* fv = std::get_if<FloatValue>(&rawVal)) {
        numVal = fv->value;
        hasNum = true;
    } else if (const auto* iv = std::get_if<std::int64_t>(&rawVal)) {
        numVal = static_cast<float>(*iv);
        hasNum = true;
    } else if (const auto* ev = std::get_if<EnumValue>(&rawVal)) {
        numVal = static_cast<float>(ev->value);
        hasNum = true;
    } else if (const auto* b = std::get_if<bool>(&rawVal)) {
        numVal = *b ? 1.0f : 0.0f;
        hasNum = true;
    } else if (hasString && !strView.empty()) {
        hasNum = str::TryParseFloat32(strView, numVal);
    }

    if (!hasNum) {
        return hasString ? std::string(strView) : rs_rt::valueToString(rawVal);
    }

    // 1. Standard single-letter format specifiers: F/f, N/n, P/p, D/d, C/c, G/g
    const char lead = spec.front();
    const char leadUpper = static_cast<char>(std::toupper(static_cast<unsigned char>(lead)));
    if (leadUpper == 'F' || leadUpper == 'N' || leadUpper == 'P' ||
        leadUpper == 'D' || leadUpper == 'C' || leadUpper == 'G') {
        bool digitsOk = true;
        std::size_t prec = 0u;
        for (std::size_t i = 1u; i < spec.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(spec[i]))) {
                digitsOk = false;
                break;
            }
            prec = prec * 10u + static_cast<std::size_t>(spec[i] - '0');
        }
        if (digitsOk) {
            const bool hasExplicitPrec = (spec.size() > 1u);
            std::string out;
            if (leadUpper == 'G') {
                AppendFixedFloat32(out, numVal, 1u, 0u, hasExplicitPrec ? prec : 4u);
                return out;
            }
            if (leadUpper == 'D') {
                const std::size_t pad = hasExplicitPrec ? std::min<std::size_t>(prec, 16u) : 1u;
                if (!std::holds_alternative<std::int64_t>(rawVal) &&
                    (!std::isfinite(numVal) || std::fabs(numVal) >= 9.0e18f)) {
                    char expBuf[64]{};
                    const int n = std::snprintf(expBuf, sizeof(expBuf), "%g", static_cast<double>(numVal));
                    if (n > 0) {
                        out.append(expBuf, static_cast<std::size_t>(std::min<int>(n, static_cast<int>(sizeof(expBuf) - 1))));
                    } else {
                        out += "0";
                    }
                    return out;
                }
                const auto iv = std::holds_alternative<std::int64_t>(rawVal)
                    ? std::get<std::int64_t>(rawVal)
                    : static_cast<std::int64_t>(std::llround(numVal));
                if (iv < 0) {
                    out.push_back('-');
                    const auto mag = static_cast<std::uint64_t>(-(iv + 1)) + 1u;
                    AppendUint64Padded(out, mag, pad);
                } else {
                    AppendUint64Padded(out, static_cast<std::uint64_t>(iv), pad);
                }
                return out;
            }
            if (leadUpper == 'P') {
                const std::size_t p = hasExplicitPrec ? std::min<std::size_t>(prec, 6u) : 2u;
                AppendFixedFloat32(out, numVal * 100.0f, 1u, p, p);
                out.push_back('%');
                return out;
            }
            if (leadUpper == 'C') {
                const std::size_t p = hasExplicitPrec ? std::min<std::size_t>(prec, 6u) : 2u;
                if (numVal < 0.0f) {
                    out += "-$";
                    AppendFixedFloat32(out, std::fabs(numVal), 1u, p, p);
                } else {
                    out.push_back('$');
                    AppendFixedFloat32(out, numVal, 1u, p, p);
                }
                return out;
            }
            // 'F' or 'N'
            const std::size_t p = hasExplicitPrec ? std::min<std::size_t>(prec, 6u) : 2u;
            AppendFixedFloat32(out, numVal, 1u, p, p);
            return out;
        }
    }

    // 2. Custom numeric pattern: '0', '#', ',', '.', '%' (e.g. "0", "00", "000", "0.0", "#,.##")
    bool isCustomPattern = false;
    for (char c : spec) {
        if (c == '0' || c == '#') {
            isCustomPattern = true;
            break;
        }
    }
    if (isCustomPattern) {
        bool hasPercent = false;
        for (char c : spec) {
            if (c == '%') {
                hasPercent = true;
                break;
            }
        }
        float scaledVal = hasPercent ? (numVal * 100.0f) : numVal;
        const std::size_t dotPos = spec.find('.');
        const std::string_view intPart = (dotPos == std::string_view::npos)
            ? spec
            : spec.substr(0u, dotPos);
        const std::string_view fracPart = (dotPos == std::string_view::npos)
            ? std::string_view{}
            : spec.substr(dotPos + 1u);

        // Count trailing scaling commas immediately preceding '.' (or end of intPart, e.g. "#,.##" -> /1000)
        std::size_t endInt = intPart.size();
        while (endInt > 0u && intPart[endInt - 1u] == '%') {
            --endInt;
        }
        while (endInt > 0u && intPart[endInt - 1u] == ',') {
            scaledVal *= 0.001f;
            --endInt;
        }

        std::size_t minIntZeros = 0u;
        for (std::size_t i = 0u; i < endInt; ++i) {
            if (intPart[i] == '0') {
                ++minIntZeros;
            }
        }
        std::size_t minFracZeros = 0u;
        std::size_t maxFracDigits = 0u;
        for (char c : fracPart) {
            if (c == '0') {
                ++minFracZeros;
                ++maxFracDigits;
            } else if (c == '#') {
                ++maxFracDigits;
            }
        }

        std::string out;
        AppendFixedFloat32(
            out,
            scaledVal,
            std::max<std::size_t>(1u, minIntZeros),
            minFracZeros,
            maxFracDigits);
        if (hasPercent) {
            out.push_back('%');
        }
        return out;
    }

    return hasString ? std::string(strView) : rs_rt::valueToString(rawVal);
}

} // namespace

ValueConverterKind ParseValueConverterKind(std::string_view rawConverterKey) noexcept {
    const std::string_view k = str::StripNamespacePrefix(str::Trim(rawConverterKey));
    if (k.empty()) {
        return ValueConverterKind::None;
    }
    std::string lower;
    lower.reserve(k.size());
    for (char ch : k) {
        lower.push_back(str::detail::ToLowerAscii(ch));
    }
    const bool isInv = (lower.find("inv") != std::string::npos) || (lower.find("not") != std::string::npos);
    if (lower.find("null") != std::string::npos) {
        if (lower.find("vis") != std::string::npos) {
            return ValueConverterKind::NullToCollapsed;
        }
        return ValueConverterKind::NullToBoolean;
    }
    if (lower.find("vis") != std::string::npos) {
        return isInv
            ? ValueConverterKind::InverseBooleanToVisibility
            : ValueConverterKind::BooleanToVisibility;
    }
    if (isInv && lower.find("bool") != std::string::npos) {
        return ValueConverterKind::InverseBoolean;
    }
    if (lower.find("mathconverter") != std::string::npos || lower == "math") {
        return ValueConverterKind::MathExpression;
    }
    if (lower == "none") {
        return ValueConverterKind::None;
    }
    return ValueConverterKind::CustomScriptOrHost;
}

UnifiedValue ApplyBuiltinValueConverter(
    ValueConverterKind kind,
    const UnifiedValue& rawVal,
    bool inverseForTwoWay) noexcept {
    if (kind == ValueConverterKind::None) {
        return rawVal;
    }
    auto isNullLike = [](const UnifiedValue& v) noexcept -> bool {
        if (std::holds_alternative<std::monostate>(v) || std::holds_alternative<NullObject>(v)) {
            return true;
        }
        if (const auto* ref = std::get_if<ObjectRef>(&v)) {
            return !ref->valid();
        }
        return false;
    };
    auto isTruthy = [](const UnifiedValue& v) noexcept -> bool {
        if (const auto* b = std::get_if<bool>(&v)) return *b;
        if (const auto* i = std::get_if<std::int64_t>(&v)) return *i != 0;
        if (const auto* f = std::get_if<FloatValue>(&v)) return f->value != 0.0f;
        if (const auto* sv = std::get_if<StringValue>(&v)) {
            return str::EqualsIgnoreCase(sv->view(), "true") || sv->view() == "1";
        }
        if (const auto* ref = std::get_if<ObjectRef>(&v)) {
            return ref->valid();
        }
        return false;
    };

    if (inverseForTwoWay) {
        switch (kind) {
            case ValueConverterKind::BooleanToVisibility:
                if (const auto* iv = std::get_if<std::int64_t>(&rawVal)) {
                    return UnifiedValue{*iv == 0};
                }
                return UnifiedValue{isTruthy(rawVal)};
            case ValueConverterKind::InverseBooleanToVisibility:
                if (const auto* iv = std::get_if<std::int64_t>(&rawVal)) {
                    return UnifiedValue{*iv != 0};
                }
                return UnifiedValue{!isTruthy(rawVal)};
            case ValueConverterKind::InverseBoolean:
                return UnifiedValue{!isTruthy(rawVal)};
            default:
                return rawVal;
        }
    }

    switch (kind) {
        case ValueConverterKind::BooleanToVisibility:
            return isTruthy(rawVal) ? value::FromInt(0) : value::FromInt(2);
        case ValueConverterKind::InverseBooleanToVisibility:
            return isTruthy(rawVal) ? value::FromInt(2) : value::FromInt(0);
        case ValueConverterKind::InverseBoolean:
            return UnifiedValue{!isTruthy(rawVal)};
        case ValueConverterKind::NullToCollapsed:
            return isNullLike(rawVal) ? value::FromInt(2) : value::FromInt(0);
        case ValueConverterKind::NullToBoolean:
            return UnifiedValue{!isNullLike(rawVal)};
        case ValueConverterKind::None:
        default:
            return rawVal;
    }
}

bool EvaluateMathConverterExpression(
    std::string_view expression,
    const UnifiedValue* args,
    std::size_t argCount,
    float parameterValue,
    float& outResult) noexcept {
    std::string_view s = str::Trim(expression);
    if (s.size() >= 2u && s[0] == '{' && s[1] == '}') {
        s = str::Trim(s.substr(2u));
    }
    if (s.empty()) {
        outResult = (args && argCount > 0u) ? CoerceUnifiedValueToFloat32(args[0]) : 0.0f;
        return false;
    }
    MathExprParser parser{s, 0u, args, argCount, parameterValue, true};
    const float value = parser.ParseTernary(0);
    parser.SkipWs();
    if (!parser.ok || parser.pos != s.size() || !std::isfinite(value)) {
        outResult = 0.0f;
        return false;
    }
    outResult = value;
    return true;
}

std::string FormatBindingString(
    std::string_view formatSpec,
    const UnifiedValue* args,
    std::size_t argCount,
    const ExecutionDomainContext* domains) {
    std::string_view fmt = formatSpec;
    const std::string_view trimmed = str::Trim(fmt);
    if (trimmed.size() >= 2u && trimmed.front() == '\'' && trimmed.back() == '\'') {
        fmt = trimmed.substr(1u, trimmed.size() - 2u);
    }
    if (fmt.size() >= 2u && fmt[0] == '{' && fmt[1] == '}') {
        fmt.remove_prefix(2u);
    }
    if (fmt.empty()) {
        if (args && argCount > 0u) {
            return FormatSingleBindingArgument(args[0], {}, domains);
        }
        return {};
    }

    // Standalone format specifier without '{' (e.g. StringFormat=F1, StringFormat=000, StringFormat=P0)
    if (fmt.find('{') == std::string_view::npos && fmt.find('}') == std::string_view::npos) {
        if (args && argCount > 0u) {
            return FormatSingleBindingArgument(args[0], fmt, domains);
        }
        return std::string(fmt);
    }

    std::string out;
    out.reserve(fmt.size() + 16u);
    std::size_t i = 0u;
    while (i < fmt.size()) {
        if (fmt[i] == '{') {
            if (i + 1u < fmt.size() && fmt[i + 1u] == '{') {
                out.push_back('{');
                i += 2u;
                continue;
            }
            const std::size_t closePos = fmt.find('}', i + 1u);
            if (closePos == std::string_view::npos) {
                out.push_back(fmt[i++]);
                continue;
            }
            const std::string_view token = fmt.substr(i + 1u, closePos - (i + 1u));
            std::size_t p = 0u;
            while (p < token.size() && str::detail::IsAsciiSpace(token[p])) {
                ++p;
            }
            std::size_t argIdx = 0u;
            bool hasDigits = false;
            while (p < token.size() && std::isdigit(static_cast<unsigned char>(token[p]))) {
                hasDigits = true;
                const std::size_t digit = static_cast<std::size_t>(token[p] - '0');
                argIdx = (argIdx <= 65535u) ? (argIdx * 10u + digit) : 65536u;
                ++p;
            }
            if (!hasDigits) {
                out.append(fmt.substr(i, closePos - i + 1u));
                i = closePos + 1u;
                continue;
            }
            std::size_t alignMag = 0u;
            bool alignLeft = false;
            bool hasAlign = false;
            if (p < token.size() && token[p] == ',') {
                ++p;
                bool negAlign = false;
                while (p < token.size() && str::detail::IsAsciiSpace(token[p])) ++p;
                if (p < token.size() && token[p] == '-') {
                    negAlign = true;
                    ++p;
                } else if (p < token.size() && token[p] == '+') {
                    ++p;
                }
                std::size_t w = 0u;
                while (p < token.size() && std::isdigit(static_cast<unsigned char>(token[p]))) {
                    const std::size_t digit = static_cast<std::size_t>(token[p] - '0');
                    w = (w * 10u + digit > 512u) ? 512u : (w * 10u + digit);
                    hasAlign = true;
                    ++p;
                }
                alignMag = w;
                alignLeft = negAlign;
            }
            std::string_view itemSpec{};
            if (p < token.size() && token[p] == ':') {
                itemSpec = token.substr(p + 1u);
            }
            std::string formattedItem;
            if (args && argIdx < argCount) {
                formattedItem = FormatSingleBindingArgument(args[argIdx], itemSpec, domains);
            }
            if (hasAlign && formattedItem.size() < alignMag) {
                const std::size_t pad = alignMag - formattedItem.size();
                if (alignLeft) {
                    out += formattedItem;
                    out.append(pad, ' ');
                } else {
                    out.append(pad, ' ');
                    out += formattedItem;
                }
            } else {
                out += formattedItem;
            }
            i = closePos + 1u;
            continue;
        }
        if (fmt[i] == '}' && i + 1u < fmt.size() && fmt[i + 1u] == '}') {
            out.push_back('}');
            i += 2u;
            continue;
        }
        out.push_back(fmt[i++]);
    }
    return out;
}

} // namespace aero
