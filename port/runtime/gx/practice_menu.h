// The Practice menu as players see it in Training: one menu (pages of rows bound to the practice CPU
// settings and the search while practicing) drawn in one of four looks, all driven by the
// controller. The full Practice window (pc_settings.cpp) stays for the mouse.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <climits>
#include <string>
#include <vector>

struct ImDrawList;

struct MuPcConfig;   // mu_practice_cpu.h

namespace practice_menu {

// Saved as "practicestyle". kWindow is the full Practice window, for the mouse and keyboard.
enum Style : int { kBars = 0, kRails = 1, kSlots = 2, kStrip = 3, kWindow = 4, kStyleCount = 5 };
const char* style_name(int style);

struct Search {
  bool available = false;   // the search can be used here (Training, event matches)
  bool can_start = false;
  bool searching = false;
  std::string mode;         // "Unranked", "Direct"
  std::string elapsed;      // "0:42"
  std::string state;        // what the matchmaking server is doing
  std::string opponent;
  std::string code;         // the Direct code searched for
  std::string offer;        // after an online session: "... Search Unranked again?" (D-pad Up / Down)
};

// The warm-up room (hle/slippi_warmup.h): Training while staying connected to the opponent, or while
// waiting for a turn in an arena room. Drawn over Training while the menu is shut.
struct Warmup {
  bool active = false;
  bool arena = false;
  bool in_small_window = false;     // this Training is the room's small window: nothing drawn over it
  std::string mode;                 // "Direct", "Teams", "Arena"
  // state: 0 picking, 1 ready, 2 warming up, 3 left; character: locked in with, else picked (-1 not known)
  struct Peer { std::string name; int state = 0; int character = -1; };
  std::vector<Peer> peers;
  int banner = 0;                   // 1 ready, 2 left, 3 warming up too, 4 your turn, 5 back on the select
  float banner_age = 99.0f;         // seconds
  int banner_peer = -1;
  int countdown_ms = -1;            // the arena's "your turn" countdown
  float hold = 0.0f;                // L+R held toward going back (0..1)
  std::string sparring;             // "Zain", "Marth CPU"
  std::string place, versus;        // arena: "Next up", "HERO vs WISE"
  bool score = false;               // arena: the game's score below
  uint32_t frame = 0;
  struct Fighter {
    bool present = false; int character = -1; int stocks = 0; float percent = 0;
    int costume = 0; std::string name;   // the stock icon's costume; the player's name ("" not known)
  } fighter[4];
};

// One of the player's YouGame friends, for Direct's list (yougame::friend_list).
struct Friend {
  std::string username, name;   // name: the display name, "" when not known
  bool online = false, here = false, playing = false;   // here: in this game now
};

struct Frame {
  float display_w = 0, display_h = 0;
  float game_x0 = 0, game_y0 = 0, game_x1 = 0, game_y1 = 0;   // the game picture in the window
  bool native_source = false;   // Source Port: the Training tools run there
  bool open = false;            // the menu is open (closed: only what a look keeps on screen)
  bool armed = false;           // the controller was let go since the menu opened
  uint16_t buttons = 0;         // GameCube buttons of the menu's controller
  int8_t stick_x = 0, stick_y = 0;
  bool keyboard = true;         // arrow keys, Enter, Esc and typing also drive it
  bool hint = false;            // closed: show how to open it
  bool intro = false;           // never opened yet: the banner first, then only the corner hint
  Search search;
  std::vector<Friend> friends;  // Direct's list (in this game first); empty: Direct asks for a code, as before
  Warmup warmup;
  double time = 0;              // seconds, for animation and key repeat
  uint32_t unlocked = 0xFFFFFFFFu;   // characters the memory card has (a bit per character kind)
};

struct Result {
  bool close = false;            // leave the menu
  bool changed = false;          // a saved setting changed (practice CPU, blocked codes)
  bool start_unranked = false;
  bool start_direct = false;
  bool start_teams = false;       // Teams with direct_code (everyone enters the same code)
  bool cancel_search = false;
  bool open_window = false;      // "All settings": the full Practice window
  bool stronger_changed = false; // "Stronger Pro AI" became Always or Never: save it (phillip_ai::stronger_choice)
  bool pro_reaction_changed = false;   // the Pro AI's added reaction changed: save it (phillip_ai::extra_delay)
  bool direct_friend = false;    // start_direct for a friend from the list: direct_code is their YouGame username
  std::string direct_code;
};

// A Fight a pro preset is in use (the Pro AI plays the CPU).
bool pro_active(const MuPcConfig& c);

// Draws the menu (open) or what the look keeps on screen while closed (the search time, the
// strip), and handles the controller and keyboard. Call once per overlay frame inside ImGui.
Result frame(int style, const Frame& f);
void opened();                          // the menu was just opened

// The replay viewer's settings (Start while watching a replay, gx/replay_hud.h), in the same look.
struct ReplaySettings {
  uint8_t length = 3;          // a takeover's length: 0 until the combo drops, 1 ten seconds, 2 twenty, 3 no limit (default)
  uint8_t defense = 0;         // the other players once you change what happened: 0 smart, 1 no DI or tech,
                               // 2 the Practice CPU's settings, 3 follow the recording
  uint8_t tips = 1;            // tip banners and timeline markers
};
ReplaySettings& replay_settings();
// Start while watching opens it: take over the player named (left and right change the player), the
// player's tips in order (A jumps a second before one), and the settings one page down.
struct ReplayTip {
  int32_t frame = 0;
  std::string when, what, value, advice;
};
struct ReplayPanelIn {
  std::string player;          // "P1 Falco"
  std::vector<ReplayTip> tips;
};
struct ReplayPanel {
  bool close = false;          // back to the replay
  bool take_over = false;      // take over `player`
  int player_step = 0;         // -1 or 1: the previous or next player
  int32_t jump = INT32_MIN;    // jump to this replay frame (the panel closes, the replay stays paused)
};
ReplayPanel replay_panel(int style, const Frame& f, const ReplayPanelIn& in);
void show_error(const std::string& s);  // shown in the description box (a bad Direct code)

// Melee's own text: the game's font, decoded from the player's disc at first use (falls back to
// the PC font when the disc has none).
float text_width(float px, const char* s);
// A row of GameCube button glyphs and words, "A:Take over|X:Tip|S:Menu" (A B X Y Z L R, S Start,
// D D-pad), centred on `cy`; returns the width (draw false only measures).
float legend(ImDrawList* dl, float x, float cy, float u, const char* spec, float a = 1.0f, bool draw = true);
// `s` broken into lines no wider than `width` in the game's font at `px`.
std::vector<std::string> wrap_text(const std::string& s, float px, float width);
void text(ImDrawList* dl, float x, float y, float px, uint32_t color, const char* s, float skew = 0.0f,
          bool shadow = true);

// The gold look's pieces (Main Menu bars), for other menus drawn the same way (phob_menu.cpp).
namespace gold {
void bar(ImDrawList* dl, float left, float top, float right, float h, float u, bool lit, const char* label,
         const std::string& value, bool enabled, float a);
void ring(ImDrawList* dl, float cx, float cy, float u, float t, float a);
void title(ImDrawList* dl, float x, float y, float u, const char* title, float a);
void description(ImDrawList* dl, float x, float y, float w, float u, const std::string& s, float a);
void fill_rect(ImDrawList* dl, float x0, float y0, float x1, float y1, uint32_t top, uint32_t bottom, float rounding);
}  // namespace gold
void font_ensure();   // the game's font, loaded from the disc at first use

// Play while searching: the banner while a kept search runs outside its character select, and the
// question on the character select. Drawn over everything else of the game, never in a match of
// that search. Every time below is in milliseconds; the look follows only these numbers (no clock of
// its own), so a picture of one moment is always the same.
struct SearchBanner {
  int stage = 0;               // 0 nothing, 1 searching, 2 opponent found (counting down, then going back)
  std::string service;         // "YouGame", "Slippi", "YouGame and Slippi"
  std::string mode;            // "Unranked", "Ranked", "Friends", "Squad Strike"
  uint32_t elapsed_ms = 0;     // the search's own time (the timer)
  uint32_t shown_ms = 0;       // since the banner came in (slide in)
  uint32_t found_ms = 0;       // since the opponent was found
  int countdown_ms = -1;       // found: until the way back (0 going back, -1 waiting for a safe moment)
  uint32_t clock_ms = 0;       // a running clock for what moves while searching
  float leave = 0.0f;          // 0..1 sliding out
  float hold = 0.0f;           // 0..1 L+R held toward going back to the search
  int look = 0;                // 0 the slim pill (the default), 1 orange letters, 2 a ticker strip, 3 a red ribbon
  int scene_major = -1, scene_minor = -1;   // where it is drawn: the height that keeps it clear of the screen's own parts
};
constexpr int kSearchBannerLooks = 4;
const char* search_banner_name(int look);
// The picture (game_x0..game_y1) inside the window (display_w x display_h).
void draw_search_banner(const SearchBanner& b, float display_w, float display_h, float game_x0, float game_y0,
                        float game_x1, float game_y1);

// A notice in the banner's look (a dark plate, centred on the picture, its top at `top_frac` of the
// picture's height): a title, an optional line under it, and buttons as hints ("U:Search|V:Keep training").
struct Notice {
  std::string title, detail, keys;
  uint32_t shown_ms = 0;   // since it came up (it drops in)
  bool alert = false;      // a red rim (a disconnect)
};
void draw_notice(const Notice& n, float display_w, float display_h, float game_x0, float game_y0, float game_x1,
                 float game_y1, float top_frac);

// The question on the online character select while a search runs. look 1: asked when the player
// starts to leave (B), a small box; 2: a card in the left bar beside the select (warm up, or leave and
// keep the place); 3: one line under the select's own search text. `answer` 0 none yet, 1 keep, 2 cancel.
struct KeepAsk {
  int look = 1;
  bool asking = false;         // look 1: the box is up
  uint32_t shown_ms = 0;
  uint32_t clock_ms = 0;
  float hold = 0.0f;           // L+R toward the warm-up
  int answer = 0;
};
constexpr int kKeepAskLooks = 3;
void draw_keep_ask(const KeepAsk& k, float display_w, float display_h, float game_x0, float game_y0, float game_x1,
                   float game_y1);

}  // namespace practice_menu
