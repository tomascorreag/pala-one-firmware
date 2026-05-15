#include "../../Pala_One_2_1/pala_app.h"
#include "../../Pala_One_2_1/pala_api.h"

__attribute__((section(".header")))
const PalaAppHeader pala_header = {
    .magic        = PALA_APP_MAGIC,
    .api_version  = PALA_API_VERSION,
    .name         = "Palagotchi",
    .entry_offset = 0,
    .reloc_offset = 0,
    .reloc_count  = 0,
};

#define MAX_STAT           10
#define START_STAT          8
#define BOOST               3
#define HUNGER_DECAY_MS  2880000   /* 8h / 10 points */
#define HAPPY_DECAY_MS   3600000   /* 10h / 10 points */
#define CLEAN_DECAY_MS   4320000   /* 12h / 10 points */
#define ACTION_SHOW_MS     1500
#define LONG_PRESS_MS       850

typedef enum { STATE_HAPPY, STATE_CONTENT, STATE_HUNGRY, STATE_SAD, STATE_DIRTY, STATE_DEAD } PetState;
typedef enum { ACTION_NONE = 0, ACTION_FEED, ACTION_PLAY, ACTION_CLEAN } ActionType;

typedef struct {
    int      hunger;
    int      happiness;
    int      cleanliness;
    uint32_t savedRtcSec;  /* rtcSeconds() at last save — survives deep sleep */
    uint32_t aliveMs;      /* cumulative alive time in ms */
} SavedState;

