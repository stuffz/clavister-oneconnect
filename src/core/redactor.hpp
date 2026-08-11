#pragma once

#include <array>
#include <cctype>
#include <string>
#include <string_view>

// Scrubs credentials out of log lines. At PRG_TRACE, libopenconnect logs raw
// HTTP, credentials included, so everything from the progress callback goes
// through here first.
//
// A denylist can never be proven complete: it covers the credential shapes
// openconnect and AnyConnect gateways actually emit, but a gateway inventing a
// new field name would slip through. Read a trace log before sharing it.
class Redactor
{
public:
    static constexpr std::string_view Placeholder = "<redacted>";

    static std::string Scrub(const std::string &line)
    {
        std::string out = line;

        // "Cookie: value", "Authorization: Bearer value"
        for (const std::string_view header : SensitiveHeaders)
        {
            RedactAfterHeader(out, header);
        }

        // "password=value&otp=value"
        for (const std::string_view key : SensitiveKeys)
        {
            RedactAfterAssignment(out, key);
        }

        // "<password>value</password>"
        for (const std::string_view key : SensitiveKeys)
        {
            RedactBetweenTags(out, key);
        }

        return out;
    }

private:
    static constexpr std::array<std::string_view, 7> SensitiveHeaders = {
        "cookie",         "set-cookie",        "authorization",   "proxy-authorization",
        "x-cstp-session", "x-dtls-session-id", "x-aggregate-auth"};

    static constexpr std::array<std::string_view, 10> SensitiveKeys = {
        "password", "passwd", "secondary_password", "otp", "token", "secret", "sessionid",
        "webvpn",   "cookie", "auth_token"};

    static bool IEquals(std::string_view lhs, std::string_view rhs)
    {
        if (lhs.size() != rhs.size())
        {
            return false;
        }
        for (size_t i = 0; i < lhs.size(); ++i)
        {
            if (Lower(lhs[i]) != Lower(rhs[i]))
            {
                return false;
            }
        }
        return true;
    }

    static char Lower(char value)
    {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    }

    static void RedactAfterHeader(std::string &text, std::string_view header)
    {
        size_t search = 0;

        while (search < text.size())
        {
            const size_t colon = text.find(':', search);
            if (colon == std::string::npos)
            {
                return;
            }

            const size_t start = LineStart(text, colon);
            const std::string_view candidate(text.data() + start, colon - start);

            if (IEquals(Trim(candidate), header))
            {
                const size_t end = text.find('\n', colon);
                const size_t stop = (end == std::string::npos) ? text.size() : end;

                text.replace(colon + 1, stop - colon - 1,
                             std::string(" ") + std::string(Placeholder));
                search = colon + 1 + Placeholder.size() + 1;
                continue;
            }

            search = colon + 1;
        }
    }

    // Stops at the first separator so the rest of the line keeps its structure.
    static void RedactAfterAssignment(std::string &text, std::string_view key)
    {
        size_t search = 0;

        while (true)
        {
            const size_t found = FindKey(text, key, search);
            if (found == std::string::npos)
            {
                return;
            }

            const size_t sep = found + key.size();
            if (sep >= text.size() || text[sep] != '=')
            {
                search = found + key.size();
                continue;
            }

            size_t stop = sep + 1;
            while (stop < text.size() && text[stop] != '&' && text[stop] != '\n' &&
                   text[stop] != '\r' && text[stop] != ' ' && text[stop] != '"')
            {
                ++stop;
            }

            text.replace(sep + 1, stop - sep - 1, std::string(Placeholder));
            search = sep + 1 + Placeholder.size();
        }
    }

    static void RedactBetweenTags(std::string &text, std::string_view key)
    {
        const std::string open = "<" + std::string(key) + ">";
        const std::string close = "</" + std::string(key) + ">";

        size_t search = 0;

        while (true)
        {
            const size_t start = LowerFind(text, open, search);
            if (start == std::string::npos)
            {
                return;
            }

            const size_t valueStart = start + open.size();
            const size_t end = LowerFind(text, close, valueStart);
            if (end == std::string::npos)
            {
                return;
            }

            text.replace(valueStart, end - valueStart, std::string(Placeholder));
            search = valueStart + Placeholder.size() + close.size();
        }
    }

    // Word-boundary match: "password" must not fire inside "password_hint".
    static size_t FindKey(const std::string &text, std::string_view key, size_t from)
    {
        for (size_t i = from; i + key.size() <= text.size(); ++i)
        {
            if (!IEquals(std::string_view(text.data() + i, key.size()), key))
            {
                continue;
            }

            if (i > 0)
            {
                const char before = text[i - 1];
                if (std::isalnum(static_cast<unsigned char>(before)) != 0 || before == '_')
                {
                    continue;
                }
            }

            return i;
        }

        return std::string::npos;
    }

    static size_t LowerFind(const std::string &text, const std::string &needle, size_t from)
    {
        for (size_t i = from; i + needle.size() <= text.size(); ++i)
        {
            if (IEquals(std::string_view(text.data() + i, needle.size()), needle))
            {
                return i;
            }
        }
        return std::string::npos;
    }

    static size_t LineStart(const std::string &text, size_t position)
    {
        const size_t newline = text.rfind('\n', position);
        return newline == std::string::npos ? 0 : newline + 1;
    }

    static std::string_view Trim(std::string_view value)
    {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
        {
            value.remove_prefix(1);
        }
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
        {
            value.remove_suffix(1);
        }
        return value;
    }
};
