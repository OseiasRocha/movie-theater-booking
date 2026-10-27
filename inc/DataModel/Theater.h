/**
 * @file
 * @brief Owning theater records.
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace datamodel {
/** @brief An owning theater record identified by a database key. */
class Theater {
 public:
   /**
    * @brief Construct a record, taking ownership of the supplied name.
    * @param id Identifier used by the database.
    * @param name Display name; moved into the record.
    * @note Configuration validation, rather than this constructor, checks names and ID uniqueness.
    */
   Theater(uint32_t id, std::string name) : m_id(id), m_name(std::move(name)) {}
   /**
    * @brief Borrow the display name without allocating.
    * @return A view valid while this record remains alive and its name is unchanged.
    */
   inline std::string_view name() const { return m_name; }

   /// @brief Return the database identifier.
   inline auto id() const { return m_id; }

 private:
   /// @brief Database identifier.
   uint32_t m_id;
   /// @brief Owned display name backing name().
   std::string m_name;
};
} // namespace datamodel
