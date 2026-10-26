#pragma once

#include <spdlog/fmt/fmt.h>
#include <string>
#include <string_view>

namespace datamodel {
class Seat {
 public:
   Seat() = default;
   Seat(std::string seat) : m_seat(std::move(seat)) {}
   inline std::string_view seat() const { return m_seat; }
   inline bool isBooked() const { return m_isBooked; }
   inline void setIsBooked(bool isBooked) { m_isBooked = isBooked; }
   inline void setSeat(const std::string& seat) { m_seat = seat; }

 private:
   std::string m_seat;
   bool m_isBooked = false;
};
} // namespace datamodel

template <> struct fmt::formatter<datamodel::Seat> : fmt::formatter<fmt::string_view> {

   template <typename FormatContext> auto format(const datamodel::Seat& seat, FormatContext& ctx) const {
      const auto name = seat.seat();
      return fmt::formatter<fmt::string_view>::format(fmt::string_view{name.data(), name.size()}, ctx);
   }
};
