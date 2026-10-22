#include "booking.h"

#include <gtest/gtest.h>

TEST(SeatBooking, SuccessfulReservationReducesAvailability)
{
    int available = 100;
    EXPECT_TRUE(booking::reserve_seats(available, 2));
    EXPECT_EQ(available, 98);
}

TEST(SeatBooking, CanReserveAllRemainingSeats)
{
    int available = 2;
    EXPECT_TRUE(booking::reserve_seats(available, 2));
    EXPECT_EQ(available, 0);
}

TEST(SeatBooking, RejectsRequestsExceedingAvailability)
{
    int available = 2;
    EXPECT_FALSE(booking::reserve_seats(available, 3));
    EXPECT_EQ(available, 2);
}

TEST(SeatBooking, RejectsZeroAndNegativeRequests)
{
    int available = 10;
    EXPECT_FALSE(booking::reserve_seats(available, 0));
    EXPECT_FALSE(booking::reserve_seats(available, -2));
    EXPECT_EQ(available, 10);
}

TEST(SeatBooking, RejectsReservationsWhenSoldOut)
{
    int available = 0;
    EXPECT_FALSE(booking::reserve_seats(available, 1));
    EXPECT_EQ(available, 0);
}

TEST(SeatBooking, ConsecutiveReservationsRespectRemainingAvailability)
{
    int available = 5;
    EXPECT_TRUE(booking::reserve_seats(available, 3));
    EXPECT_FALSE(booking::reserve_seats(available, 3));
    EXPECT_EQ(available, 2);
    EXPECT_TRUE(booking::reserve_seats(available, 2));
    EXPECT_EQ(available, 0);
}
