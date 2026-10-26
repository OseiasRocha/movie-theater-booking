/**
 * @file
 * @brief Fixed-capacity screenings and all-or-nothing seat booking.
 */

#pragma once

#include "Seat.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace datamodel {
/**
 * @brief One showing of a movie in a theater, with independent seat bookings.
 *
 * Only the first m_numSeats entries of the backing array are configured seats.
 * The configured count stays fixed after booking; Seat::isBooked() tracks availability.
 * @note Copies own independent seat state. Logging uses one implementation-local logger.
 * @warning No internal locking is performed. Use db::DB::query() for shared database records.
 */
class Screening {
 public:
   /// @brief Maximum configured seats in one screening.
   static constexpr std::size_t MAX_SEATS = 20;
   /**
    * @brief Copy the configured seats, including any existing booking flags.
    * @param id Screening identifier.
    * @param theaterId Theater hosting this showing.
    * @param movieId Movie being shown.
    * @param seats Configured seats in their display order.
    * @throws std::length_error If seats exceeds MAX_SEATS.
    * @note Empty, duplicate, and invalid labels are checked by configuration loading,
    *       not by this constructor. An empty vector is allowed here.
    */
   Screening(uint32_t id, uint32_t theaterId, uint32_t movieId, const std::vector<Seat>& seats)
      : m_id(id), m_theaterId(theaterId), m_movieId(movieId), m_numSeats(seats.size()) {
      if (seats.size() > MAX_SEATS) {
         throw std::length_error("Too many seats for screening");
      }
      std::copy(seats.begin(), seats.end(), m_seats.begin());
   }
   /// @brief Return the hosting theater identifier.
   inline uint32_t theaterId() const { return m_theaterId; }
   /// @brief Return the movie identifier.
   inline uint32_t movieId() const { return m_movieId; }
   /// @brief Return the screening identifier.
   inline uint32_t id() const { return m_id; }
   /**
    * @brief Copy all configured seats whose booking flag is false.
    * @return An owning snapshot in configured order; empty when none are available.
    * @pre The caller holds shared or exclusive access if this instance can be mutated concurrently.
    */
   std::vector<Seat> availableSeats() const;
   /**
    * @brief Copy the entire fixed-capacity seat array, including booked seats.
    * @return All MAX_SEATS entries, including unused slots with empty labels.
    * @pre The caller holds shared or exclusive access if this instance is shared.
    */
   std::array<Seat, MAX_SEATS> allSeats() const;
   /**
    * @brief Validate the entire request before marking any requested seats booked.
    * @param seatsToBook Unique, nonempty labels of configured, unbooked seats.
    * @return True on success; false for an empty or oversized request, unknown,
    *         empty, duplicate, or already booked labels. Validation failure changes no seats.
    * @pre The caller holds exclusive access if this instance is shared.
    * @note Atomicity against other threads is provided by the caller's lock;
    *       this method does not acquire a mutex itself.
    */
   bool makeBooking(const std::vector<std::string>& seatsToBook);

 private:
   /// @brief Screening identifier.
   uint32_t m_id;
   /// @brief Hosting theater identifier.
   uint32_t m_theaterId;
   /// @brief Movie identifier.
   uint32_t m_movieId;
   /// @brief Configured seats followed by unused default-constructed slots.
   std::array<Seat, MAX_SEATS> m_seats;
   /// @brief Configured prefix length; never decremented by booking.
   std::size_t m_numSeats;
};
} // namespace datamodel
