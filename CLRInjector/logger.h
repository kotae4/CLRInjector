#pragma once

#include <source_location>
#include <format>
#include <string>
#include <string_view>
#include <iostream>
#include <fstream>

class logger
{
    // TO-DO:
    // * add log file
    // * add pipe client

private:
    std::ofstream m_LogFile;

public:
    enum class e_log_level
    {
        trace,
        info,
        warn,
        error
    };

private:
    logger()
    {
        m_LogFile = std::ofstream("clr_injector_log.txt", std::ios::trunc);
    }

    ~logger()
    {
        m_LogFile.close();
    }

public:

    static logger& getInstance()
    {
        static logger instance;
        return instance;
    }

    logger(logger const&) = delete;
    void operator=(logger const&) = delete;

    // credit: tomsa#3313 (discord) / tomsa000 (github)
    template<logger::e_log_level log_level = logger::e_log_level::info, typename ... ts>
    auto print(const std::string_view message, const std::source_location& loc, ts&&... args)
        -> void
    {
        if (message.empty())
            return;

        std::string formatted_loc_info;
        if (log_level == e_log_level::trace)
        {
            std::string file_name = loc.file_name();
            file_name.erase(0, file_name.find_last_of("\\") + 1);
            formatted_loc_info = std::format("{}({}:{}): {}(): ", file_name, loc.line(), loc.column(), loc.function_name());
        }
        else
        {
            formatted_loc_info = std::format("{}(): ", loc.function_name());
        }

        switch (log_level)
        {
            case e_log_level::trace:
            {
                std::cout << "[TRACE] " << formatted_loc_info << std::format(message, std::forward<ts>(args)...) << std::endl;
                m_LogFile << "[TRACE] " << formatted_loc_info << std::format(message, std::forward<ts>(args)...) << std::endl;
                break;
            }
            case e_log_level::info:
            {
                std::cout << "[INFO] " << formatted_loc_info << std::format(message, std::forward<ts>(args)...) << std::endl;
                m_LogFile << "[INFO] " << formatted_loc_info << std::format(message, std::forward<ts>(args)...) << std::endl;
                break;
            }
            case e_log_level::warn:
            {
                std::cout << "[WARN] " << formatted_loc_info << std::format(message, std::forward<ts>(args)...) << std::endl;
                m_LogFile << "[WARN] " << formatted_loc_info << std::format(message, std::forward<ts>(args)...) << std::endl;
                break;
            }
            case e_log_level::error:
            {
                std::cout << "[ERROR] " << formatted_loc_info << std::format(message, std::forward<ts>(args)...) << std::endl;
                m_LogFile << "[ERROR] " << formatted_loc_info << std::format(message, std::forward<ts>(args)...) << std::endl;
                break;
            }
            default:
            {
                break;
            }
        }
    }

#define LOG_TRACE(message, ...) logger::getInstance().print<logger::e_log_level::trace>(message, std::source_location::current(), __VA_ARGS__)
#define LOG_INFO(message, ...) logger::getInstance().print<logger::e_log_level::info>(message, std::source_location::current(), __VA_ARGS__)
#define LOG_WARN(message, ...) logger::getInstance().print<logger::e_log_level::warn>(message, std::source_location::current(), __VA_ARGS__)
#define LOG_ERROR(message, ...) logger::getInstance().print<logger::e_log_level::error>(message, std::source_location::current(), __VA_ARGS__)
};