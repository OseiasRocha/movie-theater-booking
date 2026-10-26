/**
 * @file
 * @brief Parse startup arguments, register REST endpoints, and run the HTTP server.
 */

#include "DB/DB.h"
#include "DB/queries.h"
#include "api/docs.h"
#include "config/config.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <drogon/HttpAppFramework.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>
#include <exception>
#include <functional>
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
#include <system_error>
#include <thread>
#include <trantor/utils/Logger.h>
#include <utility>
#include <vector>

// Define MOVIE_THEATER_BOOKING_NO_MAIN to omit startup and endpoint definitions.
#ifndef MOVIE_THEATER_BOOKING_NO_MAIN

namespace {

struct CommandLineArguments {
   /// @brief Bind-address override for ADDRESS.
   std::optional<std::string_view> address;
   /// @brief Unparsed decimal override for PORT.
   std::optional<std::string_view> port;
   /// @brief Log-level override for LOG_LEVEL.
   std::optional<std::string_view> logLevel;
};

/**
 * @brief Parse --address, --port, and --log-level as separate option/value pairs.
 * @throws std::invalid_argument For unknown options, repeated options, or missing values.
 * @note Value validation is performed by config::Configs. The --option=value form is unsupported.
 */
CommandLineArguments parseArguments(int argc, char* argv[]) {
   CommandLineArguments arguments;
   /// @brief Bind a recognized option name to the optional value populated by parsing.
   struct Option {
      /// @brief Literal command-line option name.
      std::string_view name;
      /// @brief Reference to the corresponding field in the returned arguments.
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

/**
 * @brief Register the application's JSON HTTP handlers before starting the server.
 * @param logger Logger captured by value so each stored handler keeps it alive.
 * @pre Call once before drogon::app().run(), after configuration and DB initialization.
 *
 * @par Read endpoints
 * GET /movies returns movie IDs and names (the name field is called movie).
 * GET /theaters returns theater IDs and names. GET /theater?movieId=... filters
 * theaters by movie. GET /screenings lists lightweight summaries; supplying both movieId
 * and theaterId returns matching screening IDs. GET /available_seats?screeningId=...
 * returns the unbooked seats for that screening. Invalid ID parameters return HTTP 400.
 *
 * @par Booking endpoint
 * POST /bookings accepts an unsigned screeningId and 1 to MAX_SEATS unique,
 * nonempty string labels in seats. Malformed requests return HTTP 400, booking
 * failure returns HTTP 409, and success returns HTTP 200 with the booked labels.
 * Booking validation and mutation execute together under the database write lock.
 *
 * @note Read responses are owning snapshots. Detailed JSON schemas and examples are
 * in docs/openapi.json.
 */
void setupEndpoints(std::shared_ptr<spdlog::logger> logger) {
   drogon::app().registerHandler(
      "/bookings",
      [logger](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
         const auto respond = [&](drogon::HttpStatusCode status, const Json::Value& json) {
            auto response = drogon::HttpResponse::newHttpJsonResponse(json);
            response->setStatusCode(status);
            logger->debug("Booking response status: {}", static_cast<int>(status));
            callback(response);
         };
         const auto reject = [&](const std::string& message) {
            Json::Value json(Json::objectValue);
            json["error"] = message;
            respond(drogon::k400BadRequest, json);
         };

         const auto body = req->getJsonObject();
         if (!body || !body->isObject() || !(*body)["screeningId"].isUInt() || !(*body)["seats"].isArray() ||
             (*body)["seats"].empty() || (*body)["seats"].size() > datamodel::Screening::MAX_SEATS) {
            reject("Expected screeningId as an unsigned 32-bit integer and seats as an array of 1 to 20 seat names");
            return;
         }

         std::vector<std::string> seats;
         seats.reserve((*body)["seats"].size());
         for (const auto& seat : (*body)["seats"]) {
            if (!seat.isString() || seat.asString().empty()) {
               reject("Each seat must be a nonempty string");
               return;
            }
            auto name = seat.asString();
            if (std::find(seats.begin(), seats.end(), name) != seats.end()) {
               reject("Seat names must be unique");
               return;
            }
            seats.push_back(std::move(name));
         }

         const auto screeningId = (*body)["screeningId"].asUInt();
         if (!db::DB::getInstance().query(queries::MakeBooking{std::move(seats), screeningId})) {
            Json::Value json(Json::objectValue);
            json["error"] = "Screening not found or requested seats do not exist or are already booked";
            respond(drogon::k409Conflict, json);
            return;
         }

         Json::Value json(Json::objectValue);
         json["booked"] = true;
         json["screeningId"] = screeningId;
         json["seats"] = (*body)["seats"];
         respond(drogon::k200OK, json);
      },
      {drogon::Post}, "MakeBooking");
   drogon::app().registerHandler(
      "/movies",
      [logger](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) -> void {
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
         logger->debug("Response status: {}", static_cast<int>(resp->getStatusCode()));
         logger->trace("Response body: {}", resp->getJsonObject()->toStyledString());
         callback(resp);
      },
      {drogon::Get}, "MoviesList");
   drogon::app().registerHandler(
      "/theaters",
      [logger](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) -> void {
         queries::GetAllTheaters q{};
         Json::Value json{Json::arrayValue};
         const auto& theaters = db::DB::getInstance().query(q);
         for (const auto& theater : theaters) {
            Json::Value item{Json::objectValue};
            item["id"] = theater.id();
            item["name"] = std::move(std::string{theater.name()});
            json.append(std::move(item));
         }
         drogon::HttpResponsePtr resp = drogon::HttpResponse::newHttpJsonResponse(json);
         logger->debug("Response status: {}", static_cast<int>(resp->getStatusCode()));
         logger->trace("Response body: {}", resp->getJsonObject()->toStyledString());
         callback(resp);
      },
      {drogon::Get}, "TheatersList");
   drogon::app().registerHandler(
      "/screenings",
      [logger](const drogon::HttpRequestPtr& req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
         const auto& parameters = req->getParameters();
         const bool filtered = parameters.contains("movieId") || parameters.contains("theaterId");
         std::vector<queries::ScreeningSummary> screenings;
         if (filtered) {
            const auto& movie = req->getParameter("movieId");
            const auto& theater = req->getParameter("theaterId");
            uint32_t movieId{};
            uint32_t theaterId{};
            const auto [movieEnd, movieError] = std::from_chars(movie.data(), movie.data() + movie.size(), movieId);
            const auto [theaterEnd, theaterError] =
               std::from_chars(theater.data(), theater.data() + theater.size(), theaterId);
            if (movieError != std::errc{} || movieEnd != movie.data() + movie.size() || theaterError != std::errc{} ||
                theaterEnd != theater.data() + theater.size()) {
               Json::Value error(Json::objectValue);
               error["error"] = "Both movieId and theaterId must be valid unsigned 32-bit integers";
               auto response = drogon::HttpResponse::newHttpJsonResponse(error);
               response->setStatusCode(drogon::k400BadRequest);
               callback(response);
               return;
            }
            screenings = db::DB::getInstance().query(queries::GetScreeningsWithMovieAndTheater{movieId, theaterId});
         } else {
            screenings = db::DB::getInstance().query(queries::GetAllScreenings{});
         }
         Json::Value json(Json::arrayValue);
         for (const auto& screening : screenings) {
            Json::Value item{Json::objectValue};
            item["id"] = screening.id;
            if (filtered) {
               json.append(std::move(item));
               continue;
            }
            item["movieId"] = screening.movieId;
            item["theaterId"] = screening.theaterId;
            json.append(std::move(item));
         }
         drogon::HttpResponsePtr resp = drogon::HttpResponse::newHttpJsonResponse(json);
         logger->debug("Response status: {}", static_cast<int>(resp->getStatusCode()));
         logger->trace("Response body: {}", resp->getJsonObject()->toStyledString());
         callback(resp);
      },
      {drogon::Get}, "ScreeningsList");
   drogon::app().registerHandler(
      "/theater?movieId={movie}",
      [logger](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& movieId) -> void {
         uint32_t movieIdInt{};
         const auto [end, error] = std::from_chars(movieId.data(), movieId.data() + movieId.size(), movieIdInt);
         if (error == std::errc{} && end == movieId.data() + movieId.size()) {
            queries::GetTheatersWithMovie q{movieIdInt};
            Json::Value json{Json::arrayValue};
            const auto& theaters = db::DB::getInstance().query(q);
            for (const auto& theater : theaters) {
               Json::Value item{Json::objectValue};
               item["id"] = theater.id();
               item["name"] = std::move(std::string{theater.name()});
               json.append(std::move(item));
            }
            drogon::HttpResponsePtr resp = drogon::HttpResponse::newHttpJsonResponse(json);
            logger->debug("Response status: {}", static_cast<int>(resp->getStatusCode()));
            logger->trace("Response body: {}", resp->getJsonObject()->toStyledString());
            callback(resp);
         } else {
            Json::Value json{Json::objectValue};
            json["error"] = "movie must be a valid unsigned 32-bit integer";
            drogon::HttpResponsePtr resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::HttpStatusCode::k400BadRequest);
            logger->debug("Response status: {}", static_cast<int>(resp->getStatusCode()));
            logger->debug("movieId: {}", movieIdInt);
            callback(resp);
         }
      },
      {drogon::Get}, "TheatersWithMovieList");
   drogon::app().registerHandler(
      "/available_seats?screeningId={screeningId}",
      [logger](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& screnningId) -> void {
         uint32_t screeningIdInt{};
         const auto [end, error] =
            std::from_chars(screnningId.data(), screnningId.data() + screnningId.size(), screeningIdInt);
         if (error == std::errc{} && end == screnningId.data() + screnningId.size()) {
            queries::GetAvailableSeats q{screeningIdInt};
            Json::Value json{Json::arrayValue};
            const auto& seats = db::DB::getInstance().query(q);
            for (const auto& seat : seats) {
               Json::Value item{Json::objectValue};
               item["seat"] = std::move(std::string{seat.seat()});
               item["isBooked"] = seat.isBooked();
               json.append(std::move(item));
            }
            drogon::HttpResponsePtr resp = drogon::HttpResponse::newHttpJsonResponse(json);
            logger->debug("Response status: {}", static_cast<int>(resp->getStatusCode()));
            logger->trace("Response body: {}", resp->getJsonObject()->toStyledString());
            callback(resp);
         } else {
            Json::Value json{Json::objectValue};
            json["error"] = "screeningId must be a valid unsigned 32-bit integer";
            drogon::HttpResponsePtr resp = drogon::HttpResponse::newHttpJsonResponse(json);
            resp->setStatusCode(drogon::HttpStatusCode::k400BadRequest);
            logger->debug("Response status: {}", static_cast<int>(resp->getStatusCode()));
            logger->debug("screeningId: {} ", screeningIdInt);
            callback(resp);
         }
      },
      {drogon::Get}, "AvailableSeatsInScreening");
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
      setupEndpoints(logger);
      api::setupDocumentation();
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
