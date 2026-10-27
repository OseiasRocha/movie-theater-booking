/** @file
 * @brief Serialize numeric screening snapshots without an intermediate JSON DOM.
 */
#include "api/screenings.h"
#include <charconv>
#include <iterator>
#include <limits>

std::string api::screeningsJson(std::span<const queries::ScreeningSummary> screenings, bool filtered) {
   std::string result;
   // Each uint32_t has at most ten decimal digits. No string escaping is needed.
   constexpr std::size_t maximumRecordSize = 64;
   if (screenings.size() <= (result.max_size() - 2) / maximumRecordSize) {
      result.reserve(2 + screenings.size() * maximumRecordSize);
   }
   auto appendId = [&](uint32_t value) {
      char digits[std::numeric_limits<uint32_t>::digits10 + 1];
      const auto converted = std::to_chars(std::begin(digits), std::end(digits), value);
      result.append(digits, converted.ptr);
   };
   result += '[';
   for (const auto& screening : screenings) {
      if (result.size() > 1) {
         result += ',';
      }
      result += "{\"id\":";
      appendId(screening.id);
      if (!filtered) {
         result += ",\"movieId\":";
         appendId(screening.movieId);
         result += ",\"theaterId\":";
         appendId(screening.theaterId);
      }
      result += '}';
   }
   result += ']';
   return result;
}
