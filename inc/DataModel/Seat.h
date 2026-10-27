/**
 * @file
 * @brief Seat names and booking flags.
 */

#pragma once

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