static int clamp(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static PetState getPetState(int hunger, int happiness, int cleanliness) {
    if (hunger == 0 || happiness == 0 || cleanliness == 0) return STATE_DEAD;
    if (hunger      <= 2) return STATE_HUNGRY;
    if (cleanliness <= 2) return STATE_DIRTY;
    if (happiness   <= 2) return STATE_SAD;
    if (hunger >= 6 && happiness >= 6 && cleanliness >= 6) return STATE_HAPPY;
    return STATE_CONTENT;
}

static void drawStatBar(const PalaAPI* api, int y, const char* label, int val) {
    int i;
    api->drawTextAt(3, y, label, 0);
    api->drawTextAt(50, y, "[", 0);
    for (i = 0; i < val; i++)
        api->drawTextAt(55 + i * 7, y, "#", 0);
    api->drawTextAt(126, y, "]", 0);
}

static void drawScreen(const PalaAPI* api, int hunger, int happiness, int cleanliness,
                       PetState state, uint32_t aliveAtDeath) {
    api->clearScreen();
    api->drawHeader("Palagotchi");

    if (state == STATE_DEAD) {
        char timebuf[24];
        uint32_t secs  = aliveAtDeath / 1000;
        uint32_t mins  = secs / 60;
        uint32_t hours = mins / 60;
        if (hours > 0) {
            api->snprintf_wrap(timebuf, sizeof(timebuf), "Alive: %uh %um",
                               (unsigned)hours, (unsigned)(mins % 60));
        } else if (mins > 0) {
            api->snprintf_wrap(timebuf, sizeof(timebuf), "Alive: %um %us",
                               (unsigned)mins, (unsigned)(secs % 60));
        } else {
            api->snprintf_wrap(timebuf, sizeof(timebuf), "Alive: %us", (unsigned)secs);
        }
        api->drawTextAt(107, 24, "(x.x)", 1);
        api->drawTextAt(107, 36, "/   \\", 1);
        api->drawTextAt(107, 48, "R.I.P", 1);
        api->drawCenteredLarge(timebuf);
        api->drawTextAt(55, 112, "Press to restart", 0);
    } else {
        if (state == STATE_HAPPY) {
            api->drawTextAt(107, 38, "(^.^)", 1);
            api->drawTextAt(107, 52, "(>w<)", 1);
        } else if (state == STATE_HUNGRY) {
            api->drawTextAt(107, 38, "(-w-)", 1);
            api->drawTextAt(107, 52, "(<_>)", 1);
        } else if (state == STATE_DIRTY) {
            api->drawTextAt(107, 38, "(@.@)", 1);
            api->drawTextAt(107, 52, "(~ ~)", 1);
        } else if (state == STATE_SAD) {
            api->drawTextAt(107, 38, "(T.T)", 1);
            api->drawTextAt(107, 52, "( _ )", 1);
        } else {
            api->drawTextAt(107, 38, "(-,-)", 1);
            api->drawTextAt(107, 52, "(> <)", 1);
        }
        api->drawTextAt(107, 64, " \\_/ ", 1);
        drawStatBar(api,  86, "Feed:", hunger);
        drawStatBar(api,  98, "Play:", happiness);
        drawStatBar(api, 110, "Clean:", cleanliness);
        api->drawTextAt(155, 120, "Hold: exit", 0);
    }

    api->refreshDisplay();
}

static void drawActionScreen(const PalaAPI* api, ActionType action) {
    api->clearScreen();
    api->drawHeader("Palagotchi");

    if (action == ACTION_FEED) {
        api->drawTextAt(107, 32, " \\o/ ", 0);
        api->drawTextAt(107, 46, "(^O^)", 1);
        api->drawTextAt(107, 60, " \\_/ ", 1);
        api->drawTextAt(100, 78, "[.=.]", 0);
        api->drawTextAt(75, 102, "Nom nom!", 1);
    } else if (action == ACTION_PLAY) {
        api->drawTextAt(113, 32, "(o)", 0);
        api->drawTextAt(107, 46, "(*^*)", 1);
        api->drawTextAt(107, 60, " /|\\ ", 1);
        api->drawTextAt(82, 102, "Wheee!", 1);
    } else {
        api->drawTextAt(107, 32, "* * *", 0);
        api->drawTextAt(107, 46, "(o_o)", 1);
        api->drawTextAt(107, 60, " ~*~ ", 0);
        api->drawTextAt(76, 102, "Scrub!", 1);
    }

    api->refreshDisplay();
}

void app_main(const PalaAPI* api) {
    int hunger      = START_STAT;
    int happiness   = START_STAT;
    int cleanliness = START_STAT;

    uint32_t sessionStartMs = api->millisNow();
    uint32_t savedAliveMs   = 0;
    uint32_t aliveAtDeath   = 0;

    /* Restore persisted state */
    SavedState saved;
    if (api->storageRead("palagotchi", &saved, sizeof(saved)) == (int)sizeof(saved)) {
        if (saved.hunger == 0 || saved.happiness == 0 || saved.cleanliness == 0) {
            /* Pet was already dead in the save file */
            hunger = 0; happiness = 0; cleanliness = 0;
            aliveAtDeath = saved.aliveMs;
            savedAliveMs = saved.aliveMs;
        } else {
            uint32_t rtcNow = api->rtcSeconds();
            if (rtcNow >= saved.savedRtcSec) {
                uint32_t elapsed = (rtcNow - saved.savedRtcSec) * 1000;
                hunger      = clamp(saved.hunger      - (int)(elapsed / HUNGER_DECAY_MS), 0, MAX_STAT);
                happiness   = clamp(saved.happiness   - (int)(elapsed / HAPPY_DECAY_MS),  0, MAX_STAT);
                cleanliness = clamp(saved.cleanliness - (int)(elapsed / CLEAN_DECAY_MS),  0, MAX_STAT);
                if (hunger == 0 || happiness == 0 || cleanliness == 0) {
                    /* Died while app was closed — find when the first stat hit 0 */
                    uint32_t deathMs = (uint32_t)saved.hunger * HUNGER_DECAY_MS;
                    uint32_t t;
                    t = (uint32_t)saved.happiness * HAPPY_DECAY_MS;
                    if (t < deathMs) deathMs = t;
                    t = (uint32_t)saved.cleanliness * CLEAN_DECAY_MS;
                    if (t < deathMs) deathMs = t;
                    aliveAtDeath = saved.aliveMs + deathMs;
                    savedAliveMs = aliveAtDeath;
                } else {
                    /* Still alive — count closed time toward the alive total */
                    savedAliveMs = saved.aliveMs + elapsed;
                }
            } else {
                hunger      = saved.hunger;
                happiness   = saved.happiness;
                cleanliness = saved.cleanliness;
                savedAliveMs = saved.aliveMs;
            }
        }
    }

    // Drain any event still queued from the launcher gesture so the first
    // pollEvent() doesn't fire a phantom feed/play/clean on entry.
    while (api->pollEvent() != 0) { }
    (void)api->pendingPresses();

    uint32_t lastHungerDecay = api->millisNow();
    uint32_t lastHappyDecay  = lastHungerDecay;
    uint32_t lastCleanDecay  = lastHungerDecay;

    PetState   state         = getPetState(hunger, happiness, cleanliness);
    PetState   prevState     = (PetState)-1;
    ActionType actionType    = ACTION_NONE;
    uint32_t   actionStartMs = 0;
    uint32_t   pressStart    = 0;
    int needsRedraw = 1;

    while (1) {
        uint32_t now = api->millisNow();

        /* Long press: fires while button is held, not on release */
        if (api->buttonPressed()) {
            if (pressStart == 0) pressStart = now;
            if ((now - pressStart) >= LONG_PRESS_MS) {
                SavedState s = { hunger, happiness, cleanliness, api->rtcSeconds(),
                                 savedAliveMs + (now - sessionStartMs) };
                api->storageWrite("palagotchi", &s, sizeof(s));
                return;
            }
        } else {
            pressStart = 0;
        }

        uint8_t evt = api->pollEvent();

        if (state != STATE_DEAD) {
            if (evt == PALA_CLICK) {
                hunger = clamp(hunger + BOOST, 0, MAX_STAT);
                actionType = ACTION_FEED;
                actionStartMs = now;
                drawActionScreen(api, ACTION_FEED);
            } else if (evt == PALA_DOUBLE) {
                happiness = clamp(happiness + BOOST, 0, MAX_STAT);
                actionType = ACTION_PLAY;
                actionStartMs = now;
                drawActionScreen(api, ACTION_PLAY);
            } else if (evt == PALA_TRIPLE) {
                cleanliness = clamp(cleanliness + BOOST, 0, MAX_STAT);
                actionType = ACTION_CLEAN;
                actionStartMs = now;
                drawActionScreen(api, ACTION_CLEAN);
            }

            /* Stat decay */
            if (now - lastHungerDecay >= HUNGER_DECAY_MS) {
                lastHungerDecay = now;
                if (hunger > 0) { hunger--; needsRedraw = 1; }
            }
            if (now - lastHappyDecay >= HAPPY_DECAY_MS) {
                lastHappyDecay = now;
                if (happiness > 0) { happiness--; needsRedraw = 1; }
            }
            if (now - lastCleanDecay >= CLEAN_DECAY_MS) {
                lastCleanDecay = now;
                if (cleanliness > 0) { cleanliness--; needsRedraw = 1; }
            }
        } else {
            /* Death screen: any click resets the pet (long press exits via buttonPressed above) */
            if (evt != 0 && evt != PALA_LONG) {
                hunger      = START_STAT;
                happiness   = START_STAT;
                cleanliness = START_STAT;
                savedAliveMs   = 0;
                aliveAtDeath   = 0;
                sessionStartMs = now;
                lastHungerDecay = now;
                lastHappyDecay  = now;
                lastCleanDecay  = now;
                prevState   = (PetState)-1;
                actionType  = ACTION_NONE;
                needsRedraw = 1;
                /* Write fresh save so a reboot starts clean too */
                SavedState s = { START_STAT, START_STAT, START_STAT, api->rtcSeconds(), 0 };
                api->storageWrite("palagotchi", &s, sizeof(s));
            }
        }

        /* Return to normal screen after action display timeout */
        if (actionType != ACTION_NONE && (now - actionStartMs) >= ACTION_SHOW_MS) {
            actionType = ACTION_NONE;
            needsRedraw = 1;
        }

        /* State transition — capture alive time on death */
        PetState newState = getPetState(hunger, happiness, cleanliness);
        if (newState == STATE_DEAD && state != STATE_DEAD) {
            aliveAtDeath = savedAliveMs + (now - sessionStartMs);
            /* Save immediately so reopening the app shows the death screen */
            SavedState s = { 0, 0, 0, api->rtcSeconds(), aliveAtDeath };
            api->storageWrite("palagotchi", &s, sizeof(s));
        }
        if (newState != prevState) needsRedraw = 1;
        state = newState;

        if (needsRedraw && actionType == ACTION_NONE) {
            drawScreen(api, hunger, happiness, cleanliness, state, aliveAtDeath);
            prevState   = state;
            needsRedraw = 0;
        }

        api->delayMs(10);
    }
}
