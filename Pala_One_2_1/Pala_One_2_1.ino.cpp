# 1 "C:\\Users\\tomco\\AppData\\Local\\Temp\\tmpe7uesbc9"
#include <Arduino.h>
# 1 "C:/Users/tomco/Documents/3D Printing/024 E-Reader/pala-one-firmware-fork/Pala_One_2_1/Pala_One_2_1.ino"
# 35 "C:/Users/tomco/Documents/3D Printing/024 E-Reader/pala-one-firmware-fork/Pala_One_2_1/Pala_One_2_1.ino"
#define LANG_EN 
# 45 "C:/Users/tomco/Documents/3D Printing/024 E-Reader/pala-one-firmware-fork/Pala_One_2_1/Pala_One_2_1.ino"
#define WEB_THEME_LIGHT 







#if defined(BOARD_V1_1)
  #ifndef WIRELESS_PAPER
    #define WIRELESS_PAPER
  #endif
  #ifndef DISPLAY_V1_1
    #define DISPLAY_V1_1
  #endif
#elif defined(BOARD_V1_2)
  #ifndef WIRELESS_PAPER
    #define WIRELESS_PAPER
  #endif
  #ifndef DISPLAY_V1_2
    #define DISPLAY_V1_2
  #endif
#endif

#if !defined(DISPLAY_V1_1) && !defined(DISPLAY_V1_2)
  #error "Board not selected. Arduino IDE: uncomment BOARD_V1_1 or BOARD_V1_2 in Pala_One_2_1.ino. PlatformIO: build with -e wireless-paper-v1_1 or -e wireless-paper-v1_2."
#endif

#include <Arduino.h>
#include <esp_sleep.h>

#include "src/config.h"
#include "src/state.h"
#include "src/hal/battery.h"
#include "src/hal/display.h"
#include "src/hal/input.h"
#include "src/hal/wifi_provisioning.h"
#include "src/pure/hashing.h"
#include "src/storage/app_catalog.h"
#include "src/storage/fs_util.h"
#include "src/storage/library.h"
#include "src/storage/list_items.h"
#include "src/storage/page_cache.h"
#include "src/storage/statistics.h"
#include "src/ui/font.h"
#include "src/ui/pala_api_impl.h"
#include "src/ui/reader.h"
#include "src/ui/reader_actions.h"
#include "src/ui/screen.h"
#include "src/ui/widgets.h"
#include "src/ui/screens/apps_screen.h"
#include "src/ui/screens/bookmarks/book_select_screen.h"
#include "src/ui/screens/bookmarks/bookmark_list_screen.h"
#include "src/ui/screens/bookmarks/preview_screen.h"
#include "src/ui/screens/library_screen.h"
#include "src/ui/screens/list_screen.h"
#include "src/ui/screens/reader_screen.h"
#include "src/ui/screens/statistics_screen.h"
#include "src/ui/screens/upload_screen.h"
#include "src/ui/header_title.h"
#include "src/ui/lock.h"
#include "src/ui/screensavers.h"
#include "src/ui/sleep.h"
#include "src/ui/statusbar.h"
#include "src/ui/text.h"
#include "src/ui/toast.h"
#include "src/web/web.h"




LibraryScreen g_libraryScreen;
ReaderScreen g_readerScreen;
UploadScreen g_uploadScreen;
AppsScreen g_appsScreen;
ListScreen g_listScreen;
StatisticsScreen g_statsScreen;
BookmarkBookSelectScreen g_bmBookSelectScreen;
BookmarkListScreen g_bmListScreen;
BookmarkPreviewScreen g_bmPreviewScreen;

Screen* g_currentScreen = &g_libraryScreen;
void setup();
void loop();
#line 131 "C:/Users/tomco/Documents/3D Printing/024 E-Reader/pala-one-firmware-fork/Pala_One_2_1/Pala_One_2_1.ino"
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("[boot] wake cause: %d\n", esp_sleep_get_wakeup_cause());
  setCpuFrequencyMhz(240);

  pinMode(BTN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BTN), btnISR, CHANGE);






  if (digitalRead(BTN) == LOW) {
    g_btns.seedPressOnWake(millis());
  }

  u8g2.begin(gfx);

#if HAS_BATTERY
  adcSetupOnce();
  pinMode(BAT_ADC_CTRL, INPUT);
  updateBatteryCached(true);
#endif



  prefs.begin("ereader", false);
  Sleep::loadSettings();
  Lock::loadSettings();
