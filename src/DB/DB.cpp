/**
 * @file
 * @brief Populate database maps from validated configuration records.
 */

#include "DB/DB.h"
#include "DataModel/Seat.h"
#include "config/config.h"
#include <vector>

using namespace db;

DB::DB() {
   const auto& movies = config::Configs::getInstance().getMovies();
   const auto& theaters = config::Configs::getInstance().getTheaters();
   const auto& screenings = config::Configs::getInstance().getScreenings();

   for (const auto& movie : movies) {
      const auto id = movie["id"].asUInt();
      m_storage.m_movies.try_emplace(id, id, movie["name"].asString());
   }
   for (const auto& theater : theaters) {
      const auto id = theater["id"].asUInt();
      m_storage.m_theaters.try_emplace(id, id, theater["name"].asString());
   }
   for (const auto& screening : screenings) {
      std::vector<datamodel::Seat> seats;
      seats.reserve(screening["seats"].size());

      for (const auto& seat : screening["seats"]) {
         seats.push_back(datamodel::Seat{seat["name"].asString()});
      }
      m_storage.m_screenings.try_emplace(screening["id"].asUInt(), screening["id"].asUInt(),
                                         screening["theaterId"].asUInt(), screening["movieId"].asUInt(), seats);
   }
}
