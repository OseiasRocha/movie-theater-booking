#include "DB/DB.h"
#include "DB/queries.h"
#include "config/config.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <drogon/HttpAppFramework.h>
#include <drogon/HttpRequest.h>
#include <drogon/drogon.h>
#include <exception>
#include <json/value.h>
#include <memory>
#include <optional>
#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <trantor/utils/Logger.h>

// Unit tests compile this file with GoogleTest's entry point instead.
#ifndef MOVIE_THEATER_BOOKING_NO_MAIN

namespace {
struct CommandLineArguments {
   std::optional<std::string_view> address;
   std::optional<std::string_view> port;
   std::optional<std::string_view> logLevel;
};

CommandLineArguments parseArguments(int argc, char* argv[]) {
   CommandLineArguments arguments;
   struct Option {
      std::string_view name;
      std::optional<std::string_view>& value;
   };
   const std::array<Option, 3> options{{
      {"--address", arguments.address},
      {"--port", arguments.port},
      {"--log-level", arguments.logLevel},
   }};
   for (int i = 1; i < argc; ++i) {
      const std::string_view option = argv[i];
      const auto match = std::ranges::find(options, option, &Option::name);
      if (match == options.end()) {
         throw std::invalid_argument("Unknown option: " + std::string{option});
      }
      auto& value = match->value;
      if (value.has_value()) {
         throw std::invalid_argument("Duplicate option: " + std::string{option});
      }
      if (++i == argc || std::string_view{argv[i]}.starts_with("--")) {
         throw std::invalid_argument("Missing value for " + std::string{option});
      }
      value = argv[i];
   }
   return arguments;
}
} // namespace

void setupEndpoints() {
   drogon::app().registerHandler(
      "/movies",
      [](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) -> void {
         queries::GetAllMovies q{};
         Json::Value json{Json::arrayValue};
         const auto& movies = db::DB::getInstance().query(q);
         for (const auto& movie : movies) {
            Json::Value item{Json::objectValue};
            item["id"] = movie.id();
            item["movie"] = std::move(std::string{movie.name()});
            json.append(std::move(item));
         }
         drogon::HttpResponsePtr resp = drogon::HttpResponse::newHttpJsonResponse(json);
         callback(resp);
      },
      {drogon::Get}, "MoviesList");
}

int main(int argc, char* argv[]) {
   spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
   std::shared_ptr<spdlog::logger> logger = spdlog::stdout_color_mt("Main");
   try {
      const auto arguments = parseArguments(argc, argv);

      std::shared_ptr<spdlog::logger> logger_drogon = spdlog::stdout_color_mt("Drogon");
      trantor::Logger::enableSpdLog(logger_drogon);

      const auto& configs = config::Configs::getInstance(arguments.address, arguments.port, arguments.logLevel);

      spdlog::set_level(spdlog::level::from_str(std::string{configs.logLevel()}));

      db::DB::getInstance();

      const auto threads = std::clamp(std::thread::hardware_concurrency(), 2u, 8u);
      drogon::app()
         // Let the configured spdlog level filter Drogon messages too.
         .setLogLevel(trantor::Logger::kTrace)
         .addListener(std::string{configs.address()}, configs.port())
         .setThreadNum(threads)
         .enableGzip(true)
         .reusePort();
      setupEndpoints();
      logger->info("Listening on http://{}:{} with {} workers", configs.address(), configs.port(), threads);
      drogon::app().run();
   } catch (const std::exception& e) {
      logger->critical("{}", e.what());
      return EXIT_FAILURE;
   } catch (...) {
      logger->critical("Unhandled exception");
      return EXIT_FAILURE;
   }
   return EXIT_SUCCESS;
}
#endif
