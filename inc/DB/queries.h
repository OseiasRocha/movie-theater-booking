#pragma once

#include "DB/DB.h"
#include "DataModel/Movie.h"
#include "DataModel/Seat.h"
#include "DataModel/Theater.h"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace queries {

struct GetAllMovies {
   std::vector<datamodel::Movie> execute(const db::Storage& storage) const;
};

struct GetTheatersWithMovie {
   GetTheatersWithMovie(uint32_t movieId) : m_movieId(movieId) {}

   std::vector<datamodel::Theater> execute(const db::Storage& storage) const;

 private:
   uint32_t m_movieId;
};

struct GetMovieByName {
   GetMovieByName(std::string_view name) : m_name(name) {}

   std::optional<datamodel::Movie> execute(const db::Storage& storage) const;

 private:
   std::string m_name;
};

struct GetAvailableSeats {
   GetAvailableSeats(uint32_t screeningId) : m_screeningId(screeningId) {}

   std::vector<datamodel::Seat> execute(const db::Storage& storage) const;

 private:
   uint32_t m_screeningId;
};

struct MakeBooking {
   MakeBooking(std::vector<std::string> seatsToBook, uint32_t screeningId)
      : m_seatsToBook(std::move(seatsToBook)), m_screeningId(screeningId) {}

   bool execute(db::Storage& storage) const;

 private:
   std::vector<std::string> m_seatsToBook;
   uint32_t m_screeningId;
};
} // namespace queries
