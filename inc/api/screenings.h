/** @file
 * @brief JSON representation of screening query results.
 */
#pragma once

#include "DB/queries.h"
#include <span>
#include <string>

namespace api {
/** Serialize numeric screening snapshots; filtered results expose only the ID. */
std::string screeningsJson(std::span<const queries::ScreeningSummary> screenings, bool filtered);
} // namespace api
