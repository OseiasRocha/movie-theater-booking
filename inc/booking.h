#pragma once

namespace booking {

// Rejects invalid requests without changing the available seat count.
bool reserve_seats(int& available_seats, int requested_seats);

} // namespace booking
