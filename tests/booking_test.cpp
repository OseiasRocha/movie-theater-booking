/**
 * @file
 * @brief Regression tests for configuration, data models, database snapshots, and concurrency.
 */

#include "DB/DB.h"
#include "DB/queries.h"
#include "DataModel/Screening.h"
#include "config/config.h"
#include <cstddef>
#include <cstdint>
#include <json/reader.h>
#include <json/value.h>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <thread>
#include <trantor/utils/Logger.h>
#include <tuple>
#include <type_traits>
#include <vector>

/**
 * @defgroup unit_tests Unit and regression tests
 * @brief Configuration validation, model behavior, DB ownership, and concurrency checks.
 *
 * Configuration and database singleton tests run in EXPECT_EXIT child processes.
 * The parent never initializes those singletons, giving each case fresh startup state.
 * Competing booking tests use barriers to start workers together and join before assertions.
 */
/// @addtogroup unit_tests
/// @{
namespace {

using config::Configs;
using queries::GetAllMovies;
using queries::GetAvailableSeats;
using queries::GetMovieByName;
using queries::GetTheatersWithMovie;
using queries::MakeBooking;

/** @test LoggingTest: trantor has spdlog support. */
TEST(LoggingTest, TrantorHasSpdlogSupport) {
   EXPECT_TRUE(trantor::Logger::hasSpdLogSupport())
      << "Reinstall Conan dependencies with trantor/*:with_spdlog=True and rebuild.";
}

static_assert(db::WriteQuery<MakeBooking>);
static_assert(!db::ReadQuery<MakeBooking>);
static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<const db::Storage&>().screenings())>>);

/** @test ScreeningTest: multiple instances keep independent bookings. */
TEST(ScreeningTest, MultipleInstancesKeepIndependentBookings) {
   datamodel::Screening first{100, 10, 1, {datamodel::Seat{"A1"}}};
   datamodel::Screening second{101, 10, 2, {datamodel::Seat{"A1"}}};
   EXPECT_TRUE(first.makeBooking({"A1"}));
   EXPECT_TRUE(first.availableSeats().empty());
   ASSERT_EQ(second.availableSeats().size(), 1u);
   EXPECT_TRUE(second.makeBooking({"A1"}));
}

/** @test ScreeningTest: booking validates entire request before changing seats. */
TEST(ScreeningTest, BookingValidatesEntireRequestBeforeChangingSeats) {
   datamodel::Screening screening{100, 10, 1, {datamodel::Seat{"A1"}, datamodel::Seat{"A2"}}};
   EXPECT_FALSE(screening.makeBooking({}));
   EXPECT_FALSE(screening.makeBooking({"A1", "missing"}));
   EXPECT_FALSE(screening.makeBooking({"A1", "A1"}));
   EXPECT_FALSE(screening.makeBooking({"A1", ""}));
   EXPECT_EQ(screening.availableSeats().size(), 2u);
   EXPECT_TRUE(screening.makeBooking({"A1"}));
   EXPECT_FALSE(screening.makeBooking({"A1"}));
   EXPECT_FALSE(screening.makeBooking({"A2", "A1"}));
   const auto remaining = screening.availableSeats();
   ASSERT_EQ(remaining.size(), 1u);
   EXPECT_EQ(remaining[0].seat(), "A2");
   EXPECT_TRUE(screening.makeBooking({"A2"}));
   EXPECT_TRUE(screening.availableSeats().empty());
}

/** @test ScreeningTest: books multiple seats together. */
TEST(ScreeningTest, BooksMultipleSeatsTogether) {
   datamodel::Screening screening{100, 10, 1, {datamodel::Seat{"A1"}, datamodel::Seat{"A2"}}};
   EXPECT_TRUE(screening.makeBooking({"A2", "A1"}));
   EXPECT_TRUE(screening.availableSeats().empty());
}

/** @test ScreeningTest: empty screening rejects booking. */
TEST(ScreeningTest, EmptyScreeningRejectsBooking) {
   datamodel::Screening screening{100, 10, 1, {}};
   EXPECT_FALSE(screening.makeBooking({"A1"}));
   EXPECT_FALSE(screening.makeBooking({}));
}

/** @test ScreeningTest: available seats returns independent snapshots from const screening. */
TEST(ScreeningTest, AvailableSeatsReturnsIndependentSnapshotsFromConstScreening) {
   const datamodel::Screening screening{100, 10, 1, {datamodel::Seat{"A1"}, datamodel::Seat{"A2"}}};
   auto seats = screening.availableSeats();
   ASSERT_EQ(seats.size(), 2u);
   EXPECT_EQ(seats[0].seat(), "A1");
   EXPECT_EQ(seats[1].seat(), "A2");
   seats[0].setSeat("changed");
   seats[1].setIsBooked(true);

   const auto secondSnapshot = screening.availableSeats();
   ASSERT_EQ(secondSnapshot.size(), 2u);
   EXPECT_EQ(secondSnapshot[0].seat(), "A1");
   EXPECT_EQ(secondSnapshot[1].seat(), "A2");
   EXPECT_FALSE(secondSnapshot[0].isBooked());
   EXPECT_FALSE(secondSnapshot[1].isBooked());
}

/** @test ScreeningTest: available seats of empty screening is empty. */
TEST(ScreeningTest, AvailableSeatsOfEmptyScreeningIsEmpty) {
   const datamodel::Screening screening{100, 10, 1, {}};
   EXPECT_TRUE(screening.availableSeats().empty());
}

