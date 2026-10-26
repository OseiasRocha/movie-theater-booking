#pragma once
#include <cstdint>
#include <cstdlib>
#include <json/json.h>
#include <json/reader.h>
#include <json/value.h>
#include <memory>
#include <optional>
#include <set>
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <string_view>

namespace config {

enum class LogLevel {
   Info,
   Warning,
   Debug,
   Trace,
   Critical,
   Error,
};

class Configs {
 public:
   // Overrides apply only on first initialization, before DB initialization.
   inline static const Configs& getInstance(std::optional<std::string_view> address = std::nullopt,
                                            std::optional<std::string_view> port = std::nullopt) {
      static Configs configs{address, port};
      return configs;
   }

   Configs(const Configs&) = delete;
   Configs(Configs&&) = delete;
   Configs& operator=(const Configs&) = delete;
   Configs& operator=(Configs&&) = delete;

   inline const Json::Value& getMovies() const { return m_dbConfigFileRoot["movies"]; }
   inline const Json::Value& getTheaters() const { return m_dbConfigFileRoot["theaters"]; }
   inline const Json::Value& getScreenings() const { return m_dbConfigFileRoot["screenings"]; }
   inline const std::string_view logLevel() const { return m_logLevel; }
   inline uint16_t port() const { return m_port; }
   inline std::string_view address() const { return m_address; }
   inline void setPort(uint16_t port) { m_port = port; }
   inline void setAddress(const std::string& address) { m_address = std::move(address); }

 private:
   // Environment variables names
   static constexpr char ENV_DB_CONFIG_FILE_PATH[] = "CONFIG_DB_FILE_PATH";
   static constexpr char ENV_PORT[] = "PORT";
   static constexpr char ENV_ADDRESS[] = "ADDRESS";
   static constexpr char ENV_LOG_LEVEL[] = "LOG_LEVEL";

   // Default values in case environment variables for the config files are not set
   static constexpr char DEFAULT_DB_CONFIG_FILE_PATH[] = "config/db.json";
   static constexpr char DEFAULT_ADDRESS[] = "0.0.0.0";
   static constexpr uint16_t DEFAULT_PORT = 8080;
   static constexpr char DEFAULT_LOG_LEVEL[] = "info";
   std::shared_ptr<spdlog::logger> m_logger = spdlog::stdout_color_mt("Config");

   Json::Value m_dbConfigFileRoot;
   Json::Value m_configFileRoot;
   std::string m_logLevel;
   uint16_t m_port;
   std::string m_address;

   Configs(std::optional<std::string_view> address, std::optional<std::string_view> port);
   void loadDBFile();
   void loadEnvVariables(std::optional<std::string_view> address, std::optional<std::string_view> port);
   std::set<uint32_t> validateMovies();
   std::set<uint32_t> validateTheaters();
   void validateScreenings(const std::set<uint32_t>& movies, const std::set<uint32_t>& theaters);
};
} // namespace config
