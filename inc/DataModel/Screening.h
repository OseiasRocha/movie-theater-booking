#pragma once

#include "Seat.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <stdexcept>
#include <vector>

namespace datamodel {
class Screening {
 public:
   static constexpr std::size_t MAX_SEATS = 20;
   Screening(uint32_t id, uint32_t theaterId, uint32_t movieId, const std::vector<Seat>& seats)
      : m_id(id), m_theaterId(theaterId), m_movieId(movieId), m_numSeats(seats.size()) {
      if (seats.size() > MAX_SEATS) {
         throw std::length_error("Too many seats for screening");
      }
      std::copy(seats.begin(), seats.end(), m_seats.begin());
   }
   inline uint32_t theaterId() const { return m_theaterId; }
   inline uint32_t movieId() const { return m_movieId; }
   inline uint32_t id() const { return m_id; }
   std::vector<Seat> availableSeats() const;
   std::array<Seat, MAX_SEATS> allSeats() const;
   // Caller must hold exclusive access when this screening is shared.
   bool makeBooking(const std::vector<std::string>& seatsToBook);

 private:
   uint32_t m_id;
   uint32_t m_theaterId;
   uint32_t m_movieId;
   std::array<Seat, MAX_SEATS> m_seats;
   std::size_t m_numSeats;
   std::shared_ptr<spdlog::logger> m_logger = [] {
      static auto logger = spdlog::stdout_color_mt("Screening");
      return logger;
   }();
};
} // namespace datamodel