/** @test ScreeningTest: rejects construction above capacity. */
TEST(ScreeningTest, RejectsConstructionAboveCapacity) {
   const std::vector<datamodel::Seat> seats(datamodel::Screening::MAX_SEATS + 1, datamodel::Seat{"A1"});
   EXPECT_THROW((datamodel::Screening{100, 10, 1, seats}), std::length_error);
}

/** @test ScreeningTest: books entire capacity and rejects oversized request without changes. */
TEST(ScreeningTest, BooksEntireCapacityAndRejectsOversizedRequestWithoutChanges) {
   std::vector<datamodel::Seat> seats;
   std::vector<std::string> names;
   for (std::size_t i = 0; i < datamodel::Screening::MAX_SEATS; ++i) {
      names.push_back("A" + std::to_string(i));
      seats.emplace_back(names.back());
   }
   datamodel::Screening screening{100, 10, 1, seats};
   auto oversized = names;
   oversized.push_back("extra");
   EXPECT_FALSE(screening.makeBooking(oversized));
   EXPECT_EQ(screening.availableSeats().size(), seats.size());
   EXPECT_TRUE(screening.makeBooking(names));
   EXPECT_TRUE(screening.availableSeats().empty());
   EXPECT_FALSE(screening.makeBooking(names));
}

/** @test ScreeningTest: non adjacent duplicate does not book earlier seats. */
TEST(ScreeningTest, NonAdjacentDuplicateDoesNotBookEarlierSeats) {
   datamodel::Screening screening{100, 10, 1, {datamodel::Seat{"A1"}, datamodel::Seat{"A2"}, datamodel::Seat{"A3"}}};
   EXPECT_FALSE(screening.makeBooking({"A1", "A2", "A1"}));
   EXPECT_EQ(screening.availableSeats().size(), 3u);
   EXPECT_TRUE(screening.makeBooking({"A1", "A2", "A3"}));
}

/** @test ScreeningTest: preserves pre booked seats and finds remaining seats across gaps. */
TEST(ScreeningTest, PreservesPreBookedSeatsAndFindsRemainingSeatsAcrossGaps) {
   datamodel::Seat booked{"A2"};
   booked.setIsBooked(true);
   datamodel::Screening screening{
      100, 10, 1, {datamodel::Seat{"A1"}, booked, datamodel::Seat{"A3"}, datamodel::Seat{"A4"}}};
   EXPECT_FALSE(screening.makeBooking({"A1", "A2"}));
   EXPECT_EQ(screening.availableSeats().size(), 3u);
   EXPECT_TRUE(screening.makeBooking({"A3"}));
   std::vector<std::string> remaining;
   for (const auto& seat : screening.availableSeats()) {
      remaining.emplace_back(seat.seat());
   }
   EXPECT_EQ(remaining, (std::vector<std::string>{"A1", "A4"}));
   const auto snapshot = screening.availableSeats();
   ASSERT_EQ(snapshot.size(), 2u);
   EXPECT_EQ(snapshot[0].seat(), "A1");
   EXPECT_EQ(snapshot[1].seat(), "A4");
   EXPECT_TRUE(screening.makeBooking({"A4", "A1"}));
   EXPECT_TRUE(screening.availableSeats().empty());
}

/** @test ScreeningTest: seat names require exact match. */
TEST(ScreeningTest, SeatNamesRequireExactMatch) {
   datamodel::Screening screening{100, 10, 1, {datamodel::Seat{"A1"}}};
   for (const std::string name : {"a1", "A", "A1 ", " A1"}) {
      EXPECT_FALSE(screening.makeBooking({name})) << name;
   }
   EXPECT_TRUE(screening.makeBooking({"A1"}));
}

/**
 * @brief Build a valid database fixture with unscheduled movies and repeated movie/theater pairs.
 * @return A mutable JSON document that each test can customize independently.
 * @throws std::runtime_error If the embedded fixture JSON cannot be parsed.
 */
Json::Value validConfig() {
   Json::Value root;
   Json::Reader reader;
   if (!reader.parse(R"({
      "movies": [{"id": 1, "name": "Interstellar"}, {"id": 2, "name": "Arrival"},
                 {"id": 3, "name": "Unscheduled"}],
      "theaters": [{"id": 10, "name": "Cinema One"}, {"id": 20, "name": "Cinema Two"},
                   {"id": 30, "name": "Unused"}],
      "screenings": [
         {"id": 100, "movieId": 1, "theaterId": 10, "seats": [{"name": "A1"}, {"name": "A2"}]},
         {"id": 101, "movieId": 1, "theaterId": 10, "seats": [{"name": "B1"}]},
         {"id": 102, "movieId": 1, "theaterId": 20, "seats": [{"name": "C1"}]},
         {"id": 103, "movieId": 2, "theaterId": 20, "seats": [{"name": "D1"}]}
      ]
   })",
                     root)) {
      throw std::runtime_error("Invalid test fixture");
   }
   return root;
}

/**
 * @brief Set or unset a process environment variable for isolated startup tests.
 * @param name Variable name.
 * @param value Replacement value, or nullptr to unset it (empty on Windows).
 * @throws std::runtime_error If the platform environment update fails.
 * @note Use in child processes to avoid affecting other tests or threads.
 */
