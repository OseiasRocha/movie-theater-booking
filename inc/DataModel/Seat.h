/**
 * @file
 * @brief Seat names, booking flags, and fmt formatting support.
 */

#pragma once

#include <spdlog/fmt/fmt.h>
#include <string>
#include <string_view>
#include <utility>

namespace datamodel {
/**
 * @brief An owning seat label and its booking state.
 * @note This value type provides no locking. Shared instances require external synchronization.
 */
class Seat {
 public:
   /// @brief Construct an unnamed, unbooked seat for an unused array slot.
   Seat() = default;
   /** @brief Construct an unbooked seat, taking ownership of its label.
    * @param seat Seat label, such as A1; validation belongs to the caller.
    */
   Seat(std::string seat) : m_seat(std::move(seat)) {}
   /** @brief Borrow the seat label.
    * @return A view invalidated when this seat is destroyed, moved, or renamed.
    */
   inline std::string_view seat() const { return m_seat; }
   /// @brief Report whether this seat has been booked.
   inline bool isBooked() const { return m_isBooked; }
   /** @brief Change the booking flag without validation or locking.
    * @param isBooked New booking state.
    */
   inline void setIsBooked(bool isBooked) { m_isBooked = isBooked; }
   /** @brief Copy a new label, invalidating previously borrowed label views.
    * @param seat Replacement label.
    */
   inline void setSeat(const std::string& seat) { m_seat = seat; }

 private:
   /// @brief Owned label; unused array entries have an empty label.
   std::string m_seat;
   /// @brief Booking flag, initially false.
   bool m_isBooked = false;
};
} // namespace datamodel

/** @brief Format a seat using its label and string format options. */
template <> struct fmt::formatter<datamodel::Seat> : fmt::formatter<fmt::string_view> {

   /** @brief Write a seat label to the formatting destination.
    * @param seat Seat to format.
    * @param ctx Destination formatting context.
    * @return Iterator past the formatted output.
    */
   template <typename FormatContext> auto format(const datamodel::Seat& seat, FormatContext& ctx) const {
      const auto name = seat.seat();
      return fmt::formatter<fmt::string_view>::format(fmt::string_view{name.data(), name.size()}, ctx);
   }
};
