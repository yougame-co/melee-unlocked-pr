// Which YouGame site the game uses: production (yougame.co) unless the launcher that owns this game
// chose staging. The launcher keeps that choice in its launcher.ini as the line yougame_env=staging
// (port/app/launcher_yougame.cpp load_line / ini_lines); with no such line, or any other value, it
// is production. The game reads the same file, so the launcher, the game's app channel, its app link
// (yougame:// or yougame-staging://), its Get YouGame page and its site links all follow one setting.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cctype>
#include <filesystem>
#include <fstream>
#include <istream>
#include <string>
#include <vector>

namespace yougame::env {

// launcher.ini text: true only when its yougame_env line says exactly "staging". The last
// yougame_env line wins, as the launcher reads it; a Windows line ending is ignored.
inline bool ini_says_staging(std::istream& in) {
  bool staging = false;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind("yougame_env=", 0) == 0) staging = line.substr(12) == "staging";
  }
  return staging;
}

// The folders a launcher.ini for this game may be in: beside the game, then the launcher's own folder
// two up from Versions\<id> (as main.cpp finds the launcher's replay choice).
inline std::vector<std::filesystem::path> launcher_folders(const std::filesystem::path& exe_dir) {
  std::vector<std::filesystem::path> out{exe_dir};
  const auto name = exe_dir.parent_path().filename().u8string();   // never throws on a non-ASCII folder
  std::string parent(name.begin(), name.end());
  for (char& c : parent) c = (char)std::tolower((unsigned char)c);
  if (parent == "versions") out.push_back(exe_dir.parent_path().parent_path());
  return out;
}

// The first launcher.ini found decides; none (a shortcut, Steam or a test run with no launcher) is production.
inline bool chosen_staging(const std::filesystem::path& exe_dir, std::filesystem::path* found = nullptr) {
  for (const auto& dir : launcher_folders(exe_dir)) {
    std::ifstream ini(dir / "launcher.ini", std::ios::binary);
    if (!ini) continue;
    if (found) *found = dir / "launcher.ini";
    return ini_says_staging(ini);
  }
  return false;
}

}  // namespace yougame::env
