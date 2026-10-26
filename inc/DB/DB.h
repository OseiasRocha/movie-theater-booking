#pragma once

#include "DataModel/Movie.h"
#include "DataModel/Screening.h"
#include "DataModel/Theater.h"
#include <concepts>
#include <cstdint>
#include <fcntl.h>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

namespace db {

class Storage {
 public:
   const auto& movies() const { return m_movies; }
   const auto& screenings() const { return m_screenings; }
   const auto& theaters() const { return m_theaters; }
   auto& screenings() { return m_screenings; }

 private:
   friend class DB;
   std::unordered_map<uint32_t, datamodel::Movie> m_movies;
   std::unordered_map<uint32_t, datamodel::Screening> m_screenings;
   std::unordered_map<uint32_t, datamodel::Theater> m_theaters;
};

template <typename Q> using ReadResult = decltype(std::declval<const Q&>().execute(std::declval<const Storage&>()));

template <typename Q>
concept ReadQuery = requires {
   { &Q::execute } -> std::same_as<ReadResult<Q> (Q::*)(const Storage&) const>;
};

template <typename Q> using WriteResult = decltype(std::declval<const Q&>().execute(std::declval<Storage&>()));

template <typename Q>
concept WriteQuery = requires {
   { &Q::execute } -> std::same_as<WriteResult<Q> (Q::*)(Storage&) const>;
};

class DB {
 public:
   inline static DB& getInstance() {
      static DB db{};
      return db;
   }

   template <typename Query>
      requires ReadQuery<Query>
   auto query(const Query& op) const {
      std::shared_lock lock(m_mutex);
      return op.execute(std::as_const(m_storage));
   }

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
