#include "theme.hpp"

#include <cctype>
#include <cmath>
#include <cstring>

namespace {

// As paletas embutidas. Os valores sao os oficiais de cada tema.
const Theme kThemes[] = {
    // -------------------------------------------------------------------
    // Usa as cores padrao do terminal no fundo (fica transparente se o seu
    // terminal tiver transparencia). E o visual original do ted.
    // -------------------------------------------------------------------
    {"default",
     /* bg */ kDefaultColor, /* fg */ kDefaultColor,
     /* bg_alt */ 0x1c1c1c, /* bg_sel */ 0x5f5f87, /* fg_dim */ 0x6c6c6c,
     /* accent */ 0x005f87, /* accent_fg */ 0xffffff, /* accent2 */ 0x87afff,
     /* keyword */ 0xff5faf, /* type */ 0x5fd7d7, /* string */ 0xafd787,
     /* comment */ 0x808080, /* number */ 0xffaf87, /* preproc */ 0xd7af5f,
     /* punct */ 0xd0d0d0,
     /* modified */ 0xffaf5f, /* search_bg */ 0xffd700, /* search_fg */ 0x080808},

    // ------------------------------- Rosé Pine -------------------------
    {"rose-pine",
     0x191724, 0xe0def4, 0x1f1d2e, 0x403d52, 0x6e6a86,
     0x31748f, 0xe0def4, 0x9ccfd8,
     /* iris */ 0xc4a7e7, /* foam */ 0x9ccfd8, /* gold */ 0xf6c177,
     /* muted */ 0x6e6a86, /* rose */ 0xebbcba, /* love */ 0xeb6f92,
     /* subtle */ 0x908caa,
     0xf6c177, 0xf6c177, 0x191724},

    // Variante clara (boa para projetor em sala de aula).
    {"rose-pine-dawn",
     0xfaf4ed, 0x575279, 0xf2e9e1, 0xdfdad9, 0x9893a5,
     0x286983, 0xfaf4ed, 0x56949f,
     0x907aa9, 0x56949f, 0xea9d34,
     0x9893a5, 0xd7827e, 0xb4637a,
     0x797593,
     0xea9d34, 0xea9d34, 0xfaf4ed},

    // -------------------------------- Dracula --------------------------
    {"dracula",
     0x282a36, 0xf8f8f2, 0x21222c, 0x44475a, 0x6272a4,
     0xbd93f9, 0x282a36, 0x8be9fd,
     /* pink */ 0xff79c6, /* cyan */ 0x8be9fd, /* yellow */ 0xf1fa8c,
     /* comment */ 0x6272a4, /* purple */ 0xbd93f9, /* green */ 0x50fa7b,
     0xf8f8f2,
     0xffb86c, 0xf1fa8c, 0x282a36},

    // -------------------------------- Gruvbox --------------------------
    {"gruvbox",
     0x282828, 0xebdbb2, 0x1d2021, 0x504945, 0x928374,
     0x458588, 0xfbf1c7, 0x83a598,
     /* red */ 0xfb4934, /* yellow */ 0xfabd2f, /* green */ 0xb8bb26,
     /* gray */ 0x928374, /* purple */ 0xd3869b, /* aqua */ 0x8ec07c,
     0xebdbb2,
     0xfe8019, 0xfabd2f, 0x282828},

    // ---------------------------------- Nord ---------------------------
    {"nord",
     0x2e3440, 0xd8dee9, 0x3b4252, 0x434c5e, 0x616e88,
     0x5e81ac, 0xeceff4, 0x88c0d0,
     /* nord9  */ 0x81a1c1, /* nord7 */ 0x8fbcbb, /* nord14 */ 0xa3be8c,
     /* comment*/ 0x616e88, /* nord15*/ 0xb48ead, /* nord13 */ 0xebcb8b,
     0xd8dee9,
     0xd08770, 0xebcb8b, 0x2e3440},
};

constexpr size_t kThemeCount = sizeof(kThemes) / sizeof(kThemes[0]);

// "Rose Pine", "rose_pine" e "rosepine" viram todos "rose-pine".
std::string normalize(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '_' || c == ' ') o += '-';
    else o += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return o;
}

}  // namespace

const Theme& theme_by_name(const std::string& name) {
  std::string want = normalize(name);
  for (const Theme& t : kThemes)
    if (want == t.name) return t;
  return kThemes[0];
}

bool theme_exists(const std::string& name) {
  std::string want = normalize(name);
  for (const Theme& t : kThemes)
    if (want == t.name) return true;
  return false;
}

std::vector<std::string> theme_names() {
  std::vector<std::string> out;
  out.reserve(kThemeCount);
  for (const Theme& t : kThemes) out.push_back(t.name);
  return out;
}

// ---------------------------------------------------------------------------
// RGB -> paleta de 256 cores
// ---------------------------------------------------------------------------
//
// A paleta xterm tem, alem das 16 cores basicas, um cubo 6x6x6 (indices
// 16..231) e uma rampa de 24 cinzas (232..255). Testamos o candidato do cubo
// e o candidato da rampa e ficamos com o mais proximo.

int rgb_to_256(int r, int g, int b) {
  auto clamp = [](int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); };
  r = clamp(r); g = clamp(g); b = clamp(b);

  static const int kLevels[6] = {0, 95, 135, 175, 215, 255};
  auto nearest_level = [](int v) {
    int best = 0, best_d = 1000;
    for (int i = 0; i < 6; i++) {
      int d = std::abs(v - kLevels[i]);
      if (d < best_d) { best_d = d; best = i; }
    }
    return best;
  };
  auto dist2 = [](int r1, int g1, int b1, int r2, int g2, int b2) {
    int dr = r1 - r2, dg = g1 - g2, db = b1 - b2;
    return dr * dr + dg * dg + db * db;
  };

  const int ri = nearest_level(r), gi = nearest_level(g), bi = nearest_level(b);
  const int cube = 16 + 36 * ri + 6 * gi + bi;
  const int cube_d = dist2(r, g, b, kLevels[ri], kLevels[gi], kLevels[bi]);

  // Rampa de cinzas: 8, 18, 28, ... 238.
  int gi_gray = (r * 30 + g * 59 + b * 11) / 100;   // luminancia aproximada
  int idx = (gi_gray - 8 + 5) / 10;
  if (idx < 0) idx = 0;
  if (idx > 23) idx = 23;
  const int gray_value = 8 + 10 * idx;
  const int gray = 232 + idx;
  const int gray_d = dist2(r, g, b, gray_value, gray_value, gray_value);

  return (gray_d < cube_d) ? gray : cube;
}

Color blend(Color a, Color b, int percent) {
  if (a == kDefaultColor || b == kDefaultColor) return a;
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  auto mix = [&](int shift) {
    int ca = (a >> shift) & 0xFF, cb = (b >> shift) & 0xFF;
    return (ca * (100 - percent) + cb * percent) / 100;
  };
  return (mix(16) << 16) | (mix(8) << 8) | mix(0);
}

int shade_apart(Color want, Color toward, int avoid) {
  int idx = rgb_to_256(want);
  if (idx != avoid || toward == kDefaultColor || want == kDefaultColor)
    return idx;
  for (int percent = 15; percent <= 60; percent += 15) {
    int candidate = rgb_to_256(blend(want, toward, percent));
    if (candidate != avoid) return candidate;
  }
  return idx;
}
