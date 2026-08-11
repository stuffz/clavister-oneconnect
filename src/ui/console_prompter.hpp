#pragma once

#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <termios.h>
#include <unistd.h>

#include "core/auth_prompter.hpp"

// Prompts go to stderr: stdout carries the tunnel report and must stay
// pipeable.
class ConsolePrompter : public AuthPrompter
{
public:
    void ShowFormMessage(const std::string &banner, const std::string &message,
                         const std::string &error) override
    {
        if (!banner.empty())
        {
            std::cerr << banner << "\n";
        }
        if (!message.empty())
        {
            std::cerr << message << "\n";
        }
        if (!error.empty())
        {
            std::cerr << "error: " << error << "\n";
        }
    }

    std::optional<std::string> AskText(const std::string &label, bool secret,
                                       const std::string &initial) override
    {
        std::cerr << Tidy(label);

        if (!initial.empty() && !secret)
        {
            std::cerr << " [" << initial << "]";
        }

        std::cerr << ": " << std::flush;

        std::string value;

        if (secret)
        {
            const EchoGuard guard;
            if (!std::getline(std::cin, value))
            {
                std::cerr << "\n";
                return std::nullopt;
            }
            std::cerr << "\n";
        }
        else if (!std::getline(std::cin, value))
        {
            return std::nullopt;
        }

        if (value.empty() && !initial.empty())
        {
            return initial;
        }

        return value;
    }

    std::optional<std::string> AskChoice(const std::string &label,
                                         const std::vector<AuthChoice> &choices) override
    {
        if (choices.empty())
        {
            return std::nullopt;
        }

        if (choices.size() == 1)
        {
            return choices.front().name;
        }

        std::cerr << label << ":\n";
        for (size_t index = 0; index < choices.size(); ++index)
        {
            const AuthChoice &choice = choices[index];
            std::cerr << "  " << (index + 1) << ") "
                      << (choice.label.empty() ? choice.name : choice.label) << "\n";
        }

        while (true)
        {
            std::cerr << "select [1-" << choices.size() << "]: " << std::flush;

            std::string line;
            if (!std::getline(std::cin, line))
            {
                return std::nullopt;
            }

            if (const auto pick = ParseIndex(line))
            {
                if (*pick >= 1 && *pick <= choices.size())
                {
                    return choices[*pick - 1].name;
                }
            }

            std::cerr << "not a valid choice\n";
        }
    }

private:
    // openconnect's labels usually already end in ':'; strip it so appending
    // our own does not produce "Username::".
    static std::string Tidy(const std::string &label)
    {
        std::string out = label;
        while (!out.empty() && (out.back() == ':' || out.back() == ' ' || out.back() == '\t'))
        {
            out.pop_back();
        }
        return out;
    }

    static std::optional<size_t> ParseIndex(const std::string &line)
    {
        if (line.empty())
        {
            return std::nullopt;
        }

        size_t value = 0;
        for (const char digit : line)
        {
            if (digit < '0' || digit > '9')
            {
                return std::nullopt;
            }
            value = (value * 10) + static_cast<size_t>(digit - '0');
        }

        return value;
    }

    class EchoGuard
    {
    public:
        EchoGuard() : active(false), saved{}
        {
            if (tcgetattr(STDIN_FILENO, &saved) != 0)
            {
                return;
            }

            struct termios quiet = saved;
            quiet.c_lflag &= ~static_cast<tcflag_t>(ECHO);

            active = tcsetattr(STDIN_FILENO, TCSAFLUSH, &quiet) == 0;
        }

        ~EchoGuard()
        {
            if (active)
            {
                tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved);
            }
        }

        EchoGuard(const EchoGuard &) = delete;
        EchoGuard &operator=(const EchoGuard &) = delete;

    private:
        bool active;
        struct termios saved;
    };
};
