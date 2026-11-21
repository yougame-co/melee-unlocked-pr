// Which YouGame the game uses, from the launcher's launcher.ini: production unless it says yougame_env=staging.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "yougame_env.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

static bool says(const char* text) {
  std::istringstream in(text);
  return yougame::env::ini_says_staging(in);
}

static void write(const std::filesystem::path& file, const char* text) {
  std::filesystem::create_directories(file.parent_path());
  std::ofstream(file, std::ios::binary | std::ios::trunc) << text;
}

int main(int argc, char** argv) {
  // The text: production by default; only the exact value "staging" is staging.
  CHECK(!says(""));
  CHECK(!says("language=en\nsavereplays=0\n"));
  CHECK(says("yougame_env=staging\n"));
  CHECK(says("iso=C:\\Games\\melee.iso\r\nyougame_env=staging\r\nyougame_app=1\r\n"));   // Windows line endings
  CHECK(says("yougame_env=staging"));                                                    // no final newline
  CHECK(!says("yougame_env=production\n"));
  CHECK(!says("yougame_env=Staging\n"));
  CHECK(!says("yougame_env=staging \n"));
  CHECK(!says("yougame_env=stagingx\n"));
  CHECK(!says("yougame_env=\n"));
  CHECK(!says("# yougame_env=staging\n"));
  CHECK(!says(" yougame_env=staging\n"));
  CHECK(!says("yougame_env=staging\nyougame_env=production\n"));   // the last line wins, as in the launcher
  CHECK(says("yougame_env=production\nyougame_env=staging\n"));

  // The folders: beside the game, and the launcher's folder two up from Versions\<id>.
  namespace fs = std::filesystem;
  const fs::path root = fs::path(argc > 1 ? argv[1] : "ygenv");
  std::error_code ec;
  fs::remove_all(root, ec);
  CHECK(yougame::env::launcher_folders(root / "Game").size() == 1);
  CHECK(yougame::env::launcher_folders(root / "Versions" / "0.9.993").size() == 2);
  CHECK(yougame::env::launcher_folders(root / "versions" / "0.9.993")[1] == root);

  // No launcher.ini (a shortcut or a test run with no launcher): production.
  const fs::path game = root / "Game";
  fs::create_directories(game);
  CHECK(!yougame::env::chosen_staging(game));
  write(game / "launcher.ini", "savereplays=1\n");
  CHECK(!yougame::env::chosen_staging(game));
  write(game / "launcher.ini", "yougame_env=staging\n");
  fs::path found;
  CHECK(yougame::env::chosen_staging(game, &found) && found == game / "launcher.ini");
  write(game / "launcher.ini", "yougame_env=prod\n");
  CHECK(!yougame::env::chosen_staging(game));

  // A release in Versions\<id> follows the launcher's launcher.ini two folders up.
  const fs::path release = root / "Launcher" / "Versions" / "0.9.993";
  fs::create_directories(release);
  CHECK(!yougame::env::chosen_staging(release));
  write(root / "Launcher" / "launcher.ini", "yougame_env=staging\r\n");
  CHECK(yougame::env::chosen_staging(release));
  // One beside the release decides first.
  write(release / "launcher.ini", "language=en\n");
  CHECK(!yougame::env::chosen_staging(release));
  // A launcher.ini two up from a folder not named Versions is not this game's.
  const fs::path other = root / "Other" / "Sub" / "Game";
  fs::create_directories(other);
  write(root / "Other" / "launcher.ini", "yougame_env=staging\n");
  CHECK(!yougame::env::chosen_staging(other));

  fs::remove_all(root, ec);
  std::puts("yougame_env_test: ok");
  return 0;
}
