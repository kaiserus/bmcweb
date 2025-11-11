// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "bmcweb_config.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdio>
#include <format>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#if __GNUC__ <= 12
#include <fmt/format.h>
#endif

#if __GNUC__ > 12
// NOLINTBEGIN(readability-convert-member-functions-to-static, cert-dcl58-cpp)
template <>
struct std::formatter<void*>
{
    constexpr auto parse(std::format_parse_context& ctx)
    {
        return ctx.begin();
    }
    auto format(const void*& ptr, auto& ctx) const
    {
        return std::format_to(ctx.out(), "{}",
                              std::to_string(std::bit_cast<size_t>(ptr)));
    }
};
// NOLINTEND(readability-convert-member-functions-to-static, cert-dcl58-cpp)
#else
template <typename... Args>
using bmcweb_format_string = std::string_view;
#endif

namespace crow
{
enum class LogLevel
{
    Disabled = 0,
    Critical,
    Error,
    Warning,
    Info,
    Debug,
    Enabled,
};

constexpr int toSystemdLevel(LogLevel level)
{
    constexpr std::array<std::pair<LogLevel, int>, 5> mapping{
        {// EMERGENCY 0
         // ALERT 1
         {LogLevel::Critical, 2},
         {LogLevel::Error, 3},
         {LogLevel::Warning, 4},
         // NOTICE 5
         {LogLevel::Info, 6},
         // Note, debug here is actually mapped to info level, because OpenBMC
         // has a MaxLevelSyslog and MaxLevelStore of info, so DEBUG level will
         // never be stored.
         {LogLevel::Debug, 6}}};

    const auto* it = std::ranges::find_if(
        mapping, [level](const std::pair<LogLevel, int>& elem) {
            return elem.first == level;
        });

    // Unknown log level.  Just assume debug
    if (it == mapping.end())
    {
        return 6;
    }

    return it->second;
}

// Mapping of the external loglvl name to internal loglvl
constexpr std::array<std::string_view, 7> mapLogLevelFromName{
    "DISABLED", "CRITICAL", "ERROR", "WARNING", "INFO", "DEBUG", "ENABLED"};

constexpr crow::LogLevel getLogLevelFromName(std::string_view name)
{
    const auto* iter = std::ranges::find(mapLogLevelFromName, name);
    if (iter != mapLogLevelFromName.end())
    {
        return static_cast<LogLevel>(iter - mapLogLevelFromName.begin());
    }
    return crow::LogLevel::Disabled;
}

// configured bmcweb LogLevel
inline crow::LogLevel& getBmcwebCurrentLoggingLevel()
{
    static crow::LogLevel level = getLogLevelFromName(BMCWEB_LOGGING_LEVEL);
    return level;
}

struct FormatString
{
    std::string_view str;
    std::source_location loc;

    // NOLINTNEXTLINE(google-explicit-constructor)
    FormatString(const char* stringIn, const std::source_location& locIn =
                                           std::source_location::current()) :
        str(stringIn), loc(locIn)
    {}
};

template <typename T>
const void* logPtr(T p)
{
    static_assert(std::is_pointer<T>::value,
                  "Can't use logPtr without pointer");
    return std::bit_cast<const void*>(p);
}
template <typename... Args>
using bmcweb_format_string = std::string_view;
template <LogLevel level, typename... Args>
inline void vlog(bmcweb_format_string<Args...>&& format, Args&&... args,
                 const std::source_location& loc) noexcept
{
    if (getBmcwebCurrentLoggingLevel() < level)
    {
        return;
    }
    constexpr int systemdLevel = toSystemdLevel(level);
    std::string_view filename = loc.file_name();
    filename = filename.substr(filename.rfind('/'));
    if (!filename.empty())
    {
        filename.remove_prefix(1);
    }
    std::string logLocation;
    {
#if __GNUC__ > 12
    try
    {
        // TODO, multiple static analysis tools flag that this could potentially
        // throw Based on the documentation, it shouldn't throw, so long as none
        // of the formatters throw, so unclear at this point why this try/catch
        // is required, but add it to silence the static analysis tools.
        logLocation =
            std::format("<{}>[{}:{}] ", systemdLevel, filename, loc.line());
        logLocation +=
            std::format(std::move(format), std::forward<Args>(args)...);
    }
    catch (const std::format_error& /*error*/)
    {
        logLocation += "Failed to format";
        // Nothing more we can do here if logging is broken.
    }
#else
        // GCC 12 fallback: build prefix and format with {fmt}
        char prefix[256];
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        int n = std::snprintf(prefix, sizeof(prefix), "<%d>[%.*s:%u] ",
                              systemdLevel, static_cast<int>(filename.size()),
                              filename.data(), static_cast<unsigned>(loc.line()));
        if (n > 0)
        {
            logLocation.append(prefix, static_cast<size_t>(
                                         std::min(n, static_cast<int>(sizeof(prefix)) - 1)));
        }
        try
        {
            logLocation +=
                fmt::vformat(format, fmt::make_format_args(args...));
        }
        catch (const fmt::format_error&)
        {
            logLocation += "Failed to format";
        }
#endif
    }
    logLocation += '\n';
    // Intentionally ignore error return.
    fwrite(logLocation.data(), sizeof(std::string::value_type),
           logLocation.size(), stdout);
    fflush(stdout);
}
} // namespace crow

#if __GNUC__ > 12
template <typename... Args>
struct BMCWEB_LOG_CRITICAL
{
    // NOLINTNEXTLINE(google-explicit-constructor)
    BMCWEB_LOG_CRITICAL(std::format_string<Args...> format, Args&&... args,
                        const std::source_location& loc =
                            std::source_location::current()) noexcept
    {
        crow::vlog<crow::LogLevel::Critical, Args...>(
            std::move(format), std::forward<Args>(args)..., loc);
    }
};

