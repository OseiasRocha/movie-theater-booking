#include "booking.h"

#include <atomic>
#include <iostream>

bool booking::reserve_seats(int& available_seats, int requested_seats)
{
    if (requested_seats <= 0 || requested_seats > available_seats) {
        return false;
    }
    available_seats -= requested_seats;
    return true;
}

// Unit tests compile this file with GoogleTest's entry point instead.
#ifndef MOVIE_THEATER_BOOKING_NO_MAIN
struct A { int a[4]; };
struct B { int x, y; };

int main()
{
    std::atomic<A> a{};
    std::atomic<B> b{};
    std::cout << std::boolalpha
              << "std::atomic<A> is lock free? "
              << std::atomic_is_lock_free(&a) << '\n'
              << "std::atomic<B> is lock free? "
              << std::atomic_is_lock_free(&b) << '\n';

    // constexpr int worker_count = 4;
    // constexpr uint64_t increments_per_worker = 10000;
    // constexpr uint64_t expected = worker_count * increments_per_worker;
    // const uint64_t actual = run_threaded_counter(worker_count, increments_per_worker);
    // std::cout << "Threaded counter: " << actual << '/' << expected << '\n';
    // return actual == expected ? 0 : 1;
    int available_seats = 100;
    const bool reserved = booking::reserve_seats(available_seats, 2);
    std::cout << "Reservation successful? " << reserved << '\n'
              << "Available seats: " << available_seats << '\n';
    return reserved ? 0 : 1;
}
#endif
