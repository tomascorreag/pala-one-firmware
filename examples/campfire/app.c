#include "../../Pala_One_2_1/pala_app.h"
#include "../../Pala_One_2_1/pala_api.h"
#include "campfire_frames.h"

__attribute__((section(".header")))
const PalaAppHeader pala_header = {
    .magic        = PALA_APP_MAGIC,
    .api_version  = PALA_API_VERSION,
    .name         = "Campfire",
    .entry_offset = 0,
    .reloc_offset = 0,
    .reloc_count  = 0,
};

// At ~0.5 fps clearScreen's internal full refresh (every 60 frames) lands every
// ~120 s, near the panel's recommended >=180 s spacing. MAX_RUNTIME_MS caps
// total cycle exposure per session.
#define FRAME_INTERVAL 2000
#define MAX_RUNTIME_MS (5UL * 60UL * 1000UL)

void app_main(const PalaAPI* api) {
    uint8_t  f         = 0;
    uint32_t app_start = api->millisNow();

    // Drop the launcher-gesture press so we don't self-exit on the first poll.
    (void)api->pendingPresses();

    while (1) {
        if ((api->millisNow() - app_start) >= MAX_RUNTIME_MS) return;

        // Capture t0 before the draw so FRAME_INTERVAL is a minimum period,
        // not an additive post-refresh delay.
        uint32_t t0 = api->millisNow();

        api->clearScreen();
        api->drawXBitmap(0, 0, campfire_frames[f], CAMPFIRE_FRAME_W, CAMPFIRE_FRAME_H, 1);
        api->refreshDisplay();

        if (++f >= CAMPFIRE_FRAME_COUNT) f = 0;

        // Any input exits. pendingPresses() also catches a press-release that
        // completed entirely during the ~900ms blocking refresh.
        do {
            if (api->buttonPressed() || api->pendingPresses() > 0) return;
            api->delayMs(5);
        } while ((api->millisNow() - t0) < FRAME_INTERVAL);
    }
}
