#pragma once

#include <cstdint>
#include <spdlog/fmt/fmt.h>
#include <string>
#include <string_view>

namespace datamodel {
class Theater {
 public:
   Theater(uint32_t id, std::string name) : m_id(id), m_name(std::move(name)) {}
   inline std::string_view name() const { return m_name; }

   inline auto id() const { return m_id; }

 private:
   uint32_t m_id;
   std::string m_name;
};
} // namespace datamodel

template <> struct fmt::formatter<datamodel::Theater> : fmt::formatter<fmt::string_view> {

   template <typename FormatContext> auto format(const datamodel::Theater& theater, FormatContext& ctx) const {
      const auto name = theater.name();

      return fmt::formatter<fmt::string_view>::format(fmt::string_view{name.data(), name.size()}, ctx);
   }
};
