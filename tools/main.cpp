// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * rejmerge - resolve files rejected during package upgrades
 * See COPYING for license terms and COPYRIGHT for notices.
 */

#include "terminal.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string_view>

#include <getopt.h>
#include <unistd.h>

#ifndef REJMERGE_VERSION
#define REJMERGE_VERSION "unknown"
#endif

namespace {

void usage(std::ostream& output) {
  output << "Usage: rejmerge [-hnv] [-r root-dir] [--color=when]\n"
            "Inspect and resolve staged package files.\n\n"
            "  -r, --root=root-dir  use an alternate root directory\n"
            "  -n, --dry-run        inspect and show differences only\n"
            "      --color=when     color diffs: auto, always, never\n"
            "  -v, --version        print version and exit\n"
            "  -h, --help           print this help and exit\n";
}

std::optional<pkgreconcile::tool::color_mode>
parse_color_mode(std::string_view value) {
  using pkgreconcile::tool::color_mode;
  if (value == "auto") {
    return color_mode::automatic;
  }
  if (value == "always") {
    return color_mode::always;
  }
  if (value == "never") {
    return color_mode::never;
  }
  return std::nullopt;
}

} // namespace

int main(int argc, char** argv) {
  pkgreconcile::tool::run_options options;

  constexpr int color_option = 1000;
  static const option long_options[] = {
      {"root", required_argument, nullptr, 'r'},
      {"dry-run", no_argument, nullptr, 'n'},
      {"color", required_argument, nullptr, color_option},
      {"version", no_argument, nullptr, 'v'},
      {"help", no_argument, nullptr, 'h'},
      {nullptr, 0, nullptr, 0},
  };

  for (;;) {
    const int choice =
        ::getopt_long(argc, argv, "r:nvh", long_options, nullptr);
    if (choice == -1) {
      break;
    }

    switch (choice) {
    case 'r':
      options.filesystem.root = optarg;
      break;
    case 'n':
      options.dry_run = true;
      break;
    case color_option: {
      const auto mode = parse_color_mode(optarg);
      if (!mode.has_value()) {
        std::cerr << "rejmerge: invalid color mode: " << optarg << '\n';
        usage(std::cerr);
        return 1;
      }
      options.color = *mode;
      break;
    }
    case 'v':
      std::cout << "rejmerge " << REJMERGE_VERSION << '\n';
      return 0;
    case 'h':
      usage(std::cout);
      return 0;
    default:
      usage(std::cerr);
      return 1;
    }
  }

  if (optind != argc) {
    std::cerr << "rejmerge: unexpected operand: " << argv[optind] << '\n';
    usage(std::cerr);
    return 1;
  }

  if (!options.dry_run && ::geteuid() != 0) {
    std::cerr << "rejmerge: only root can resolve staged package files\n";
    return 1;
  }

  try {
    pkgreconcile::tool::run_terminal(options);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "rejmerge: " << error.what() << '\n';
    return 1;
  }
}
