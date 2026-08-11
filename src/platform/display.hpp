#pragma once

#include <cstdlib>
#include <string>

// QApplication's constructor aborts the process when it cannot load a
// platform plugin, so anything that wants to report an error must check for a
// display first.
class SessionDisplay
{
public:
    static bool Available()
    {
        if (const char *platform = std::getenv("QT_QPA_PLATFORM"))
        {
            // An explicit request wins, including headless backends.
            if (*platform != '\0')
            {
                return true;
            }
        }

        return NonEmpty("WAYLAND_DISPLAY") || NonEmpty("DISPLAY");
    }

    static std::string Diagnosis()
    {
        std::string out = "No display available (";
        out += "WAYLAND_DISPLAY=" + Describe("WAYLAND_DISPLAY");
        out += ", DISPLAY=" + Describe("DISPLAY");
        out += ", XDG_RUNTIME_DIR=" + Describe("XDG_RUNTIME_DIR");
        out += ")";
        return out;
    }

private:
    static bool NonEmpty(const char *name)
    {
        const char *value = std::getenv(name);
        return value != nullptr && *value != '\0';
    }

    static std::string Describe(const char *name)
    {
        const char *value = std::getenv(name);
        if (value == nullptr || *value == '\0')
        {
            return "unset";
        }
        return value;
    }
};
