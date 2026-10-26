/**
 * @file
 * @brief Resolve environment settings, parse JSON, and validate database configuration.
 */

#include "config/config.h"
#include "DataModel/Screening.h"
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <json/reader.h>
#include <json/value.h>
#include <set>
#include <spdlog/common.h>
#include <stdexcept>
#include <string>
#include <system_error>

using namespace config;

Configs::Configs(std::optional<std::string_view> address, std::optional<std::string_view> port,
                 std::optional<std::string_view> logLevel) {
   loadEnvVariables(address, port, logLevel);
   loadDBFile();
   const auto movies = validateMovies();
   const auto theaters = validateTheaters();
   validateScreenings(movies, theaters);
   m_logger->info("Configuration loaded");
}

void Configs::loadDBFile() {
   const char* filePathEnv = std::getenv(ENV_DB_CONFIG_FILE_PATH);
   if (filePathEnv == nullptr) {
      m_logger->warn("Environment variable {} not set. Using default {}", ENV_DB_CONFIG_FILE_PATH,
                     DEFAULT_DB_CONFIG_FILE_PATH);
   }
   std::string filePath = filePathEnv != nullptr ? filePathEnv : DEFAULT_DB_CONFIG_FILE_PATH;
   std::ifstream file(filePath);
   if (!file) {
      throw std::runtime_error("Failed to open config file: " + filePath);
   }
   Json::CharReaderBuilder builder;
   builder["collectComments"] = false;
   builder["rejectDupKeys"] = true;
   builder["failIfExtra"] = true;

   std::string errors;

   if (!Json::parseFromStream(builder, file, &m_dbConfigFileRoot, &errors)) {
      throw std::runtime_error("Failed to parse " + filePath + ": " + errors);
   }

   if (file.bad()) {
      throw std::runtime_error("Can't read JSON file: " + filePath);
   }
   if (!m_dbConfigFileRoot.isObject()) {
      throw std::runtime_error("Config file malformed");
   }
   m_logger->info("Successfully read {}", filePath);
}

void Configs::loadEnvVariables(std::optional<std::string_view> address, std::optional<std::string_view> port,
                               std::optional<std::string_view> logLevel) {
   if (!address) {
      if (const char* value = std::getenv(ENV_ADDRESS)) {
         address = value;
      } else {
         m_logger->warn("Environment variable {} not set. Using default {}", ENV_ADDRESS, DEFAULT_ADDRESS);
         address = DEFAULT_ADDRESS;
      }
   }
   if (address->empty()) {
      throw std::invalid_argument("Address must not be empty");
   }
   m_address = *address;

   if (!port) {
      if (const char* value = std::getenv(ENV_PORT)) {
         port = value;
      } else {
         m_logger->warn("Environment variable {} not set. Using default {}", ENV_PORT, DEFAULT_PORT);
      }
   }
   m_port = DEFAULT_PORT;
   if (port) {
      unsigned value = 0;
      if (port->empty()) {
         throw std::invalid_argument("Port must be an integer between 1 and 65535");
      }
      const auto [end, error] = std::from_chars(port->data(), port->data() + port->size(), value);
      if (error != std::errc{} || end != port->data() + port->size() || value == 0 || value > 65535) {
         throw std::invalid_argument("Port must be an integer between 1 and 65535");
      }
      m_port = static_cast<uint16_t>(value);
   }
   if (!logLevel) {
      if (const char* value = std::getenv(ENV_LOG_LEVEL)) {
         logLevel = value;
      } else {
         logLevel = DEFAULT_LOG_LEVEL;
      }
   }
   m_logLevel = *logLevel;
   const auto level = spdlog::level::from_str(m_logLevel);
   if (level == spdlog::level::off && m_logLevel != "off") {
      throw std::invalid_argument("Invalid log level: " + m_logLevel);
   }
   m_logger->set_level(level);
}

std::set<uint32_t> Configs::validateMovies() {
   if (!m_dbConfigFileRoot["movies"].isArray()) {
      throw std::runtime_error("Movies must be an array");
   }

   std::set<uint32_t> ids;
   for (const auto& movie : m_dbConfigFileRoot["movies"]) {
      if (!movie.isObject() || !movie["id"].isUInt() || !movie["name"].isString() || movie["name"].asString().empty()) {
         throw std::runtime_error("Invalid movie record: " + movie.toStyledString());
      }
      if (!ids.insert(movie["id"].asUInt()).second) {
         throw std::runtime_error("Duplicated movie id: " + movie.toStyledString());
      }
   }

   return ids;
}

std::set<uint32_t> Configs::validateTheaters() {

   if (!m_dbConfigFileRoot["theaters"].isArray()) {
      throw std::runtime_error("theaters must be an array");
   }

   std::set<uint32_t> ids;
   for (const auto& theater : m_dbConfigFileRoot["theaters"]) {
      if (!theater.isObject() || !theater["id"].isUInt() || !theater["name"].isString() ||
          theater["name"].asString().empty()) {
         throw std::runtime_error("Invalid theater record: " + theater.toStyledString());
      }
      if (!ids.insert(theater["id"].asUInt()).second) {
         throw std::runtime_error("Duplicated theater id: " + theater.toStyledString());
      }
   }

   return ids;
}

void Configs::validateScreenings(const std::set<uint32_t>& movies, const std::set<uint32_t>& theaters) {
   if (!m_dbConfigFileRoot["screenings"].isArray()) {
      throw std::runtime_error("Screenings must be an array");
   }

   std::set<uint32_t> ids;
   for (const auto& screening : m_dbConfigFileRoot["screenings"]) {
      if (!screening.isObject() || !screening["id"].isUInt() || !screening["movieId"].isUInt() ||
          !screening["theaterId"].isUInt() || !screening["seats"].isArray()) {
         throw std::runtime_error("Invalid screening record: " + screening.toStyledString());
      }

      if (!ids.insert(screening["id"].asUInt()).second) {
         throw std::runtime_error("Duplicated screening id: " + screening.toStyledString());
      }

      if (movies.find(screening["movieId"].asUInt()) == movies.end()) {
         throw std::runtime_error("Movie id " + screening["movieId"].asString() + " not found in movies object");
      }

      if (theaters.find(screening["theaterId"].asUInt()) == theaters.end()) {
         throw std::runtime_error("Theater id " + screening["theaterId"].asString() + " not found in theaters object");
      }

      std::set<std::string> seats;
      if (screening["seats"].empty()) {
         throw std::runtime_error("Screening " + screening["id"].asString() + " must have at least one seat");
      }
      if (screening["seats"].size() > datamodel::Screening::MAX_SEATS) {
         throw std::runtime_error("Too many seats for screening: " + screening["id"].asString() + " in config file. " +
                                  std::to_string(screening["seats"].size()) +
                                  "max: " + std::to_string(datamodel::Screening::MAX_SEATS));
      }
      for (const auto& seat : screening["seats"]) {
         if (!seat.isObject() || !seat["name"].isString() || seat["name"].asString().empty()) {
            throw std::runtime_error("Invalid seat record: " + seat.toStyledString());
         }
         if (!seats.insert(seat["name"].asString()).second) {
            throw std::runtime_error("Duplicated seat " + seat.toStyledString() + " at theater " +
                                     screening["theaterId"].toStyledString());
         }
      }
   }

   m_logger->info("Configuration validated");
}
