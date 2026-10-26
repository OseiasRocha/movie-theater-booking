/**
 * @file
 * @brief Optional fmt formatters for data models; include where models are logged or formatted.
 */

#pragma once

#include "Movie.h"
#include "Seat.h"
#include "Theater.h"
#include <spdlog/fmt/fmt.h>

/** @namespace fmt
 * @brief External formatting namespace extended with model formatters.
 */

/** @brief Format a movie using its display name and string format options. */
template <> struct fmt::formatter<datamodel::Movie> : fmt::formatter<fmt::string_view> {

   /**
    * @brief Write the movie name using the inherited string formatter.
    * @param movie Record to format.
    * @param ctx Destination formatting context.
    * @return Iterator past the formatted output.
    */
   template <typename FormatContext> auto format(const datamodel::Movie& movie, FormatContext& ctx) const {
      const auto name = movie.name();

      return fmt::formatter<fmt::string_view>::format(fmt::string_view{name.data(), name.size()}, ctx);
   }
};

/** @brief Format a theater using its display name and string format options. */
template <> struct fmt::formatter<datamodel::Theater> : fmt::formatter<fmt::string_view> {

   /**
    * @brief Write the theater name using the inherited string formatter.
    * @param theater Record to format.
    * @param ctx Destination formatting context.
    * @return Iterator past the formatted output.
    */
   template <typename FormatContext> auto format(const datamodel::Theater& theater, FormatContext& ctx) const {
      const auto name = theater.name();

      return fmt::formatter<fmt::string_view>::format(fmt::string_view{name.data(), name.size()}, ctx);
   }
};

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
