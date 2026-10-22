#include <array>
#include <numeric>
#include <span>

#include <spdlog/spdlog.h>

int main()
{
    constexpr std::array values{1, 2, 3, 4, 5};
    const std::span<const int> numbers{values};
    const auto sum = std::accumulate(numbers.begin(), numbers.end(), 0);

    spdlog::info("Hello, C++20! The sum is {}.", sum);

    return 0;
}