void setEnvironment(const char* name, const char* value) {
#ifdef _WIN32
   if (_putenv_s(name, value == nullptr ? "" : value) != 0) {
#else
   if ((value == nullptr ? unsetenv(name) : setenv(name, value, 1)) != 0) {
#endif
      throw std::runtime_error("Cannot set test environment variable");
   }
}

/** @brief Point configuration loading at an isolated test document.
 * @param path Database JSON file to load in the child process.
 */
void setConfigPath(const std::filesystem::path& path) { setEnvironment("CONFIG_DB_FILE_PATH", path.string().c_str()); }

/**
 * @brief Fixture owning a temporary configuration file for singleton initialization tests.
 * @details Each test modifies its own document; child processes consume it and exit.
 */
class ConfigFileTest : public testing::Test {
 protected:
   /// @brief Unique temporary directory removed after each test.
   std::filesystem::path directory;
   /// @brief Database JSON path within the temporary directory.
   std::filesystem::path path;

   /// @brief Create an isolated directory and write a valid initial configuration.
   void SetUp() override {
      static std::atomic<unsigned> sequence{0};
      const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
      directory = std::filesystem::temp_directory_path() /
                  ("booking-config-" + std::to_string(stamp) + "-" + std::to_string(sequence++));
      ASSERT_TRUE(std::filesystem::create_directory(directory));
      path = directory / "database.json";
      write(validConfig().toStyledString());
   }

   /// @brief Remove temporary files without throwing from cleanup.
   void TearDown() override {
      std::error_code error;
      std::filesystem::remove_all(directory, error);
   }

   /** @brief Replace the fixture JSON file and assert successful writing.
    * @param contents Serialized document to write, including intentionally invalid input.
    */
   void write(const std::string& contents) {
      std::ofstream file(path);
      file << contents;
      file.close();
      ASSERT_TRUE(file);
   }

   // Each child initializes its own singletons; the parent never initializes them.
   /**
    * @brief Assert that Configs initialization fails in a fresh child process.
    * @param message Regular expression matched against the reported exception message.
    */
   void expectRejected(const std::string& message) {
      EXPECT_EXIT(
         {
            setConfigPath(path);
            try {
               (void)Configs::getInstance();
            } catch (const std::exception& error) {
               std::cerr << error.what();
               std::exit(1);
            }
            std::exit(0);
         },
         testing::ExitedWithCode(1), message);
   }
};

/** @test ConfigFileTest: loads arrays and reuses singleton. */
TEST_F(ConfigFileTest, LoadsArraysAndReusesSingleton) {
   EXPECT_EXIT(
      {
         setConfigPath(path);
         const auto& config = Configs::getInstance();
         EXPECT_EQ(&config, &Configs::getInstance());
         EXPECT_EQ(config.getMovies(), validConfig()["movies"]);
         EXPECT_EQ(config.getTheaters(), validConfig()["theaters"]);
         EXPECT_EQ(config.getScreenings(), validConfig()["screenings"]);
         std::exit(testing::Test::HasFailure() ? 1 : 0);
      },
      testing::ExitedWithCode(0), "");
}

/** @test ConfigFileTest: server overrides ignore invalid environment values. */
TEST_F(ConfigFileTest, ServerOverridesIgnoreInvalidEnvironmentValues) {
   EXPECT_EXIT(
      {
         setConfigPath(path);
         setEnvironment("ADDRESS", "");
         setEnvironment("PORT", "invalid");
         setEnvironment("LOG_LEVEL", "invalid");
         const auto& config = Configs::getInstance("127.0.0.1", "9000", "debug");
         EXPECT_EQ(config.logLevel(), "debug");
         EXPECT_EQ(config.address(), "127.0.0.1");
         EXPECT_EQ(config.port(), 9000);
         EXPECT_EQ(&config, &Configs::getInstance());
         EXPECT_EQ(Configs::getInstance().port(), 9000);
         std::exit(testing::Test::HasFailure() ? 1 : 0);
      },
      testing::ExitedWithCode(0), "");
}

/** @test ConfigFileTest: log level uses environment and validates overrides. */
TEST_F(ConfigFileTest, LogLevelUsesEnvironmentAndValidatesOverrides) {
   EXPECT_EXIT(
      {
         setConfigPath(path);
         setEnvironment("LOG_LEVEL", "trace");
         EXPECT_EQ(Configs::getInstance("127.0.0.1", "8080").logLevel(), "trace");
         std::exit(testing::Test::HasFailure() ? 1 : 0);
      },
      testing::ExitedWithCode(0), "");
   for (const char* level : {"", "invalid", "DEBUG"}) {
      EXPECT_EXIT(
         {
            setConfigPath(path);
            try {
               (void)Configs::getInstance("127.0.0.1", "8080", level);
            } catch (const std::invalid_argument& error) {
               std::cerr << error.what();
               std::exit(1);
            }
            std::exit(0);
         },
         testing::ExitedWithCode(1), "Invalid log level");
   }
}

/** @test ConfigFileTest: log level defaults to info. */
TEST_F(ConfigFileTest, LogLevelDefaultsToInfo) {
   EXPECT_EXIT(
      {
         setConfigPath(path);
         setEnvironment("LOG_LEVEL", nullptr);
         EXPECT_EQ(Configs::getInstance("127.0.0.1", "8080").logLevel(), "info");
         std::exit(testing::Test::HasFailure() ? 1 : 0);
      },
      testing::ExitedWithCode(0), "");
}

/** @test ConfigFileTest: address override still uses environment port. */
TEST_F(ConfigFileTest, AddressOverrideStillUsesEnvironmentPort) {
   EXPECT_EXIT(
      {
         setConfigPath(path);
         setEnvironment("ADDRESS", "");
         setEnvironment("PORT", "65535");
         const auto& config = Configs::getInstance("127.0.0.1");
         EXPECT_EQ(config.address(), "127.0.0.1");
         EXPECT_EQ(config.port(), 65535);
         std::exit(testing::Test::HasFailure() ? 1 : 0);
      },
      testing::ExitedWithCode(0), "");
}

/** @test ConfigFileTest: port override still uses environment address. */
TEST_F(ConfigFileTest, PortOverrideStillUsesEnvironmentAddress) {
   EXPECT_EXIT(
      {
         setConfigPath(path);
         setEnvironment("ADDRESS", "127.0.0.2");
         setEnvironment("PORT", "invalid");
         const auto& config = Configs::getInstance(std::nullopt, "1");
         EXPECT_EQ(config.address(), "127.0.0.2");
         EXPECT_EQ(config.port(), 1);
         std::exit(testing::Test::HasFailure() ? 1 : 0);
      },
      testing::ExitedWithCode(0), "");
}

/** @test ConfigFileTest: server settings use environment without overrides. */
TEST_F(ConfigFileTest, ServerSettingsUseEnvironmentWithoutOverrides) {
   EXPECT_EXIT(
      {
         setConfigPath(path);
         setEnvironment("ADDRESS", "127.0.0.3");
         setEnvironment("PORT", "9090");
         const auto& config = Configs::getInstance();
         EXPECT_EQ(config.address(), "127.0.0.3");
         EXPECT_EQ(config.port(), 9090);
         std::exit(testing::Test::HasFailure() ? 1 : 0);
      },
      testing::ExitedWithCode(0), "");
}

/** @test ConfigFileTest: server settings use defaults without environment or overrides. */
TEST_F(ConfigFileTest, ServerSettingsUseDefaultsWithoutEnvironmentOrOverrides) {
   EXPECT_EXIT(
      {
         setConfigPath(path);
         setEnvironment("ADDRESS", nullptr);
         setEnvironment("PORT", nullptr);
         const auto& config = Configs::getInstance();
         EXPECT_EQ(config.address(), "0.0.0.0");
         EXPECT_EQ(config.port(), 8080);
         std::exit(testing::Test::HasFailure() ? 1 : 0);
      },
      testing::ExitedWithCode(0), "");
}

/** @test ConfigFileTest: rejects invalid ports from overrides and environment. */
TEST_F(ConfigFileTest, RejectsInvalidPortsFromOverridesAndEnvironment) {
   for (const char* port : {"", "0", "65536", "-1", "80abc", "1.5", " 80", "99999999999999999999"}) {
      SCOPED_TRACE(port);
      for (bool overridePort : {false, true}) {
         EXPECT_EXIT(
            {
               setConfigPath(path);
               setEnvironment("ADDRESS", "127.0.0.1");
               setEnvironment("PORT", overridePort ? "8080" : port);
               try {
                  (void)Configs::getInstance(std::nullopt,
                                             overridePort ? std::optional<std::string_view>{port} : std::nullopt);
               } catch (const std::invalid_argument& error) {
                  std::cerr << error.what();
                  std::exit(1);
               }
               std::exit(0);
            },
            testing::ExitedWithCode(1), "Port must be an integer between 1 and 65535");
      }
   }
}

/** @test ConfigFileTest: empty address override does not fall back to environment. */
TEST_F(ConfigFileTest, EmptyAddressOverrideDoesNotFallBackToEnvironment) {
   EXPECT_EXIT(
      {
         setConfigPath(path);
         setEnvironment("ADDRESS", "127.0.0.1");
         try {
            (void)Configs::getInstance("", "8080");
         } catch (const std::invalid_argument& error) {
            std::cerr << error.what();
            std::exit(1);
         }
         std::exit(0);
      },
      testing::ExitedWithCode(1), "Address must not be empty");
}

/** @test ConfigFileTest: loads default path when environment is unset. */
TEST_F(ConfigFileTest, LoadsDefaultPathWhenEnvironmentIsUnset) {
   std::filesystem::create_directory(directory / "config");
   std::filesystem::copy_file(path, directory / "config/db.json");
   EXPECT_EXIT(
      {
#ifdef _WIN32
         _putenv_s("CONFIG_DB_FILE_PATH", "");
#else
         unsetenv("CONFIG_DB_FILE_PATH");
#endif
         std::filesystem::current_path(directory);
         EXPECT_EQ(Configs::getInstance().getMovies(), validConfig()["movies"]);
         std::exit(testing::Test::HasFailure() ? 1 : 0);
      },
      testing::ExitedWithCode(0), "");
}

/** @test ConfigFileTest: rejects missing file. */
TEST_F(ConfigFileTest, RejectsMissingFile) {
   std::filesystem::remove(path);
   expectRejected("Failed to open config file");
}

/** @test ConfigFileTest: rejects malformed json. */
TEST_F(ConfigFileTest, RejectsMalformedJson) {
   write("{ invalid");
   expectRejected("Failed to parse");
}

/** @test ConfigFileTest: rejects duplicate json keys. */
TEST_F(ConfigFileTest, RejectsDuplicateJsonKeys) {
   write(R"({"movies": [], "movies": [], "theaters": [], "screenings": []})");
   expectRejected("Failed to parse");
}

/** @test ConfigFileTest: rejects trailing json. */
TEST_F(ConfigFileTest, RejectsTrailingJson) {
   write(validConfig().toStyledString() + "{}");
   expectRejected("Failed to parse");
}

/** @test ConfigFileTest: rejects empty seats. */
TEST_F(ConfigFileTest, RejectsEmptySeats) {
   auto config = validConfig();
   config["screenings"][0]["seats"] = Json::Value(Json::arrayValue);
   write(config.toStyledString());
   expectRejected("Screening 100 must have at least one seat");
}

/** @test ConfigFileTest: accepts empty database. */
TEST_F(ConfigFileTest, AcceptsEmptyDatabase) {
   Json::Value config;
   for (const char* key : {"movies", "theaters", "screenings"}) {
      config[key] = Json::Value(Json::arrayValue);
   }
   write(config.toStyledString());
   EXPECT_EXIT(
      {
         setConfigPath(path);
         EXPECT_TRUE(Configs::getInstance().getMovies().empty());
         EXPECT_TRUE(db::DB::getInstance().query(GetAllMovies{}).empty());
         EXPECT_TRUE(db::DB::getInstance().query(GetTheatersWithMovie{1}).empty());
         EXPECT_FALSE(db::DB::getInstance().query(GetMovieByName{"missing"}));
         EXPECT_TRUE(db::DB::getInstance().query(GetAvailableSeats{100}).empty());
         EXPECT_TRUE(db::DB::getInstance().query(queries::GetAllScreenings{}).empty());
         EXPECT_TRUE(db::DB::getInstance().query(queries::GetScreeningsWithMovieAndTheater{1, 10}).empty());
         std::exit(testing::Test::HasFailure() ? 1 : 0);
      },
      testing::ExitedWithCode(0), "");
}

/** @brief Named malformed document and expected validation diagnostic. */
struct InvalidConfig {
   /// @brief Stable parameterized-test case name.
   std::string name;
   /// @brief Malformed document written by the parameterized case.
   Json::Value document;
   /// @brief Expected diagnostic regular expression.
   std::string message;
};

/** @brief Generate independent mutations testing malformed fields, IDs, references, and seat limits.
 * @return Named invalid configurations and their expected exception diagnostics.
 */
std::vector<InvalidConfig> invalidConfigs() {
   std::vector<InvalidConfig> cases;
   auto add = [&](const std::string& name, auto mutate, const std::string& message) {
      auto config = validConfig();
      mutate(config);
      cases.push_back({name, config, message});
   };
   add("RootArray", [](auto& c) { c = Json::Value(Json::arrayValue); }, "Config file malformed");
   for (const std::string key : {"movies", "theaters", "screenings"}) {
      add("Missing_" + key, [&](auto& c) { c.removeMember(key); }, "must be an array");
      add("NotArray_" + key, [&](auto& c) { c[key] = "invalid"; }, "must be an array");
      add("NotObject_" + key, [&](auto& c) { c[key][0] = 7; }, "Invalid .* record");
      add("InvalidId_" + key, [&](auto& c) { c[key][0]["id"] = -1; }, "Invalid .* record");
      add(
         "OverflowId_" + key,
         [&](auto& c) { c[key][0]["id"] = Json::UInt64{std::numeric_limits<uint32_t>::max()} + 1; },
         "Invalid .* record");
      add("FractionalId_" + key, [&](auto& c) { c[key][0]["id"] = 1.5; }, "Invalid .* record");
      add("StringId_" + key, [&](auto& c) { c[key][0]["id"] = "1"; }, "Invalid .* record");
      add("NullId_" + key, [&](auto& c) { c[key][0]["id"] = Json::Value{}; }, "Invalid .* record");
      add("MissingId_" + key, [&](auto& c) { c[key][0].removeMember("id"); }, "Invalid .* record");
      add("DuplicateId_" + key, [&](auto& c) { c[key].append(c[key][0]); }, "Duplicated .* id");
   }
   for (const std::string key : {"movies", "theaters"}) {
      add("InvalidName_" + key, [&](auto& c) { c[key][0]["name"] = 42; }, "Invalid .* record");
      add("EmptyName_" + key, [&](auto& c) { c[key][0]["name"] = ""; }, "Invalid .* record");
   }
   add("UnknownMovie", [](auto& c) { c["screenings"][0]["movieId"] = 999; }, "Movie id 999 not found");
   add("UnknownTheater", [](auto& c) { c["screenings"][0]["theaterId"] = 999; }, "Theater id 999 not found");
   add("InvalidMovieId", [](auto& c) { c["screenings"][0]["movieId"] = "1"; }, "Invalid screening record");
   add("InvalidTheaterId", [](auto& c) { c["screenings"][0]["theaterId"] = "10"; }, "Invalid screening record");
   add("InvalidSeats", [](auto& c) { c["screenings"][0]["seats"] = "A1"; }, "Invalid screening record");
   add("SeatNotObject", [](auto& c) { c["screenings"][0]["seats"][0] = "A1"; }, "Invalid seat record");
   add("SeatNameMissing", [](auto& c) { c["screenings"][0]["seats"][0].removeMember("name"); }, "Invalid seat record");
   add("SeatNameInvalid", [](auto& c) { c["screenings"][0]["seats"][0]["name"] = false; }, "Invalid seat record");
   add("SeatNameEmpty", [](auto& c) { c["screenings"][0]["seats"][0]["name"] = ""; }, "Invalid seat record");
   add(
      "DuplicateSeat", [](auto& c) { c["screenings"][0]["seats"].append(c["screenings"][0]["seats"][0]); },
      "Duplicated seat");
   add(
      "TooManySeats",
      [](auto& c) {
         auto& seats = c["screenings"][0]["seats"];
         seats.clear();
         for (std::size_t i = 0; i <= datamodel::Screening::MAX_SEATS; ++i) {
            Json::Value seat;
            seat["name"] = "A" + std::to_string(i);
            seats.append(seat);
         }
      },
      "Too many seats");
   return cases;
}

/** @brief Apply each invalid document to the isolated configuration fixture. */
class ConfigValidationTest : public ConfigFileTest, public testing::WithParamInterface<InvalidConfig> {};

/** @test ConfigValidationTest: rejects invalid input. */
TEST_P(ConfigValidationTest, RejectsInvalidInput) {
   write(GetParam().document.toStyledString());
   expectRejected(GetParam().message);
}

/** @brief Register one configuration rejection test per named malformed document. */
INSTANTIATE_TEST_SUITE_P(Validation, ConfigValidationTest, testing::ValuesIn(invalidConfigs()),
                         [](const auto& info) { return info.param.name; });

/** @test ConfigFileTest: accepts maximum seat count. */
TEST_F(ConfigFileTest, AcceptsMaximumSeatCount) {
   auto config = validConfig();
   auto& seats = config["screenings"][0]["seats"];
   seats.clear();
   for (std::size_t i = 0; i < datamodel::Screening::MAX_SEATS; ++i) {
      Json::Value seat;
      seat["name"] = "A" + std::to_string(i);
      seats.append(seat);
   }
   write(config.toStyledString());
   EXPECT_EXIT(
      {
         setConfigPath(path);
         EXPECT_EQ(db::DB::getInstance().query(GetAvailableSeats{100}).size(), datamodel::Screening::MAX_SEATS);
         std::exit(testing::Test::HasFailure() ? 1 : 0);
      },
      testing::ExitedWithCode(0), "");
}

/** @test ConfigFileTest: database loads movies and finds names. */
TEST_F(ConfigFileTest, DatabaseLoadsMoviesAndFindsNames) {
   EXPECT_EXIT(
      {
         setConfigPath(path);
         const auto& database = db::DB::getInstance();
         EXPECT_EQ(&database, &db::DB::getInstance());
         const auto movies = database.query(GetAllMovies{});
         ASSERT_EQ(movies.size(), 3u);
         std::set<uint32_t> ids;
         for (const auto& movie : movies) {
            ids.insert(movie.id());
         }
         EXPECT_EQ(ids, (std::set<uint32_t>{1, 2, 3}));
         const auto movie = database.query(GetMovieByName{"Interstellar"});
         ASSERT_TRUE(movie);
         EXPECT_EQ(movie->id(), 1u);
         EXPECT_EQ(movie->name(), "Interstellar");
         std::string name = "Interstellar";
         const GetMovieByName ownedQuery{name};
         name[0] = 'X';
         const auto ownedMovie = database.query(ownedQuery);
         ASSERT_TRUE(ownedMovie);
         EXPECT_EQ(ownedMovie->id(), 1u);
         const GetMovieByName temporaryQuery{std::string{"Interstellar"}};
         const auto temporaryMovie = database.query(temporaryQuery);
         ASSERT_TRUE(temporaryMovie);
         EXPECT_EQ(temporaryMovie->id(), 1u);
         EXPECT_FALSE(database.query(GetMovieByName{"interstellar"}));
         EXPECT_FALSE(database.query(GetMovieByName{"missing"}));
         std::exit(testing::Test::HasFailure() ? 1 : 0);
      },
      testing::ExitedWithCode(0), "");
}

/** @test ConfigFileTest: database finds distinct theaters by movie. */
TEST_F(ConfigFileTest, DatabaseFindsDistinctTheatersByMovie) {
   EXPECT_EXIT(
      {
         setConfigPath(path);
         const auto& database = db::DB::getInstance();
         std::set<uint32_t> ids;
         for (const auto& theater : database.query(GetTheatersWithMovie{1})) {
            ids.insert(theater.id());
         }
         EXPECT_EQ(ids, (std::set<uint32_t>{10, 20}));
         EXPECT_EQ(database.query(GetTheatersWithMovie{1}).size(), 2u);
         const auto theaters = database.query(GetTheatersWithMovie{2});
         ASSERT_EQ(theaters.size(), 1u);
         EXPECT_EQ(theaters[0].name(), "Cinema Two");
         EXPECT_TRUE(database.query(GetTheatersWithMovie{3}).empty());
         EXPECT_TRUE(database.query(GetTheatersWithMovie{999}).empty());
         std::exit(testing::Test::HasFailure() ? 1 : 0);
      },
      testing::ExitedWithCode(0), "");
}

/** @test ConfigFileTest: database returns only configured seats and independent snapshots. */
TEST_F(ConfigFileTest, DatabaseReturnsOnlyConfiguredSeatsAndIndependentSnapshots) {
   EXPECT_EXIT(
      {
         setConfigPath(path);
         const auto& database = db::DB::getInstance();
         auto seats = database.query(GetAvailableSeats{100});
         ASSERT_EQ(seats.size(), 2u);
         EXPECT_EQ(seats[0].seat(), "A1");
         EXPECT_EQ(seats[1].seat(), "A2");
         EXPECT_FALSE(seats[0].isBooked());
         seats.clear();
         EXPECT_EQ(database.query(GetAvailableSeats{100}).size(), 2u);
         const auto other = database.query(GetAvailableSeats{101});
         ASSERT_EQ(other.size(), 1u);
         EXPECT_EQ(other[0].seat(), "B1");
         const auto last = database.query(GetAvailableSeats{103});
         ASSERT_EQ(last.size(), 1u);
         EXPECT_EQ(last[0].seat(), "D1");
         EXPECT_TRUE(database.query(GetAvailableSeats{999}).empty());
         std::exit(testing::Test::HasFailure() ? 1 : 0);
      },
      testing::ExitedWithCode(0), "");
}

/** @test ConfigFileTest: screening queries return independent identity-only snapshots. */
TEST_F(ConfigFileTest, ScreeningSummariesPreserveIdentityAndFiltersAfterBooking) {
   using Keys = std::set<std::tuple<uint32_t, uint32_t, uint32_t>>;
   static_assert(std::is_trivially_copyable_v<queries::ScreeningSummary>);
   EXPECT_EXIT(
      {
         setConfigPath(path);
         auto& database = db::DB::getInstance();
         const auto keys = [](const auto& rows) {
            Keys result;
            for (const auto& row : rows) {
               result.emplace(row.id, row.movieId, row.theaterId);
            }
            return result;
         };
         const auto expected = (Keys{{100, 1, 10}, {101, 1, 10}, {102, 1, 20}, {103, 2, 20}});
         auto snapshot = database.query(queries::GetAllScreenings{});
         EXPECT_EQ(snapshot.size(), 4u);
         EXPECT_EQ(keys(snapshot), expected);
         snapshot.front().id = 999;
         EXPECT_TRUE(database.query(MakeBooking{{"A1"}, 100}));
         EXPECT_EQ(keys(database.query(queries::GetAllScreenings{})), expected);
         const auto filtered = database.query(queries::GetScreeningsWithMovieAndTheater{1, 10});
         EXPECT_EQ(filtered.size(), 2u);
         EXPECT_EQ(keys(filtered), (Keys{{100, 1, 10}, {101, 1, 10}}));
         EXPECT_TRUE(database.query(queries::GetScreeningsWithMovieAndTheater{999, 10}).empty());
         std::exit(testing::Test::HasFailure() ? 1 : 0);
      },
      testing::ExitedWithCode(0), "");
}

/** @test ConfigFileTest: database bookings persist and reject invalid requests without changes. */
TEST_F(ConfigFileTest, DatabaseBookingsPersistAndRejectInvalidRequestsWithoutChanges) {
   EXPECT_EXIT(
      {
         setConfigPath(path);
         auto& database = db::DB::getInstance();
         EXPECT_FALSE(database.query(MakeBooking{{"A1"}, 999}));
         EXPECT_FALSE(database.query(MakeBooking{{}, 100}));
         EXPECT_FALSE(database.query(MakeBooking{{"A1", "A1"}, 100}));
         EXPECT_FALSE(database.query(MakeBooking{{"A1", "missing"}, 100}));
         EXPECT_FALSE(database.query(MakeBooking{{""}, 100}));
         EXPECT_EQ(database.query(GetAvailableSeats{100}).size(), 2u);
         EXPECT_TRUE(database.query(MakeBooking{{"A1"}, 100}));
         EXPECT_FALSE(database.query(MakeBooking{{"A1"}, 100}));
         EXPECT_FALSE(database.query(MakeBooking{{"A2", "A1"}, 100}));
         const auto seats = database.query(GetAvailableSeats{100});
         ASSERT_EQ(seats.size(), 1u);
         EXPECT_EQ(seats[0].seat(), "A2");
         EXPECT_TRUE(database.query(MakeBooking{{"A2"}, 100}));
         EXPECT_TRUE(database.query(GetAvailableSeats{100}).empty());
         EXPECT_EQ(database.query(GetAvailableSeats{101}).size(), 1u);
         std::exit(testing::Test::HasFailure() ? 1 : 0);
      },
      testing::ExitedWithCode(0), "");
}

/** @test ConfigFileTest: exactly one concurrent booking wins. */
TEST_F(ConfigFileTest, ExactlyOneConcurrentBookingWins) {
   EXPECT_EXIT(
      {
         setConfigPath(path);
         auto& database = db::DB::getInstance();
         std::barrier start{16};
         std::atomic<int> successes{0};
         {
            std::vector<std::jthread> workers;
            for (int i = 0; i < 16; ++i) {
               workers.emplace_back([&] {
                  start.arrive_and_wait();
                  if (database.query(MakeBooking{{"A1", "A2"}, 100})) {
                     ++successes;
                  }
               });
            }
         }
         EXPECT_EQ(successes.load(), 1);
         EXPECT_TRUE(database.query(GetAvailableSeats{100}).empty());
         std::exit(testing::Test::HasFailure() ? 1 : 0);
      },
      testing::ExitedWithCode(0), "");
}

/** @test ConfigFileTest: readers never see partially applied booking. */
TEST_F(ConfigFileTest, ReadersNeverSeePartiallyAppliedBooking) {
   EXPECT_EXIT(
      {
         setConfigPath(path);
         auto& database = db::DB::getInstance();
         std::barrier start{8};
         std::atomic<bool> correct{true};
         {
            std::vector<std::jthread> workers;
            workers.emplace_back([&] {
               start.arrive_and_wait();
               if (!database.query(MakeBooking{{"A1", "A2"}, 100})) {
                  correct = false;
               }
            });
            for (int i = 0; i < 7; ++i) {
               workers.emplace_back([&] {
                  start.arrive_and_wait();
                  for (int iteration = 0; iteration < 100; ++iteration) {
                     const auto count = database.query(GetAvailableSeats{100}).size();
                     if (count != 0 && count != 2) {
                        correct = false;
                     }
                  }
               });
            }
         }
         EXPECT_TRUE(correct.load());
         EXPECT_TRUE(database.query(GetAvailableSeats{100}).empty());
         std::exit(testing::Test::HasFailure() ? 1 : 0);
      },
      testing::ExitedWithCode(0), "");
}

/** @test ConfigFileTest: database supports concurrent reads. */
TEST_F(ConfigFileTest, DatabaseSupportsConcurrentReads) {
   EXPECT_EXIT(
      {
         setConfigPath(path);
         std::atomic<bool> correct{true};
         {
            std::vector<std::jthread> readers;
            for (int i = 0; i < 8; ++i) {
               readers.emplace_back([&] {
                  const auto& database = db::DB::getInstance();
                  for (int iteration = 0; iteration < 50; ++iteration) {
                     if (database.query(GetAllMovies{}).size() != 3 ||
                         database.query(GetTheatersWithMovie{1}).size() != 2 ||
                         database.query(GetAvailableSeats{100}).size() != 2) {
                        correct = false;
                     }
                  }
               });
            }
         } // All readers join before assertions or process exit.
         EXPECT_TRUE(correct.load());
         std::exit(testing::Test::HasFailure() ? 1 : 0);
      },
      testing::ExitedWithCode(0), "");
}

/** @test ConfigFileTest: movie lookup respects string view length. */
TEST_F(ConfigFileTest, MovieLookupRespectsStringViewLength) {
   EXPECT_EXIT(
      {
         setConfigPath(path);
         const auto& database = db::DB::getInstance();
         const std::string padded = "Interstellar-extra";
         const auto movie = database.query(GetMovieByName{std::string_view{padded}.substr(0, 12)});
         ASSERT_TRUE(movie);
         EXPECT_EQ(movie->id(), 1u);
         EXPECT_FALSE(database.query(GetMovieByName{padded}));
         EXPECT_FALSE(database.query(GetMovieByName{std::string_view{}}));
         std::exit(testing::Test::HasFailure() ? 1 : 0);
      },
      testing::ExitedWithCode(0), "");
}

/** @test ConfigFileTest: booking owns input and snapshots remain independent. */
TEST_F(ConfigFileTest, BookingOwnsInputAndSnapshotsRemainIndependent) {
   EXPECT_EXIT(
      {
         setConfigPath(path);
         auto& database = db::DB::getInstance();
         std::vector<std::string> names{"A1"};
         const MakeBooking booking(names, 100);
         names[0] = "missing";
         auto snapshot = database.query(GetAvailableSeats{100});
         ASSERT_EQ(snapshot.size(), 2u);
         snapshot[1].setSeat("changed");
         snapshot[1].setIsBooked(true);
         EXPECT_TRUE(database.query(booking));
         EXPECT_FALSE(snapshot[0].isBooked());
         EXPECT_EQ(snapshot[0].seat(), "A1");
         const auto remaining = database.query(GetAvailableSeats{100});
         ASSERT_EQ(remaining.size(), 1u);
         EXPECT_EQ(remaining[0].seat(), "A2");
         EXPECT_FALSE(remaining[0].isBooked());
         EXPECT_FALSE(database.query(booking));
         std::exit(testing::Test::HasFailure() ? 1 : 0);
      },
      testing::ExitedWithCode(0), "");
}

/** @test ConfigFileTest: overlapping concurrent bookings do not partially book losing request. */
TEST_F(ConfigFileTest, OverlappingConcurrentBookingsDoNotPartiallyBookLosingRequest) {
   auto config = validConfig();
   Json::Value seat;
   seat["name"] = "A3";
   config["screenings"][0]["seats"].append(seat);
   write(config.toStyledString());
   EXPECT_EXIT(
      {
         setConfigPath(path);
         auto& database = db::DB::getInstance();
         std::barrier start{2};
         bool first = false;
         bool second = false;
         {
            std::jthread a([&] {
               start.arrive_and_wait();
               first = database.query(MakeBooking{{"A1", "A2"}, 100});
            });
            std::jthread b([&] {
               start.arrive_and_wait();
               second = database.query(MakeBooking{{"A3", "A2"}, 100});
            });
         }
         EXPECT_NE(first, second);
         const auto remaining = database.query(GetAvailableSeats{100});
         ASSERT_EQ(remaining.size(), 1u);
         EXPECT_EQ(remaining[0].seat(), first ? "A3" : "A1");
         std::exit(testing::Test::HasFailure() ? 1 : 0);
      },
      testing::ExitedWithCode(0), "");
}

/** @test ConfigFileTest: disjoint concurrent bookings both succeed. */
TEST_F(ConfigFileTest, DisjointConcurrentBookingsBothSucceed) {
   EXPECT_EXIT(
      {
         setConfigPath(path);
         auto& database = db::DB::getInstance();
         std::barrier start{2};
         bool first = false;
         bool second = false;
         {
            std::jthread a([&] {
               start.arrive_and_wait();
               first = database.query(MakeBooking{{"A1"}, 100});
            });
            std::jthread b([&] {
               start.arrive_and_wait();
               second = database.query(MakeBooking{{"A2"}, 100});
            });
         }
         EXPECT_TRUE(first);
         EXPECT_TRUE(second);
         EXPECT_TRUE(database.query(GetAvailableSeats{100}).empty());
         std::exit(testing::Test::HasFailure() ? 1 : 0);
      },
      testing::ExitedWithCode(0), "");
}

} // namespace

/// @}
