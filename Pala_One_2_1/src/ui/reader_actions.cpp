#include "src/ui/reader_actions.h"

#include "src/state.h"  // prefs

namespace Gestures {

static constexpr const char* kKeyLong      = "cfg_btnL";
static constexpr const char* kKeyExtraLong = "cfg_btnXL";
static constexpr const char* kKeyClickHold = "cfg_btnCH";

// Defaults chosen to make the device useful out of the box:
//   long      = bookmark — the most common action while reading
//   extralong = none     — unbound by default
//   clickhold = menu     — easy chord, doesn't fight short-click paging
static ButtonAction s_long      = ACTION_BOOKMARK;
static ButtonAction s_extraLong = ACTION_NONE;
static ButtonAction s_clickHold = ACTION_MENU;

// Map a raw stored/posted int to a valid action. Unknown values — including
// the removed value 2 (old ACTION_LOCK) — fall back to ACTION_NONE.
static ButtonAction clamp(int v) {
  switch (v) {
    case ACTION_NONE:
    case ACTION_BOOKMARK:
    case ACTION_MENU:
      return (ButtonAction)v;
    default:
      return ACTION_NONE;
  }
}

void loadSettings() {
  s_long      = clamp(prefs.getInt(kKeyLong,      ACTION_BOOKMARK));
  s_extraLong = clamp(prefs.getInt(kKeyExtraLong, ACTION_NONE));
  s_clickHold = clamp(prefs.getInt(kKeyClickHold, ACTION_MENU));
}

ButtonAction actionLong()      { return s_long; }
ButtonAction actionExtraLong() { return s_extraLong; }
ButtonAction actionClickHold() { return s_clickHold; }

static void persist(const char* key, ButtonAction& dest, ButtonAction value) {
  ButtonAction v = clamp(value);
  if (v == dest) return;
  dest = v;
  prefs.putInt(key, (int)v);
}

void setActionLong(ButtonAction a)      { persist(kKeyLong,      s_long,      a); }
void setActionExtraLong(ButtonAction a) { persist(kKeyExtraLong, s_extraLong, a); }
void setActionClickHold(ButtonAction a) { persist(kKeyClickHold, s_clickHold, a); }

ButtonAction actionFor(ButtonEvent::Kind kind) {
  switch (kind) {
    case ButtonEvent::Long:      return s_long;
    case ButtonEvent::VeryLong:  return s_extraLong;
    case ButtonEvent::ClickHold: return s_clickHold;
    default:                     return ACTION_NONE;
  }
}

}  // namespace Gestures
