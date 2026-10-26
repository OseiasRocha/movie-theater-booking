#include "DB/DB.h"
#include "config/config.h"
#include <cstdlib>
#include <drogon/drogon.h>
#include <exception>
#include <memory>
#include <optional>
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <trantor/utils/Logger.h>

// Unit tests compile this file with GoogleTest's entry point instead.
#ifndef MOVIE_THEATER_BOOKING_NO_MAIN

int main(int argc, char* argv[]) {
   try {
      std::optional<std::string_view> address;
      std::optional<std::string_view> port;
      for (int i = 1; i < argc; ++i) {
         const std::string_view option = argv[i];
         if (option != "--address" && option != "--port") {
            throw std::invalid_argument("Unknown option: " + std::string{option});
         }
         auto& value = option == "--address" ? address : port;
         if (value) {
            throw std::invalid_argument("Duplicate option: " + std::string{option});
         }
         if (++i == argc || std::string_view{argv[i]}.starts_with("--")) {
            throw std::invalid_argument("Missing value for " + std::string{option});
         }
         value = argv[i];
      }
      std::shared_ptr<spdlog::logger> logger = spdlog::stdout_color_mt("Main");
      std::shared_ptr<spdlog::logger> logger_drogon = spdlog::stdout_color_mt("Drogon");
      trantor::Logger::enableSpdLog(logger_drogon);
      const auto& configs = config::Configs::getInstance(address, port);
      db::DB::getInstance();
      drogon::app()
         .setLogLevel(trantor::Logger::kInfo)
         .addListener(std::string{configs.address()}, configs.port())
         .run();
   } catch (const std::exception& e) {
      spdlog::critical("{}", e.what());
      return EXIT_FAILURE;
   } catch (...) {
      spdlog::critical("Unhandled exception");
      return EXIT_FAILURE;
   }
   return EXIT_SUCCESS;
}
#endif