template <typename... Args>
struct BMCWEB_LOG_ERROR
{
    // NOLINTNEXTLINE(google-explicit-constructor)
    BMCWEB_LOG_ERROR(std::format_string<Args...> format, Args&&... args,
                     const std::source_location& loc =
                         std::source_location::current()) noexcept
    {
        crow::vlog<crow::LogLevel::Error, Args...>(
            std::move(format), std::forward<Args>(args)..., loc);
    }
};

template <typename... Args>
struct BMCWEB_LOG_WARNING
{
    // NOLINTNEXTLINE(google-explicit-constructor)
    BMCWEB_LOG_WARNING(std::format_string<Args...> format, Args&&... args,
                       const std::source_location& loc =
                           std::source_location::current()) noexcept
    {
        crow::vlog<crow::LogLevel::Warning, Args...>(
            std::move(format), std::forward<Args>(args)..., loc);
    }
};

template <typename... Args>
struct BMCWEB_LOG_INFO
{
    // NOLINTNEXTLINE(google-explicit-constructor)
    BMCWEB_LOG_INFO(std::format_string<Args...> format, Args&&... args,
                    const std::source_location& loc =
                        std::source_location::current()) noexcept
    {
        crow::vlog<crow::LogLevel::Info, Args...>(
            std::move(format), std::forward<Args>(args)..., loc);
    }
};

template <typename... Args>
struct BMCWEB_LOG_DEBUG
{
    // NOLINTNEXTLINE(google-explicit-constructor)
    BMCWEB_LOG_DEBUG(std::format_string<Args...> format, Args&&... args,
                     const std::source_location& loc =
                         std::source_location::current()) noexcept
    {
        crow::vlog<crow::LogLevel::Debug, Args...>(
            std::move(format), std::forward<Args>(args)..., loc);
    }
};

template <typename... Args>
BMCWEB_LOG_CRITICAL(std::format_string<Args...>, Args&&...)
    -> BMCWEB_LOG_CRITICAL<Args...>;

template <typename... Args>
BMCWEB_LOG_ERROR(std::format_string<Args...>, Args&&...)
    -> BMCWEB_LOG_ERROR<Args...>;

template <typename... Args>
BMCWEB_LOG_WARNING(std::format_string<Args...>, Args&&...)
    -> BMCWEB_LOG_WARNING<Args...>;

template <typename... Args>
BMCWEB_LOG_INFO(std::format_string<Args...>, Args&&...)
    -> BMCWEB_LOG_INFO<Args...>;

#else
template <typename... Args>
struct BMCWEB_LOG_ERROR
{
    BMCWEB_LOG_ERROR( bmcweb_format_string<Args...> format,
                      Args&&... args,
                     const std::source_location& loc =
                         std::source_location::current()) noexcept
    {
        crow::vlog<crow::LogLevel::Error, Args...>(
            std::move(format), std::forward<Args>(args)..., loc);
    }
};

template <typename... Args>
BMCWEB_LOG_ERROR(bmcweb_format_string<Args...>, Args&&...)
    -> BMCWEB_LOG_ERROR<Args...>;


template <typename... Args>
struct BMCWEB_LOG_WARNING
{
    BMCWEB_LOG_WARNING(bmcweb_format_string<Args...> format,
                      Args&&... args,
                      const std::source_location& loc =
                         std::source_location::current()) noexcept
    {
        crow::vlog<crow::LogLevel::Warning, Args...>(
            std::move(format), std::forward<Args>(args)..., loc);
    }
};

template <typename... Args>
BMCWEB_LOG_WARNING(bmcweb_format_string<Args...>, Args&&...)
    -> BMCWEB_LOG_WARNING<Args...>;


template <typename... Args>
struct BMCWEB_LOG_INFO
{
    BMCWEB_LOG_INFO( bmcweb_format_string<Args...> format,
                    Args&&... args,
                    const std::source_location& loc =
                         std::source_location::current()) noexcept
    {
        crow::vlog<crow::LogLevel::Info, Args...>(
            std::move(format), std::forward<Args>(args)..., loc);
    }
};

template <typename... Args>
BMCWEB_LOG_INFO(bmcweb_format_string<Args...>, Args&&...)
    -> BMCWEB_LOG_INFO<Args...>;

template <typename... Args>
struct BMCWEB_LOG_DEBUG
{
    BMCWEB_LOG_DEBUG( bmcweb_format_string<Args...> format,
                     Args&&... args,
                     const std::source_location& loc =
                         std::source_location::current()) noexcept
    {
        crow::vlog<crow::LogLevel::Debug, Args...>(
            std::move(format), std::forward<Args>(args)..., loc);
    }
};

template <typename... Args>
BMCWEB_LOG_DEBUG(bmcweb_format_string<Args...>, Args&&...)
    -> BMCWEB_LOG_DEBUG<Args...>;


template <typename... Args>
struct BMCWEB_LOG_CRITICAL
{
    BMCWEB_LOG_CRITICAL(bmcweb_format_string<Args...> format,
                     Args&&... args,
                     const std::source_location& loc =
                         std::source_location::current()) noexcept
    {
        crow::vlog<crow::LogLevel::Critical, Args...>(
            std::move(format), std::forward<Args>(args)..., loc);
    }
};

template <typename... Args>
BMCWEB_LOG_CRITICAL(bmcweb_format_string<Args...>, Args&&...)
    -> BMCWEB_LOG_CRITICAL<Args...>;
#endif