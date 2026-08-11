#pragma once

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "core/connection_profile.hpp"
#include "core/logger.hpp"

// Loads and saves the connection list. Only the stable core of the yaml-cpp
// API is used, which keeps one source tree building against both the 0.7 on
// Debian/Ubuntu and the 0.9 on Arch.
class ProfileStore
{
public:
    static constexpr int CurrentVersion = 1;

    const std::vector<ConnectionProfile> &Profiles() const
    {
        return profiles;
    }

    std::vector<ConnectionProfile> &Profiles()
    {
        return profiles;
    }

    const std::string &LastError() const
    {
        return lastError;
    }

    ConnectionProfile *Find(const std::string &name)
    {
        const auto found = std::find_if(profiles.begin(), profiles.end(),
                                        [&name](const ConnectionProfile &profile)
                                        {
                                            return profile.name == name;
                                        });

        return found == profiles.end() ? nullptr : &(*found);
    }

    // A missing file is a first run, not an error.
    bool Load(const std::string &path)
    {
        profiles.clear();
        lastError.clear();

        std::ifstream probe(path);
        if (!probe.is_open())
        {
            return true;
        }
        probe.close();

        try
        {
            const YAML::Node root = YAML::LoadFile(path);

            if (!root["connections"])
            {
                return true;
            }

            const YAML::Node connections = root["connections"];
            if (!connections.IsSequence())
            {
                lastError = "'connections' must be a list";
                return false;
            }

            for (const YAML::Node &entry : connections)
            {
                ConnectionProfile profile;

                profile.name = Get(entry, "name", std::string());
                profile.gateway = Get(entry, "gateway", std::string());
                profile.protocol = Get(entry, "protocol", profile.protocol);
                profile.caFile = Get(entry, "cafile", std::string());
                profile.interfaceName = Get(entry, "interface", std::string());
                profile.vpncScript = Get(entry, "vpnc_script", profile.vpncScript);
                profile.username = Get(entry, "username", std::string());
                profile.userAgent = Get(entry, "user_agent", profile.userAgent);
                profile.reportedOs = Get(entry, "reported_os", profile.reportedOs);
                profile.useDtls = Get(entry, "dtls", profile.useDtls);
                profile.autoConnect = Get(entry, "autoconnect", profile.autoConnect);
                profile.rememberPassword =
                    Get(entry, "remember_password", profile.rememberPassword);
                profile.ignorePushedDns = Get(entry, "ignore_dns", profile.ignorePushedDns);
                profile.reconnectTimeout =
                    Get(entry, "reconnect_timeout", profile.reconnectTimeout);
                profile.reconnectInterval =
                    Get(entry, "reconnect_interval", profile.reconnectInterval);

                if (!profile.IsValid())
                {
                    LOG_ERROR("Skipping connection with no name or gateway");
                    continue;
                }

                profiles.push_back(profile);
            }
        }
        catch (const YAML::Exception &ex)
        {
            // A malformed file must not wipe the user's connections: report,
            // write nothing back.
            lastError = std::string("YAML error: ") + ex.what();
            profiles.clear();
            return false;
        }

        return true;
    }

    bool Save(const std::string &path) const
    {
        YAML::Emitter out;

        out << YAML::BeginMap;
        out << YAML::Key << "version" << YAML::Value << CurrentVersion;
        out << YAML::Key << "connections" << YAML::Value << YAML::BeginSeq;

        for (const ConnectionProfile &profile : profiles)
        {
            out << YAML::BeginMap;
            out << YAML::Key << "name" << YAML::Value << profile.name;
            out << YAML::Key << "gateway" << YAML::Value << profile.gateway;
            out << YAML::Key << "protocol" << YAML::Value << profile.protocol;

            if (!profile.caFile.empty())
            {
                out << YAML::Key << "cafile" << YAML::Value << profile.caFile;
            }
            if (!profile.interfaceName.empty())
            {
                out << YAML::Key << "interface" << YAML::Value << profile.interfaceName;
            }
            if (!profile.username.empty())
            {
                out << YAML::Key << "username" << YAML::Value << profile.username;
            }
            if (!profile.vpncScript.empty())
            {
                out << YAML::Key << "vpnc_script" << YAML::Value << profile.vpncScript;
            }

            out << YAML::Key << "user_agent" << YAML::Value << profile.userAgent;
            out << YAML::Key << "reported_os" << YAML::Value << profile.reportedOs;
            out << YAML::Key << "dtls" << YAML::Value << profile.useDtls;
            out << YAML::Key << "autoconnect" << YAML::Value << profile.autoConnect;
            out << YAML::Key << "remember_password" << YAML::Value << profile.rememberPassword;
            out << YAML::Key << "ignore_dns" << YAML::Value << profile.ignorePushedDns;
            out << YAML::EndMap;
        }

        out << YAML::EndSeq;
        out << YAML::EndMap;

        const std::string temporary = path + ".tmp";

        std::ofstream file(temporary, std::ios::trunc);
        if (!file.is_open())
        {
            return false;
        }

        file << "# clavister-oneconnect connections\n"
             << "# Managed by the application; hand edits are preserved in shape but\n"
             << "# comments are not. No credentials are stored here.\n\n";
        file << out.c_str() << "\n";
        file.close();

        return std::rename(temporary.c_str(), path.c_str()) == 0;
    }

private:
    // yaml-cpp throws on a bad cast; one malformed field must not fail the
    // whole load.
    template <typename T>
    static T Get(const YAML::Node &node, const char *key, const T &fallback)
    {
        if (!node[key])
        {
            return fallback;
        }

        try
        {
            return node[key].template as<T>();
        }
        catch (const YAML::Exception &)
        {
            LOG_ERROR(std::string("Ignoring malformed value for '") + key + "'");
            return fallback;
        }
    }

    std::vector<ConnectionProfile> profiles;
    std::string lastError;
};
