// The twelve named drivers and what they say (see roster.h)
#include "roster.h"
#include "aidriver.h"

#include <algorithm>
#include <cstring>

namespace sim {

const RosterDriver &rosterDriver(int i)
{
    //  name          personality         car colour              helmet                 home  lean
    static const RosterDriver R[ROSTER_SIZE] = {
        {"BLITZ",      PersonaAttacker,    {0.95f, 0.20f, 0.15f}, {0.10f, 0.10f, 0.12f}, 1,  0.04f},
        {"RAMROD",     PersonaAttacker,    {0.55f, 0.10f, 0.10f}, {0.95f, 0.75f, 0.10f}, 3,  0.00f},
        {"BULWARK",    PersonaBlocker,     {0.40f, 0.42f, 0.48f}, {0.95f, 0.95f, 0.95f}, 2,  0.02f},
        {"GATEKEEP",   PersonaBlocker,     {0.15f, 0.35f, 0.25f}, {0.90f, 0.85f, 0.20f}, 4, -0.02f},
        {"SUNDOWN",    PersonaLateCharger, {1.00f, 0.55f, 0.15f}, {0.30f, 0.10f, 0.45f}, 0,  0.03f},
        {"NIGHTOWL",   PersonaLateCharger, {0.12f, 0.12f, 0.35f}, {0.85f, 0.85f, 0.95f}, 3, -0.01f},
        {"STUNTZ",     PersonaShowOff,     {0.95f, 0.30f, 0.80f}, {0.15f, 0.90f, 0.90f}, 5,  0.01f},
        {"LOOPY",      PersonaShowOff,     {0.60f, 0.95f, 0.20f}, {0.95f, 0.30f, 0.70f}, 1, -0.04f},
        {"PROPER PIP", PersonaClean,       {0.92f, 0.92f, 0.95f}, {0.20f, 0.40f, 0.95f}, 2,  0.05f},
        {"SILK",       PersonaClean,       {0.65f, 0.80f, 1.00f}, {0.95f, 0.95f, 0.95f}, 4,  0.00f},
        {"GRUDGE GUS", PersonaGrudge,      {0.45f, 0.30f, 0.15f}, {0.85f, 0.15f, 0.10f}, 0,  0.02f},
        {"WILDCAT",    PersonaWildCard,    {1.00f, 0.85f, 0.10f}, {0.10f, 0.10f, 0.10f}, 5, -0.03f},
    };
    return R[std::clamp(i, 0, ROSTER_SIZE - 1)];
}

int rosterByName(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < ROSTER_SIZE; ++i) if (std::strcmp(rosterDriver(i).name, name) == 0) return i;
    return -1;
}

