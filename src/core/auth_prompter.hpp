#pragma once

#include <optional>
#include <string>
#include <vector>

// Front-end for the authentication exchange. The gateway decides what to ask
// and when -- a RADIUS challenge arrives as a *second* form after the first
// succeeds -- so implementations must build their prompts at runtime.
struct AuthChoice
{
    std::string name;
    std::string label;
};

class AuthPrompter
{
public:
    virtual ~AuthPrompter() = default;

    virtual void ShowFormMessage(const std::string &banner, const std::string &message,
                                 const std::string &error) = 0;

    // `initial` is an editable default, not a value to submit. Returning
    // nullopt cancels the whole exchange.
    virtual std::optional<std::string> AskText(const std::string &label, bool secret,
                                               const std::string &initial) = 0;

    virtual std::optional<std::string> AskChoice(const std::string &label,
                                                 const std::vector<AuthChoice> &choices) = 0;

    // SSO needs a browser round-trip; returning false fails the session with a
    // clear message instead of hanging on a form it cannot complete.
    virtual bool SupportsSingleSignOn() const
    {
        return false;
    }
};
