/**
 * @file
 * @brief Validated process configuration with command-line overrides.
 */

#pragma once
#include <cstdint>
#include <json/value.h>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace spdlog {
class logger;
}

/** @brief Startup settings and validation of JSON-backed database records. */
namespace config {

/**
 * @brief Process-lifetime configuration resolved and validated on first access.
 *
 * Address, port, and log level resolve independently: an explicit override skips
 * its matching environment variable; otherwise the environment precedes defaults.
 * CONFIG_DB_FILE_PATH selects the database JSON file, defaulting to config/db.json
 * relative to the working directory. All database records are validated before publication.
 * @note JSON accessors and string views borrow from this singleton. Concurrent
 *       reads are safe once construction is complete and no setters are used.
 */
class Configs {
 public:
   /**
    * @brief Return the singleton, applying startup overrides only on first access.
    * @param address Optional address override; empty strings are rejected.
    * @param port Optional decimal port override in the range 1 to 65535.
    * @param logLevel Optional spdlog level name; unknown or empty values are rejected.
    * @return The existing configuration; initialization is protected by C++ local-static rules.
    * @throws std::invalid_argument For invalid address, port, or log-level settings.
    * @throws std::runtime_error If the database JSON cannot be read, parsed, or validated.
    * @note Later calls reuse the instance and ignore new overrides. Call with
    *       command-line settings before any db::DB access. No-argument calls are supported.
    */
   inline static const Configs& getInstance(std::optional<std::string_view> address = std::nullopt,
                                            std::optional<std::string_view> port = std::nullopt,
                                            std::optional<std::string_view> logLevel = std::nullopt) {
      static Configs configs{address, port, logLevel};
      return configs;
   }

   Configs(const Configs&) = delete;
   Configs(Configs&&) = delete;
   Configs& operator=(const Configs&) = delete;
   Configs& operator=(Configs&&) = delete;

   /// @brief Borrow the validated movie array for this configuration's lifetime.
   inline const Json::Value& getMovies() const { return m_dbConfigFileRoot["movies"]; }
   /// @brief Borrow the validated theater array for this configuration's lifetime.
   inline const Json::Value& getTheaters() const { return m_dbConfigFileRoot["theaters"]; }
   /// @brief Borrow the validated screening array for this configuration's lifetime.
   inline const Json::Value& getScreenings() const { return m_dbConfigFileRoot["screenings"]; }
   /// @brief Borrow the validated spdlog level name for this configuration's lifetime.
   inline const std::string_view logLevel() const { return m_logLevel; }
   /// @brief Return the validated listening port.
   inline uint16_t port() const { return m_port; }
   /// @brief Borrow the address; renaming it through setAddress() invalidates the view.
   inline std::string_view address() const { return m_address; }
   /** @brief Replace the stored port without validation or synchronization.
    * @param port New port value.
    * @note getInstance() exposes const access; this mutator requires a mutable object.
    */
   inline void setPort(uint16_t port) { m_port = port; }
   /** @brief Copy a replacement address without validation or synchronization.
    * @param address New address, invalidating previously borrowed address views.
    * @note getInstance() exposes const access; this mutator requires a mutable object.
    */
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
   std::shared_ptr<spdlog::logger> m_logger;

   Json::Value m_dbConfigFileRoot;
   std::string m_logLevel;
   uint16_t m_port;
   std::string m_address;

   /**
    * @brief Resolve settings, load the JSON document, and validate all references.
    * @param address Optional address override.
    * @param port Optional decimal port override.
    * @param logLevel Optional log-level override.
    * @throws std::invalid_argument For invalid server settings.
    * @throws std::runtime_error For unreadable, malformed, or invalid database JSON.
    */
   Configs(std::optional<std::string_view> address, std::optional<std::string_view> port,
           std::optional<std::string_view> logLevel);
   /**
    * @brief Parse CONFIG_DB_FILE_PATH or the default database JSON file.
    * @throws std::runtime_error For open/read failures, invalid JSON, duplicate keys,
    *         trailing JSON values, or a non-object document root.
    */
   void loadDBFile();
   /**
    * @brief Resolve settings, validate them, and set the configuration logger's level.
    * @param address Override that skips ADDRESS, if present.
    * @param port Override that skips PORT, if present.
    * @param logLevel Override that skips LOG_LEVEL, if present.
    * @throws std::invalid_argument If a resolved server setting is invalid.
    * @note Defaults are 0.0.0.0, 8080, and info; the address is checked for emptiness,
    *       not for syntactic validity as a network address.
    */
   void loadEnvVariables(std::optional<std::string_view> address, std::optional<std::string_view> port,
                         std::optional<std::string_view> logLevel);
   /**
    * @brief Require a movie array of unique unsigned IDs and nonempty names.
    * @return Valid movie IDs for screening foreign-key checks.
    * @throws std::runtime_error If a record is malformed or an identifier is duplicated.
    * @note Display names need not be unique and whitespace-only names are not rejected.
    */
   std::set<uint32_t> validateMovies();
   /**
    * @brief Require a theater array of unique unsigned IDs and nonempty names.
    * @return Valid theater IDs for screening foreign-key checks.
    * @throws std::runtime_error If a record is malformed or an identifier is duplicated.
    * @note Display names need not be unique and whitespace-only names are not rejected.
    */
   std::set<uint32_t> validateTheaters();
   /**
    * @brief Check screening IDs, references, and per-screening seat labels.
    * @param movies Valid movie IDs.
    * @param theaters Valid theater IDs.
    * @throws std::runtime_error For malformed/duplicate IDs, missing foreign keys,
    *         empty/oversized seat arrays, or malformed/empty/duplicate seat labels.
    */
   void validateScreenings(const std::set<uint32_t>& movies, const std::set<uint32_t>& theaters);
};
} // namespace config