const char *tauntLine(int rosterId, int moment, int variant)
{
    // [personality][moment][2]
    static const char *L[NUM_PERSONAS][NUM_TAUNT_MOMENTS][2] = {
        {   // attacker
            {"MOVE OVER, I'M COMING THROUGH.", "YOU'RE IN MY WAY ALREADY."},
            {"TOO SLOW! SEE YOU AT THE FINISH!", "BYE BYE!"},
            {"ENJOY IT WHILE IT LASTS.", "I'LL BE RIGHT BACK."},
            {"OH, IT'S LIKE THAT? FINE.", "YOU JUST MADE IT PERSONAL."},
            {"OUT OF MY LANE!", "RUBBING IS RACING!"},
            {"TOO MUCH, TOO FAST.", "THAT'S WHAT HAPPENS."},
            {"THIS ONE'S FOR BEFORE.", "PAYBACK TIME."},
            {"TOO EASY.", "WHO'S NEXT?"},
            {"NEXT TIME YOU'RE MINE.", "LUCKY RUN. NOTHING MORE."}},
        {   // blocker
            {"YOU WON'T GET PAST ME TODAY.", "THIS ROAD IS CLOSED."},
            {"STAY BEHIND, WHERE YOU BELONG.", "THE DOOR IS SHUT."},
            {"HOW DID YOU FIT THROUGH THERE?", "I LEFT ONE GAP. ONE!"},
            {"NO PUSHING ALLOWED.", "I WON'T FORGET THAT."},
            {"MY LINE, MY RULES.", "YOU GOT IN THE WAY."},
            {"I TOLD YOU TO STAY BACK.", "CAREFUL OUT THERE."},
            {"NOW YOU'LL NEVER GET PAST.", "YOU'RE GOING NOWHERE."},
            {"NOBODY GOT PAST. NOBODY.", "A WALL WINS AGAIN."},
            {"YOU FOUND A WAY. THIS TIME.", "I'LL CLOSE THAT GAP."}},
        {   // late charger
            {"WAKE ME UP FOR THE LAST LAP.", "PATIENCE WINS RACES."},
            {"I SAVED A LITTLE FOR THE END.", "AND THAT'S MY CHARGE."},
            {"FINE, I'M NOT EVEN TRYING YET.", "SEE YOU LATER. MUCH LATER."},
            {"CAREFUL, I'M SAVING THIS CAR.", "HEY, I NEED THAT FOR LATER."},
            {"SORRY, WAS IN A HURRY.", "MOVE, IT'S CHARGE TIME."},
            {"SHOULD HAVE WAITED LIKE ME.", "SLOW DOWN, IT'S A LONG RACE."},
            {"YOU'LL SEE ME AT THE END.", "THE LAST LAP IS MINE."},
            {"TIMING IS EVERYTHING.", "TOLD YOU. THE END IS MINE."},
            {"I CHARGED TOO LATE.", "ONE MORE LAP AND YOU'RE TOAST."}},
        {   // show-off
            {"WATCH THE JUMPS, NOT THE ROAD.", "CAMERAS READY? GOOD."},
            {"DID YOU SEE THAT? EVERYONE DID.", "STYLE POINTS FOR ME!"},
            {"WHATEVER, MY JUMP WAS COOLER.", "I WAS POSING."},
            {"HEY! NOT THE PAINT!", "THAT RUINED MY ANGLE."},
            {"THAT WAS A STUNT, OKAY?", "PART OF THE SHOW!"},
            {"NO STYLE, NO LANDING.", "OUCH. ZERO POINTS."},
            {"WATCH THIS ONE, IT'S FOR YOU.", "TIME FOR A SHOW-STOPPER."},
            {"WIN AND A BARREL ROLL.", "THE CROWD LOVES ME."},
            {"YOU WIN. I LOOKED BETTER.", "FINE. MY JUMPS WERE BIGGER."}},
        {   // clean racer
            {"GOOD LUCK. RACE CLEAN.", "LET'S HAVE A FAIR ONE."},
            {"EXCUSE ME, PASSING.", "SORRY, COMING THROUGH."},
            {"NICELY DONE.", "WELL TAKEN."},
            {"PLEASE, RACE CLEAN.", "THAT WAS UNNECESSARY."},
            {"MY APOLOGIES, MY FAULT.", "SORRY, MISJUDGED THAT."},
            {"ARE YOU ALRIGHT?", "TAKE IT EASY THERE."},
            {"NO MORE MISTER NICE.", "THAT'S IT, GLOVES OFF."},
            {"GOOD RACE, EVERYONE.", "CLEAN AND FAST. AS ALWAYS."},
            {"WELL DRIVEN. YOU EARNED IT.", "CONGRATULATIONS."}},
        {   // grudge holder
            {"I REMEMBER YOU.", "WE HAVE UNFINISHED BUSINESS."},
            {"THAT'S ONE BACK.", "HOW DOES IT FEEL?"},
            {"I'LL REMEMBER THIS.", "WRITE IT DOWN. I WILL."},
            {"BIG MISTAKE. HUGE.", "YOU'RE ON MY LIST NOW."},
            {"YOU HAD THAT COMING.", "WE'RE EVEN. ALMOST."},
            {"KARMA.", "THE ROAD REMEMBERS TOO."},
            {"FOR LAST TIME. AND THE TIME BEFORE.", "I NEVER FORGET."},
            {"SWEET, SWEET REVENGE.", "SETTLED."},
            {"THIS ISN'T OVER.", "SEE YOU NEXT RACE. COUNT ON IT."}},
        {   // wild card
            {"WHAT DOES THIS BUTTON DO?", "LET'S MAKE IT INTERESTING."},
            {"WHEEE!", "EVEN I DIDN'T EXPECT THAT."},
            {"WAIT, WHICH WAY IS THE FINISH?", "HA! FUN!"},
            {"BUMPER CARS? I'M IN!", "OH, A GAME? I LIKE GAMES."},
            {"OOPS. OR WAS IT?", "CHAOS!"},
            {"THAT LOOKED FUN FROM HERE.", "DO IT AGAIN!"},
            {"NOW IT'S PERSONAL. PROBABLY.", "HERE COMES TROUBLE."},
            {"I WON? I WON!", "PURE TALENT. OR LUCK."},
            {"WAS THAT A RACE? WHO KNEW.", "AGAIN! AGAIN!"}},
    };
    const int p = std::clamp(rosterDriver(rosterId).persona, 0, NUM_PERSONAS - 1);
    return L[p][std::clamp(moment, 0, NUM_TAUNT_MOMENTS - 1)][variant & 1];
}

const char *cloneTauntLine(int moment, int variant)
{
    static const char *L[NUM_TAUNT_MOMENTS][2] = {
        {"I LEARNED FROM THE BEST. THEN I GOT BETTER.", "I KNOW EVERY MOVE YOU'RE ABOUT TO MAKE."},   // start
        {"SAW THAT COMING. I'D HAVE DONE THE SAME.", "YOUR LINE, MY CAR. SORRY."},                     // passed you
        {"OKAY, THAT ONE I DIDN'T LEARN YET.", "NEW TRICK? NOTED."},                                   // you passed
        {"HITTING YOURSELF? CLASSIC YOU.", "I WOULD HAVE DONE THAT TOO."},                             // you rammed
        {"YOU TAUGHT ME THAT ONE.", "BLAME MY TEACHER."},                                              // rammed you
        {"I DON'T REMEMBER CRASHING THERE...", "THAT'S NOT IN MY RECORDINGS."},                        // you wrecked
        {"YOU KNOW HOW I HOLD A GRUDGE. YOU DO TOO.", "PAYBACK, YOUR STYLE."},                         // revenge
        {"THE STUDENT WINS.", "STUDENT BEATS TEACHER."},                                               // win
        {"YOU'RE STILL THE ORIGINAL. FOR NOW.", "NEXT RACE I'LL KNOW MORE."},                         // lost
    };
    return L[moment < 0 ? 0 : moment >= NUM_TAUNT_MOMENTS ? NUM_TAUNT_MOMENTS - 1 : moment][variant & 1];
}

} // namespace sim
