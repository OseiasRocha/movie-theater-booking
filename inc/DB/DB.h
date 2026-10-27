/**
 * @file
 * @brief In-memory storage, query concepts, and synchronized database access.
 */

#pragma once

#include "DataModel/Movie.h"
#include "DataModel/Screening.h"
#include "DataModel/Theater.h"
#include <concepts>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

namespace db {

/**
 * @brief Owning maps exposed to query objects for the duration of a database lock.
 *
 * Movies, theaters, and screenings are keyed by their configured uint32_t IDs.
 * Only screening storage is mutable through the public interface.
 */
class Storage {
 public:
   /// @brief Borrow the movie map with read-only access to records.
   const auto& movies() const { return m_movies; }
   /// @brief Borrow the screening map with read-only access to records.
   const auto& screenings() const { return m_screenings; }
   /// @brief Borrow the theater map with read-only access to records.
   const auto& theaters() const { return m_theaters; }
   /// @brief Borrow the mutable screening map; shared storage requires exclusive access.
   auto& screenings() { return m_screenings; }

 private:
   friend class DB;
   std::unordered_map<uint32_t, datamodel::Movie> m_movies;
   std::unordered_map<uint32_t, datamodel::Screening> m_screenings;
   std::unordered_map<uint32_t, datamodel::Theater> m_theaters;
};

/** @brief Result type obtained by invoking a const operation with read-only storage.
 * @tparam Q Operation providing execute(const Storage&) const.
 */
template <typename Q> using ReadResult = decltype(std::declval<const Q&>().execute(std::declval<const Storage&>()));

/**
 * @brief Require the exact member signature Result execute(const Storage&) const.
 * @tparam Q Operation type with a single, unambiguous execute member.
 * @note Overloaded, templated, static, or differently qualified execute members
 *       do not satisfy this signature check. Result ownership is not checked.
 */
template <typename Q>
concept ReadQuery = requires {
   { &Q::execute } -> std::same_as<ReadResult<Q> (Q::*)(const Storage&) const>;
};

/** @brief Result type obtained by invoking a const operation with mutable storage.
 * @tparam Q Operation providing execute(Storage&) const.
 */
template <typename Q> using WriteResult = decltype(std::declval<const Q&>().execute(std::declval<Storage&>()));

/** @brief Require the exact member signature Result execute(Storage&) const.
 * @tparam Q Operation type with a single, unambiguous execute member.
 * @note The signature chooses exclusive locking; it does not guarantee exception rollback.
 */
template <typename Q>
concept WriteQuery = requires {
   { &Q::execute } -> std::same_as<WriteResult<Q> (Q::*)(Storage&) const>;
};

/**
 * @brief In-memory database with shared reads and exclusive writes.
 *
 * Initialization copies records from config::Configs once. Data is not persisted
 * after process exit. Read and write operations lock the same mutex for their
 * entire execute call and result construction.
 */
class DB {
 public:
   /**
    * @brief Return the database, creating it on the first call.
    * @return The process-lifetime instance; local-static initialization is thread-safe.
    * @throws std::exception If configuration loading, validation, or allocation fails.
    * @note Initialize config::Configs with startup overrides before the first DB access.
    */
   inline static DB& getInstance() {
      static DB db{};
      return db;
   }

   /**
    * @brief Execute a read operation while holding a shared storage lock.
    * @tparam Query Type satisfying ReadQuery.
    * @param op Operation invoked with const Storage&.
    * @return The result by value, constructed before the lock is released.
    * @warning Queries must return owning snapshots; pointers and views into storage
    *          would outlive this lock. Do not recursively call query() from execute().
    * @note Exceptions propagate and release the lock through RAII.
    */
   template <typename Query>
      requires ReadQuery<Query>
   auto query(const Query& op) const {
      std::shared_lock lock(m_mutex);
      return op.execute(std::as_const(m_storage));
   }

   /**
    * @brief Execute a write operation while holding an exclusive storage lock.
    * @tparam Query Type satisfying WriteQuery.
    * @param op Operation invoked with mutable Storage&.
    * @return The result by value, constructed before the lock is released.
    * @warning Serialization does not provide rollback: commands must validate before
    *          mutation. Borrowed results must not escape and execute() must not reenter DB.
    * @note Exceptions propagate and release the lock through RAII.
    */
   template <typename Query>
      requires WriteQuery<Query>
   auto query(const Query& op) {
      std::unique_lock lock(m_mutex);
      return op.execute(m_storage);
   }

   DB(const DB&) = delete;
   DB(DB&&) = delete;
   DB& operator=(const DB&) = delete;
   DB& operator=(DB&&) = delete;

 private:
   DB();
   mutable std::shared_mutex m_mutex;
   Storage m_storage;
};
} // namespace db
