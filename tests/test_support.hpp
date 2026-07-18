// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_support.hpp
 * @brief Dependency-free test harness and filesystem fixtures.
 */

#ifndef LIBPKGRECONCILE_TEST_SUPPORT_HPP
#define LIBPKGRECONCILE_TEST_SUPPORT_HPP

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace test_support {

namespace fs = std::filesystem;

/** Exception used for one assertion failure. */
class failure : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

/** Convert a value supported by operator<< into diagnostic text. */
template <typename T> std::string describe(const T& value) {
  std::ostringstream output;
  output << value;
  return output.str();
}

/** Specialization-friendly diagnostic rendering for enum values. */
template <typename T> std::string describe_enum(T value) {
  using underlying = std::underlying_type_t<T>;
  return std::to_string(static_cast<underlying>(value));
}

/** Raise a test failure with source position. */
[[noreturn]] inline void fail(const char* expression, const char* file,
                              int line, const std::string& detail = {}) {
  std::ostringstream output;
  output << file << ':' << line << ": check failed: " << expression;
  if (!detail.empty()) {
    output << " (" << detail << ')';
  }
  throw failure(output.str());
}

/** Execute named cases and print a TAP-like readable summary. */
class runner {
public:
  /** Execute one test case and retain failure state. */
  template <typename Function>
  void run(const std::string& name, Function&& function) {
    ++total_;
    try {
      std::forward<Function>(function)();
      std::cout << "ok " << total_ << " - " << name << '\n';
    } catch (const std::exception& error) {
      ++failed_;
      std::cerr << "not ok " << total_ << " - " << name << "\n  "
                << error.what() << '\n';
    } catch (...) {
      ++failed_;
      std::cerr << "not ok " << total_ << " - " << name
                << "\n  unknown exception\n";
    }
  }

  /** Return process status after printing the suite plan. */
  int finish() const {
    std::cout << "1.." << total_ << '\n';
    return failed_ == 0U ? 0 : 1;
  }

private:
  std::size_t total_{0};
  std::size_t failed_{0};
};

/** Temporary target root containing an empty rejected staging tree. */
class test_root {
public:
  test_root() {
    std::string pattern = "/tmp/pkgreconcile-test.XXXXXX";
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back('\0');
    char* result = ::mkdtemp(name.data());
    if (result == nullptr) {
      throw std::system_error(errno, std::generic_category(), "mkdtemp");
    }
    path_ = result;
    fs::create_directories(rejected_root());
  }

  ~test_root() {
    std::error_code ignored;
    fs::remove_all(path_, ignored);
  }

  test_root(const test_root&) = delete;
  test_root& operator=(const test_root&) = delete;

  /** Root of the synthetic installed tree. */
  const fs::path& path() const noexcept {
    return path_;
  }

  /** Rejected staging root below path(). */
  fs::path rejected_root() const {
    return path_ / "var/lib/pkg/rejected";
  }

  /** Installed path for one relative package path. */
  fs::path installed(const fs::path& relative) const {
    return path_ / relative;
  }

  /** Rejected path for one relative package path. */
  fs::path rejected(const fs::path& relative) const {
    return rejected_root() / relative;
  }

private:
  fs::path path_;
};

/** Write exact bytes, creating parent directories first. */
inline void write_file(const fs::path& path, const std::string& contents) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  output.close();
  if (!output) {
    throw std::runtime_error("write failed: " + path.string());
  }
}

/** Read exact bytes from one file. */
inline std::string read_file(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("read failed: " + path.string());
  }
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

/** Read mode bits with stat(2). */
inline mode_t mode(const fs::path& path) {
  struct stat value{};
  if (::stat(path.c_str(), &value) == -1) {
    throw std::system_error(errno, std::generic_category(),
                            "stat: " + path.string());
  }
  return static_cast<mode_t>(value.st_mode & 07777);
}

/** Temporarily redirect one iostream buffer. */
class stream_redirect {
public:
  stream_redirect(std::ios& stream, std::streambuf* replacement)
      : stream_(stream), original_(stream.rdbuf(replacement)) {}

  ~stream_redirect() {
    stream_.rdbuf(original_);
  }

  stream_redirect(const stream_redirect&) = delete;
  stream_redirect& operator=(const stream_redirect&) = delete;

private:
  std::ios& stream_;
  std::streambuf* original_;
};

} // namespace test_support

#define TEST_CHECK(expression)                                                 \
  do {                                                                         \
    if (!(expression)) {                                                       \
      ::test_support::fail(#expression, __FILE__, __LINE__);                   \
    }                                                                          \
  } while (false)

#define TEST_CHECK_EQ(actual, expected)                                        \
  do {                                                                         \
    const auto test_actual_value = (actual);                                   \
    const auto test_expected_value = (expected);                               \
    if (!(test_actual_value == test_expected_value)) {                         \
      ::test_support::fail(                                                    \
          #actual " == " #expected, __FILE__, __LINE__,                        \
          "actual=" + ::test_support::describe(test_actual_value) +            \
              ", expected=" + ::test_support::describe(test_expected_value));  \
    }                                                                          \
  } while (false)

#define TEST_CHECK_THROWS(statement, exception_type)                           \
  do {                                                                         \
    bool test_thrown = false;                                                  \
    try {                                                                      \
      statement;                                                               \
    } catch (const exception_type&) {                                          \
      test_thrown = true;                                                      \
    }                                                                          \
    if (!test_thrown) {                                                        \
      ::test_support::fail(#statement " throws " #exception_type, __FILE__,    \
                           __LINE__);                                          \
    }                                                                          \
  } while (false)

#endif
