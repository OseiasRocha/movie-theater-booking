#include "DB/DB.h"
#include "DB/queries.h"
#include "DataModel/Screening.h"
#include "config/config.h"

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
#include <type_traits>
#include <vector>

namespace {

using config::Configs;
using queries::GetAllMovies;
using queries::GetAvailableSeats;
using queries::GetMovieByName;
using queries::GetTheatersWithMovie;
using queries::MakeBooking;

TEST(LoggingTest, TrantorHasSpdlogSupport) {
   EXPECT_TRUE(trantor::Logger::hasSpdLogSupport())
      << "Reinstall Conan dependencies with trantor/*:with_spdlog=True and rebuild.";
}

static_assert(db::WriteQuery<MakeBooking>);
static_assert(!db::ReadQuery<MakeBooking>);
static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<const db::Storage&>().screenings())>>);

TEST(ScreeningTest, MultipleInstancesKeepIndependentBookings) {
   datamodel::Screening first{10, 1, {datamodel::Seat{"A1"}}};
   datamodel::Screening second{10, 2, {datamodel::Seat{"A1"}}};
   EXPECT_TRUE(first.makeBooking({"A1"}));
   EXPECT_TRUE(first.availableSeats().empty());
   ASSERT_EQ(second.availableSeats().size(), 1u);
   EXPECT_TRUE(second.makeBooking({"A1"}));
}

TEST(ScreeningTest, BookingValidatesEntireRequestBeforeChangingSeats) {
   datamodel::Screening screening{10, 1, {datamodel::Seat{"A1"}, datamodel::Seat{"A2"}}};
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
   auto view = screening.availableSeatsView();
   ASSERT_NE(view.begin(), view.end());
   EXPECT_EQ(view.begin()->seat(), "A2");
   EXPECT_TRUE(screening.makeBooking({"A2"}));
   EXPECT_TRUE(screening.availableSeats().empty());
   auto emptyView = screening.availableSeatsView();
   EXPECT_EQ(emptyView.begin(), emptyView.end());
}

TEST(ScreeningTest, BooksMultipleSeatsTogether) {
   datamodel::Screening screening{10, 1, {datamodel::Seat{"A1"}, datamodel::Seat{"A2"}}};
   EXPECT_TRUE(screening.makeBooking({"A2", "A1"}));
   EXPECT_TRUE(screening.availableSeats().empty());
}

TEST(ScreeningTest, EmptyScreeningRejectsBooking) {
   datamodel::Screening screening{10, 1, {}};
   EXPECT_FALSE(screening.makeBooking({"A1"}));
   EXPECT_FALSE(screening.makeBooking({}));
}

TEST(ScreeningTest, SeatViewBorrowsConstConfiguredSeats) {
   const datamodel::Screening screening{10, 1, {datamodel::Seat{"A1"}, datamodel::Seat{"A2"}}};
   auto view = screening.availableSeatsView();
   static_assert(std::same_as<std::ranges::range_reference_t<decltype(view)>, const datamodel::Seat&>);
   auto it = view.begin();
   ASSERT_NE(it, view.end());
   EXPECT_EQ(it->seat(), "A1");
   auto secondView = screening.availableSeatsView();
   EXPECT_EQ(&*it, &*secondView.begin());
   ++it;
   ASSERT_NE(it, view.end());
   EXPECT_EQ(it->seat(), "A2");
   ++it;
   EXPECT_EQ(it, view.end());
}

TEST(ScreeningTest, SeatViewOfEmptyScreeningIsEmpty) {
   const datamodel::Screening screening{10, 1, {}};
   auto view = screening.availableSeatsView();
   EXPECT_EQ(view.begin(), view.end());
}

TEST(ScreeningTest, RejectsConstructionAboveCapacity) {
   const std::vector<datamodel::Seat> seats(datamodel::Screening::MAX_SEATS + 1, datamodel::Seat{"A1"});
   EXPECT_THROW((datamodel::Screening{10, 1, seats}), std::length_error);
}

TEST(ScreeningTest, BooksEntireCapacityAndRejectsOversizedRequestWithoutChanges) {
   std::vector<datamodel::Seat> seats;
   std::vector<std::string> names;
   for (std::size_t i = 0; i < datamodel::Screening::MAX_SEATS; ++i) {
      names.push_back("A" + std::to_string(i));
      seats.emplace_back(names.back());
   }
   datamodel::Screening screening{10, 1, seats};
   auto oversized = names;
   oversized.push_back("extra");
   EXPECT_FALSE(screening.makeBooking(oversized));
   EXPECT_EQ(screening.availableSeats().size(), seats.size());
   EXPECT_TRUE(screening.makeBooking(names));
   EXPECT_TRUE(screening.availableSeats().empty());
   EXPECT_TRUE(std::ranges::empty(screening.availableSeatsView()));
   EXPECT_FALSE(screening.makeBooking(names));
}

