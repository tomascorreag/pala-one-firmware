#include "../../Pala_One_2_1/pala_app.h"
#include "../../Pala_One_2_1/pala_api.h"

__attribute__((section(".header")))
const PalaAppHeader pala_header = {
    .magic        = PALA_APP_MAGIC,
    .api_version  = PALA_API_VERSION,
    .name         = "Click Counter",
    .entry_offset = 0,  // patched to the address of app_main() by the Makefile
    .reloc_offset = 0,  // patched by Makefile
    .reloc_count  = 0,  // patched by Makefile
};

#define LONG_PRESS_MS 850

void app_main(const PalaAPI* api) {
    int count = 0;
    char buf[16];
    uint32_t pressStart = 0;

    // Drain any press still queued from the launcher gesture so the first
    // poll doesn't increment the counter to 1 on entry.
    (void)api->pendingPresses();

    api->clearScreen();
    api->drawHeader("Click Counter");
    api->snprintf_wrap(buf, sizeof(buf), "%d", count);
    api->drawCenteredLarge(buf);
    api->refreshDisplay();

    while (1) {
        // pendingPresses() drives btns.poll() and returns the number of
        // individual short press-release events since the last call.
        // This bypasses the multi-click grouping (no quadClick loss).
        uint32_t presses = api->pendingPresses();

        if (api->buttonPressed()) {
            if (pressStart == 0) pressStart = api->millisNow();
            if ((api->millisNow() - pressStart) >= LONG_PRESS_MS) return;
        } else {
            pressStart = 0;
        }

        if (presses > 0) {
            pressStart = 0;
            count += (int)presses;
            api->clearScreen();
            api->drawHeader("Click Counter");
            api->snprintf_wrap(buf, sizeof(buf), "%d", count);
            api->drawCenteredLarge(buf);
            api->refreshDisplay();
        }

        api->delayMs(1);
    }
}
