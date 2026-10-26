/**
 * @file
 * @brief Copy seat snapshots and validate complete booking requests before mutation.
 */

#include "DataModel/Screening.h"
#include "DataModel/Seat.h"
#include "DataModel/formatters.h"
#include <algorithm>
#include <array>
#include <span>
#include <spdlog/fmt/ranges.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <vector>

using namespace datamodel;

namespace {
/**
 * @brief Return the registered logger shared by all screening operations.
 * @return A reference kept alive by the helper's static shared pointer.
 * @note First-use initialization is thread-safe; screening objects store no logger state.
 */
spdlog::logger& screeningLogger() {
   static auto logger = spdlog::stdout_color_mt("Screening");
   return *logger;
}
} // namespace

std::vector<Seat> Screening::availableSeats() const {
   std::vector<Seat> result;
   if (m_numSeats == 0) {
      return result;
   }
   result.reserve(m_numSeats);
   for (std::size_t i = 0; i < m_numSeats; ++i) {
      const auto& seat = m_seats[i];
      if (!seat.isBooked()) {
         result.push_back(seat);
      }
   }
   screeningLogger().trace("Available seats: {}", result);
   return result;
}

std::array<Seat, Screening::MAX_SEATS> Screening::allSeats() const {
   std::array<Seat, MAX_SEATS> result{m_seats};
   return result;
}

bool Screening::makeBooking(const std::vector<std::string>& seatsToBook) {
   if (seatsToBook.empty() || seatsToBook.size() > m_numSeats) {
      return false;
   }

   auto& logger = screeningLogger();
   auto configuredSeats = std::span<Seat>{m_seats}.first(m_numSeats);
   std::array<Seat*, MAX_SEATS> selected{};
   std::size_t count = 0;
   for (const auto& seatToBook : seatsToBook) {
      const auto seat = std::ranges::find_if(
         configuredSeats, [&seatToBook](const auto& candidate) { return seatToBook == candidate.seat(); });
      if (seatToBook.empty() || seat == configuredSeats.end() || seat->isBooked()) {
         logger.error("Seat {} was not found or is already booked", seatToBook);
         return false;
      }
      if (std::find(selected.begin(), selected.begin() + count, &*seat) != selected.begin() + count) {
         logger.error("Seat {} is duplicated in the request", seatToBook);
         return false;
      }
      selected[count++] = &*seat;
   }
   for (std::size_t i = 0; i < count; ++i) {
      selected[i]->setIsBooked(true);
   }
   logger.trace("Booked seats: {}", fmt::join(seatsToBook, " - "));
   return true;
}
