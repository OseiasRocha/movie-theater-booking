/**
 * @file
 * @brief Read snapshots and atomic booking commands executed through db::DB.
 */

#pragma once

#include "DataModel/Movie.h"
#include "DataModel/Seat.h"
#include "DataModel/Theater.h"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace db {
class Storage;
}

/**
 * @brief Query objects and commands dispatched through db::DB::query().
 *
 * execute() methods do not acquire locks themselves. Read operations return owning
 * snapshots in unspecified map iteration order. Commands require exclusive access.
 */
namespace queries {

/** @brief Copy every movie record. */
struct GetAllMovies {
   /**
    * @brief Copy every movie record.
    * @param storage Read-only storage; the caller holds a shared or exclusive lock.
    * @return An owning vector, empty when no movies are configured.
    */
   std::vector<datamodel::Movie> execute(const db::Storage& storage) const;
};

/** @brief Lightweight screening identity and references, with no seat or booking state. */
struct ScreeningSummary {
   /// @brief Screening identifier.
   uint32_t id;
   /// @brief Movie being shown.
   uint32_t movieId;
   /// @brief Theater hosting the showing.
   uint32_t theaterId;
};

/** @brief List every screening without copying its seat storage. */
struct GetAllScreenings {
   /**
    * @brief Project every screening into a lightweight value snapshot.
    * @param storage Read-only storage; the caller holds a shared or exclusive lock.
    * @return Screening summaries in unspecified map order; empty for an empty database.
    */
   std::vector<ScreeningSummary> execute(const db::Storage& storage) const;
};

/** @brief Copy every theater record. */
struct GetAllTheaters {
   /**
    * @brief Copy every theater record.
    * @param storage Read-only storage; the caller holds a shared or exclusive lock.
    * @return An owning vector, empty when no theaters are configured.
    */
   std::vector<datamodel::Theater> execute(const db::Storage& storage) const;
};

/** @brief Find distinct theaters hosting screenings of a movie. */
struct GetTheatersWithMovie {
   /** @brief Store the movie filter.
    * @param movieId Movie identifier; existence is checked during execution.
    */
   GetTheatersWithMovie(uint32_t movieId) : m_movieId(movieId) {}

   /**
    * @brief Find distinct theaters hosting screenings of a movie.
    * @param storage Read-only storage; the caller holds a shared or exclusive lock.
    * @return Each matching theater once; empty for an unknown or unscheduled movie.
    */
   std::vector<datamodel::Theater> execute(const db::Storage& storage) const;

 private:
   /// @brief Movie identifier used for filtering.
   uint32_t m_movieId;
};

/** @brief Find all screenings for one movie/theater pair. */
struct GetScreeningsWithMovieAndTheater {
   /** @brief Store both screening filters.
    * @param movieId Movie identifier.
    * @param theaterId Hosting theater identifier.
    */
   GetScreeningsWithMovieAndTheater(uint32_t movieId, uint32_t theaterId)
      : m_movieId(movieId), m_theaterId(theaterId) {}

   /**
    * @brief Find all screenings for one movie/theater pair.
    * @param storage Read-only storage; the caller holds a shared or exclusive lock.
    * @return Matching summaries, including repeated showings; empty if none match.
    */
   std::vector<ScreeningSummary> execute(const db::Storage& storage) const;

 private:
   /// @brief Movie identifier used for filtering.
   uint32_t m_movieId;
   /// @brief Theater identifier used for filtering.
   uint32_t m_theaterId;
};

/** @brief Find a movie by exact, case-sensitive name. */
struct GetMovieByName {
   /** @brief Copy the search name into the query.
    * @param name Exact movie name; its backing string need not outlive construction.
    */
   GetMovieByName(std::string_view name) : m_name(name) {}

   /**
    * @brief Find a movie by exact, case-sensitive name.
    * @param storage Read-only storage; the caller holds a shared or exclusive lock.
    * @return A copied movie, or std::nullopt when no exact match exists.
    * @note Duplicate configured names are allowed; the first match in unspecified map order wins.
    */
   std::optional<datamodel::Movie> execute(const db::Storage& storage) const;

 private:
   /// @brief Owned exact-match search name.
   std::string m_name;
};

/** @brief Copy the unbooked seats in one screening. */
struct GetAvailableSeats {
   /** @brief Store the screening to inspect.
    * @param screeningId Screening identifier.
    */
   GetAvailableSeats(uint32_t screeningId) : m_screeningId(screeningId) {}

   /**
    * @brief Copy the unbooked seats in one screening.
    * @param storage Read-only storage; the caller holds a shared or exclusive lock.
    * @return Configured unbooked seats in seat order, or empty for an unknown or sold-out screening.
    */
   std::vector<datamodel::Seat> execute(const db::Storage& storage) const;

 private:
   /// @brief Target screening identifier.
   uint32_t m_screeningId;
};

/** @brief Book requested seats together in a single screening. */
struct MakeBooking {
   /** @brief Take ownership of a seat request for a particular screening.
    * @param seatsToBook Unique, nonempty seat labels; validated during execution.
    * @param screeningId Screening whose seats will be booked.
    */
   MakeBooking(std::vector<std::string> seatsToBook, uint32_t screeningId)
      : m_seatsToBook(std::move(seatsToBook)), m_screeningId(screeningId) {}

   /**
    * @brief Book requested seats together in a single screening.
    * @param storage Mutable storage; the caller holds an exclusive lock.
    * @return True if all seats were booked; false if the screening or request is invalid.
    * @note Validation failure leaves all seats unchanged. Use db::DB::query() to serialize competing bookings.
    */
   bool execute(db::Storage& storage) const;

 private:
   /// @brief Owned requested seat labels.
   std::vector<std::string> m_seatsToBook;
   /// @brief Target screening identifier.
   uint32_t m_screeningId;
};
} // namespace queries
