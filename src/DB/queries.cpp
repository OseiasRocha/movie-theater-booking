/**
 * @file
 * @brief Implement read snapshots, screening filters, and booking commands.
 */

#include "DB/queries.h"
#include "DB/DB.h"
#include "DataModel/Movie.h"
#include "DataModel/Screening.h"
#include "DataModel/Seat.h"
#include "DataModel/Theater.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <spdlog/fmt/ranges.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <unordered_set>
#include <vector>

using namespace db;
using namespace queries;

/// @brief Registered logger shared by all query implementations.
static std::shared_ptr<spdlog::logger> logger = spdlog::stdout_color_mt("Queries");

std::vector<datamodel::Movie> GetAllMovies::execute(const Storage& storage) const {
   std::vector<datamodel::Movie> result;
   result.reserve(storage.movies().size());

   for (const auto& [id, movie] : storage.movies()) {
      result.push_back(movie);
   }

   return result;
}

std::vector<datamodel::Theater> GetTheatersWithMovie::execute(const Storage& storage) const {
   std::vector<datamodel::Theater> result;
   std::unordered_set<uint32_t> theaterIds;

   auto movie = storage.movies().find(m_movieId);
   if (movie == storage.movies().end()) {
      logger->error("Movie with id: {} not found", m_movieId);
      return result;
   }

   for (const auto& [id, screening] : storage.screenings()) {
      if (screening.movieId() == m_movieId) {
         theaterIds.insert(screening.theaterId());
      }
   }

   for (const auto& id : theaterIds) {
      if (auto it = storage.theaters().find(id); it != storage.theaters().end()) {
         result.push_back(it->second);
      }
   }

   auto theaters = result | std::views::transform([](const auto& theater) { return theater.name(); });
   logger->trace("Theaters with movie {}: {}", movie->second.name(), fmt::join(theaters, ", "));

   return result;
}

std::optional<datamodel::Movie> GetMovieByName::execute(const Storage& storage) const {
   auto it = std::find_if(storage.movies().begin(), storage.movies().end(),
                          [&](const auto& movie) { return movie.second.name() == m_name; });
   if (it != storage.movies().end()) {
      logger->trace("Found movie {} id: {}", m_name, it->first);
      return it->second;
   }
   logger->error("Movie {} not found", m_name);
   return {};
}

std::vector<datamodel::Seat> GetAvailableSeats::execute(const db::Storage& storage) const {
   const auto& screening = storage.screenings().find(m_screeningId);

   if (screening == storage.screenings().end()) {
      logger->error("Screening with Id {} not found", m_screeningId);
      return {};
   }

   return screening->second.availableSeats();
}

bool MakeBooking::execute(db::Storage& storage) const {
   const auto& screeningIt = storage.screenings().find(m_screeningId);
   if (screeningIt == storage.screenings().end()) {
      logger->error("Screening {} doesn't exist", m_screeningId);
      return false;
   }
   return screeningIt->second.makeBooking(m_seatsToBook);
}
std::vector<datamodel::Screening> queries::GetAllScreenings::execute(const db::Storage& storage) const {
   std::vector<datamodel::Screening> result;
   result.reserve(storage.screenings().size());

   for (const auto& [id, screening] : storage.screenings()) {
      result.push_back(screening);
   }

   return result;
}

std::vector<datamodel::Theater> queries::GetAllTheaters::execute(const db::Storage& storage) const {

   std::vector<datamodel::Theater> result;
   result.reserve(storage.theaters().size());

   for (const auto& [id, theater] : storage.theaters()) {
      result.push_back(theater);
   }

   return result;
}
std::vector<datamodel::Screening> GetScreeningsWithMovieAndTheater::execute(const db::Storage& storage) const {

   auto matches = storage.screenings() | std::views::values | std::views::filter([this](const auto& screening) {
                     return screening.movieId() == m_movieId && screening.theaterId() == m_theaterId;
                  });

   if (matches.empty()) {
      logger->error("Screening with movieId {} and teatherId {} not found", m_movieId, m_theaterId);
      return {};
   }

   std::vector<datamodel::Screening> result;

   for (const auto& screening : matches) {
      result.push_back(screening);
   }

   return result;
}