TEST(ScreeningTest, NonAdjacentDuplicateDoesNotBookEarlierSeats) {
   datamodel::Screening screening{10, 1, {datamodel::Seat{"A1"}, datamodel::Seat{"A2"}, datamodel::Seat{"A3"}}};
   EXPECT_FALSE(screening.makeBooking({"A1", "A2", "A1"}));
   EXPECT_EQ(screening.availableSeats().size(), 3u);
   EXPECT_TRUE(screening.makeBooking({"A1", "A2", "A3"}));
}

TEST(ScreeningTest, PreservesPreBookedSeatsAndFindsRemainingSeatsAcrossGaps) {
   datamodel::Seat booked{"A2"};
   booked.setIsBooked(true);
   datamodel::Screening screening{10, 1, {datamodel::Seat{"A1"}, booked, datamodel::Seat{"A3"}, datamodel::Seat{"A4"}}};
   EXPECT_FALSE(screening.makeBooking({"A1", "A2"}));
   EXPECT_EQ(screening.availableSeats().size(), 3u);
   EXPECT_TRUE(screening.makeBooking({"A3"}));
   std::vector<std::string> remaining;
   for (const auto& seat : screening.availableSeatsView()) {
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

TEST(ScreeningTest, SeatNamesRequireExactMatch) {
   datamodel::Screening screening{10, 1, {datamodel::Seat{"A1"}}};
   for (const std::string name : {"a1", "A", "A1 ", " A1"}) {
      EXPECT_FALSE(screening.makeBooking({name})) << name;
   }
   EXPECT_TRUE(screening.makeBooking({"A1"}));
}

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

void setEnvironment(const char* name, const char* value) {
#ifdef _WIN32
   if (_putenv_s(name, value == nullptr ? "" : value) != 0) {
#else
   if ((value == nullptr ? unsetenv(name) : setenv(name, value, 1)) != 0) {
#endif
      throw std::runtime_error("Cannot set test environment variable");
   }
}

void setConfigPath(const std::filesystem::path& path) { setEnvironment("CONFIG_DB_FILE_PATH", path.string().c_str()); }

class ConfigFileTest : public testing::Test {
 protected:
   std::filesystem::path directory;
   std::filesystem::path path;

   void SetUp() override {
      static std::atomic<unsigned> sequence{0};
      const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
      directory = std::filesystem::temp_directory_path() /
                  ("booking-config-" + std::to_string(stamp) + "-" + std::to_string(sequence++));
      ASSERT_TRUE(std::filesystem::create_directory(directory));
      path = directory / "database.json";
      write(validConfig().toStyledString());
   }

   void TearDown() override {
      std::error_code error;
      std::filesystem::remove_all(directory, error);
   }

   void write(const std::string& contents) {
      std::ofstream file(path);
      file << contents;
      file.close();
      ASSERT_TRUE(file);
   }

   // Each child initializes its own singletons; the parent never initializes them.
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

TEST_F(ConfigFileTest, RejectsMissingFile) {
   std::filesystem::remove(path);
   expectRejected("Failed to open config file");
}

TEST_F(ConfigFileTest, RejectsMalformedJson) {
   write("{ invalid");
   expectRejected("Failed to parse");
}

TEST_F(ConfigFileTest, RejectsDuplicateJsonKeys) {
   write(R"({"movies": [], "movies": [], "theaters": [], "screenings": []})");
   expectRejected("Failed to parse");
}

TEST_F(ConfigFileTest, RejectsTrailingJson) {
   write(validConfig().toStyledString() + "{}");
   expectRejected("Failed to parse");
}

TEST_F(ConfigFileTest, RejectsEmptySeats) {
   auto config = validConfig();
   config["screenings"][0]["seats"] = Json::Value(Json::arrayValue);
   write(config.toStyledString());
   expectRejected("Screening 100 must have at least one seat");
}

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
         std::exit(testing::Test::HasFailure() ? 1 : 0);
      },
      testing::ExitedWithCode(0), "");
}

struct InvalidConfig {
   std::string name;
   Json::Value document;
   std::string message;
};

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

class ConfigValidationTest : public ConfigFileTest, public testing::WithParamInterface<InvalidConfig> {};

TEST_P(ConfigValidationTest, RejectsInvalidInput) {
   write(GetParam().document.toStyledString());
   expectRejected(GetParam().message);
}

INSTANTIATE_TEST_SUITE_P(Validation, ConfigValidationTest, testing::ValuesIn(invalidConfigs()),
                         [](const auto& info) { return info.param.name; });

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