# 171 "C:/Users/tomco/Documents/3D Printing/024 E-Reader/pala-one-firmware-fork/Pala_One_2_1/Pala_One_2_1.ino"
  bool wokeFromSleep = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0);
  bool wereReading = (prefs.getString("wake_path", "").length() > 0);
  display.fastmodeOff();
  bool skipClear = wokeFromSleep &&
                   (Lock::isLocked() ||
                    (Sleep::noScreensaver() && wereReading));
  if (!skipClear) {
    display.clear();
  }

  if (!fsBegin()) {
    drawCenter(D_BOOT_STORAGE_ERROR, D_BOOT_TRY_FACTORY_RESET);
    return;
  }
  ensureBooksDir();

  {
    uint64_t chipId = ESP.getEfuseMac();
    snprintf(AP_SSID, sizeof(AP_SSID), "PALA-%06llX", chipId & 0xFFFFFFULL);
  }

  Font::loadSettings();
  Screensavers::loadSettings();
  Statusbar::loadSettings();
  Gestures::loadSettings();
  HeaderTitle::loadSettings();



  loadBooks();
  loadListItems();
  loadApps();
  initPalaAPI();
  registerWebRoutes();
  markUserActivity();



  Statistics::loadOnBoot();







  if (tryRestoreReadingSession()) {
    g_currentScreen = &g_readerScreen;
    if (Lock::isLocked()) {



      markUserActivity();
    } else {
      renderCurrentPage();
      resetInputFrontend();
    }
  } else {
    g_currentScreen = &g_libraryScreen;
    if (Lock::isLocked()) {
      markUserActivity();
    } else {
      g_libraryScreen.onEnter();
      resetInputFrontend();
    }
  }



  setCpuFrequencyMhz(80);





  WifiProvisioning::begin();
}





void loop() {
  g_btns.poll();
  maybeRecoverFromIsrOverflow();

  ButtonEvent ev = ButtonEvent::fromButtonState(g_btns);






  {
    static uint32_t lastSeenPressCount = 0;
    uint32_t pc = g_btns.peekPressCount();
    if (pc < lastSeenPressCount) lastSeenPressCount = pc;
    if (pc != lastSeenPressCount) {
      Statistics::bumpButtons(pc - lastSeenPressCount);
      lastSeenPressCount = pc;
    }
  }
# 285 "C:/Users/tomco/Documents/3D Printing/024 E-Reader/pala-one-firmware-fork/Pala_One_2_1/Pala_One_2_1.ino"
  {
    static bool s_lockedWakePressConsumed = false;

    if (Lock::isLocked()) {
      if (Lock::isUnlockGesture(ev)) {
        s_lockedWakePressConsumed = false;
        Lock::disengage();
        markUserActivity();
        Toast::show(D_TOAST_UNLOCKED);

        display.fastmodeOff();
        g_currentScreen->draw();
        return;
      }
      if (ev.any()) {
        if (!s_lockedWakePressConsumed) {
          s_lockedWakePressConsumed = true;
        } else {
          Sleep::enter();
          return;
        }
      }

      if (ENABLE_DEEP_SLEEP && g_currentScreen->allowSleep() && userIdleMs() > 1500) {
        Sleep::enter();
        return;
      }
      return;
    }
    s_lockedWakePressConsumed = false;
  }

  if (ev.any()) markUserActivity();

  if (ENABLE_DEEP_SLEEP && g_currentScreen->allowSleep() && !WifiProvisioning::isActive()) {
    if (userIdleMs() > Sleep::idleTimeoutMs()) {
      Sleep::enter();
      return;
    }
  }

  WifiProvisioning::loop();

  g_currentScreen->onButton(ev);
  g_currentScreen->onIdleTick();


  if (Toast::clearIfExpired()) g_currentScreen->draw();

  if (g_currentScreen->nextScreen) {
    g_currentScreen = g_currentScreen->nextScreen;
    g_currentScreen->nextScreen = nullptr;
    g_currentScreen->onEnter();
  }
# 357 "C:/Users/tomco/Documents/3D Printing/024 E-Reader/pala-one-firmware-fork/Pala_One_2_1/Pala_One_2_1.ino"
  if (g_currentScreen->allowSleep()
      && !g_btns.hasPendingClicks()
      && !buttonQueueNonEmpty()
      && !WifiProvisioning::isActive()) {
    Sleep::idleLightSleep(Toast::isActive());
  }
}