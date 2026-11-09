// Story mode ("STORY MODE" in the menu): THE GOLDEN WHEEL. A prologue and five chapters, one
// per map, each against a rival of the Brick Barons crew, and an ending. Before and after
// every race a scene plays in the game's own 3D world: the block-figure characters stand in
// front of their cars at the start grid and talk (the camera cuts to whoever speaks); the
// prologue also shows the temple, the road and a live crash. Progress is saved on this PC.
#include "glwidget.h"
#include "audioengine.h"
#include "raceclient.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#ifdef __APPLE__
#include <OpenGL/glu.h>
#else
#include <GL/glu.h>
#endif

#include <QKeyEvent>
#include <QSettings>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include "gl1redirect.h"   // (the drawing goes through gl1shim: OpenGL ES 2 / WebGL)

using sim::PI;

namespace {

// ---------------------------------------------------------------------------
//  The cast
// ---------------------------------------------------------------------------
enum Who { NARRATOR, GRANNY, PLAYER, DAN, VEX, NOVA, KAI, BARON, NUM_WHO };

struct CastMember {
    const char *name;
    float tag[3];                               // name tag in the dialogue box
    float suit[3], trim[3], helmet[3], skin[3], hair[3];
    bool  helmetOn;
    int   face;
    float car[3];                               // their car (rivals)
};

const CastMember CAST[NUM_WHO] = {
    {"", {0.80f, 0.85f, 0.95f}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, false, 0, {0, 0, 0}},
    {"GRANNY GEARS", {0.80f, 0.60f, 1.00f}, {0.45f, 0.25f, 0.65f}, {1.00f, 0.85f, 0.30f}, {0.9f, 0.9f, 0.9f},
     {0.98f, 0.86f, 0.72f}, {0.90f, 0.90f, 0.92f}, false, 1, {0.45f, 0.25f, 0.65f}},
    {"YOU", {1.00f, 0.80f, 0.20f}, {0.95f, 0.50f, 0.10f}, {1.0f, 1.0f, 1.0f}, {1.00f, 0.78f, 0.15f},
     {1.00f, 0.80f, 0.25f}, {0.30f, 0.18f, 0.08f}, true, 0, {0.95f, 0.50f, 0.10f}},
    {"DUSTY DAN", {0.55f, 0.90f, 0.35f}, {0.25f, 0.55f, 0.20f}, {0.95f, 0.85f, 0.45f}, {0.9f, 0.8f, 0.4f},
     {1.00f, 0.80f, 0.25f}, {0.95f, 0.78f, 0.30f}, false, 1, {0.30f, 0.70f, 0.25f}},
    {"VIPER VEX", {1.00f, 0.38f, 0.32f}, {0.70f, 0.10f, 0.10f}, {0.10f, 0.10f, 0.10f}, {0.12f, 0.12f, 0.12f},
     {0.62f, 0.42f, 0.28f}, {0.10f, 0.10f, 0.10f}, true, 2, {0.78f, 0.10f, 0.10f}},
    {"FROST NOVA", {0.55f, 0.90f, 1.00f}, {0.85f, 0.92f, 1.00f}, {0.20f, 0.70f, 1.00f}, {0.30f, 0.80f, 1.00f},
     {0.93f, 0.72f, 0.55f}, {0.96f, 0.96f, 1.00f}, false, 0, {0.85f, 0.95f, 1.00f}},
    {"KAI VOLT", {0.90f, 0.45f, 1.00f}, {0.55f, 0.20f, 0.80f}, {0.20f, 1.00f, 0.90f}, {0.20f, 1.00f, 0.90f},
     {1.00f, 0.80f, 0.25f}, {0.15f, 0.90f, 0.85f}, false, 1, {0.60f, 0.20f, 0.85f}},
    {"THE IRON BARON", {1.00f, 0.80f, 0.25f}, {0.12f, 0.12f, 0.14f}, {1.00f, 0.80f, 0.25f}, {0.14f, 0.14f, 0.16f},
     {0.98f, 0.86f, 0.72f}, {0.25f, 0.25f, 0.25f}, true, 2, {0.15f, 0.15f, 0.17f}},
};

// ---------------------------------------------------------------------------
//  The story
// ---------------------------------------------------------------------------
// Where the camera looks while a line is said
enum Shot { ON_SPEAKER, TEMPLE, FLYOVER, GRID, LIVE_CRASH };

struct Line { int who; const char *text; int shot; };

struct Chapter {
    int map;                // which map (-1: the scene uses chapter 1's)
    int rival;              // a Who
    const char *title;
    const char *goal;       // shown in the chapter list and at the start of the race
    int difficulty;         // CPU level band: 0 easy, 1 medium, 2 hard
    std::vector<Line> intro, won, lost;
};

const std::vector<Chapter> &chapters()
{
    static const std::vector<Chapter> all = {
        {0, BARON, "THE FIVE ROADS", "", 0,
         {{NARRATOR, "LONG AGO, FIVE ROADS WERE BUILT TO THE OLD TEMPLE.", TEMPLE},
          {NARRATOR, "EVERY SUMMER THE FASTEST DRIVER LIFTS ITS GOLDEN WHEEL.", FLYOVER},
          {NARRATOR, "BUT THIS YEAR THE BRICK BARONS TOOK EVERY ROAD...", GRID},
          {NARRATOR, "...AND THEY RAM ANYONE WHO TRIES TO GET PAST.", LIVE_CRASH},
          {GRANNY, "MY OLD CAR STILL HAS ONE RACE IN IT, KID. MAYBE FIVE!", ON_SPEAKER},
          {GRANNY, "WIN ALL FIVE ROADS. AND DRIVE CLEAN - RAMMING HURTS YOU MORE THAN THEM.", ON_SPEAKER},
          {BARON, "A ROOKIE IN GRANNY'S OLD CAR? SEE YOU IN THE DUST!", ON_SPEAKER},
          {PLAYER, "WE'LL SEE ABOUT THAT.", ON_SPEAKER}},
         {}, {}},
        {0, DAN, "THE VALLEY", "FINISH IN THE TOP 3", 0,
         {{DAN, "THIS VALLEY IS MINE, ROOKIE. I KNOW EVERY BUMP!", ON_SPEAKER},
          {GRANNY, "FINISH IN THE TOP THREE AND HE HAS TO LET YOU THROUGH.", ON_SPEAKER},
          {GRANNY, "THE CREST JUMP COMES FAST. LINE UP IN THE MIDDLE.", ON_SPEAKER},
          {PLAYER, "EASY. JUST ONE LONG STRAIGHT ROAD.", ON_SPEAKER}},
         {{DAN, "AW SHUCKS. THE BARON WON'T LIKE THIS ONE BIT...", ON_SPEAKER},
          {GRANNY, "ONE ROAD OPEN. FOUR TO GO!", ON_SPEAKER}},
         {{DAN, "BACK TO THE FARM WITH YOU, ROOKIE!", ON_SPEAKER},
          {GRANNY, "DON'T GIVE UP. TRY AGAIN!", ON_SPEAKER}}},
        {1, VEX, "THE CANYON", "BEAT VIPER VEX", 1,
         {{VEX, "MY CANYON BITES. THE SLOT CANYON IS FOR SNAKES ONLY.", ON_SPEAKER},
          {GRANNY, "BEAT VEX TO THE LINE. THE DIRT SHORTCUT SAVES A SECOND - IF YOU DARE.", ON_SPEAKER},
          {PLAYER, "I'M NOT AFRAID OF A LITTLE DIRT.", ON_SPEAKER}},
         {{VEX, "HSSS... LUCKY ROOKIE.", ON_SPEAKER},
          {GRANNY, "TWO ROADS OPEN. THE MOUNTAIN IS NEXT.", ON_SPEAKER}},
         {{VEX, "TOO SLOW FOR MY CANYON!", ON_SPEAKER},
          {GRANNY, "SHE'S FAST, NOT UNBEATABLE. AGAIN!", ON_SPEAKER}}},
        {2, NOVA, "THE MOUNTAIN", "WIN THE RACE", 1,
         {{NOVA, "UP HERE THE CORNERS ARE ICE. AND THEY ARE ALL MINE.", ON_SPEAKER},
          {GRANNY, "WIN IT. BRAKE BEFORE THE SWITCHBACKS, NOT IN THEM.", ON_SPEAKER},
          {PLAYER, "COOL.", ON_SPEAKER}},
         {{NOVA, "...RESPECT. THE BARON IS WAITING IN THE CITY.", ON_SPEAKER},
          {GRANNY, "THREE DOWN! I KNEW IT, KID.", ON_SPEAKER}},
         {{NOVA, "THE MOUNTAIN SAYS NO.", ON_SPEAKER},
          {GRANNY, "SMOOTH IS FAST. TRY AGAIN.", ON_SPEAKER}}},
        {3, KAI, "THE CITY", "WIN WITHOUT BEING WRECKED", 2,
         {{KAI, "CITY RULES: NO BRAKES, NO MERCY!", ON_SPEAKER},
          {GRANNY, "WIN, AND DON'T GET WRECKED. KAI LOVES A BUMP - DON'T BUMP BACK.", ON_SPEAKER},
          {PLAYER, "CLEAN AND FAST. GOT IT.", ON_SPEAKER}},
         {{KAI, "CLEAN AND FAST? THAT'S NEW IN THIS TOWN.", ON_SPEAKER},
          {GRANNY, "ONLY THE COAST IS LEFT. AND THE BARON HIMSELF.", ON_SPEAKER}},
         {{KAI, "THE CITY CHEWED YOU UP!", ON_SPEAKER},
          {GRANNY, "STAY AWAY FROM HIS BUMPER. AGAIN!", ON_SPEAKER}}},
        {4, BARON, "THE COAST", "WIN THE RACE", 2,
         {{BARON, "FIVE ROADS. ONE GOLDEN WHEEL. AND IT IS MINE!", ON_SPEAKER},
          {GRANNY, "THIS IS IT, KID. WIN THE COAST AND THE TEMPLE IS YOURS.", ON_SPEAKER},
          {PLAYER, "LET'S RACE.", ON_SPEAKER}},
         {{BARON, "NO! NOT MY WHEEL!", ON_SPEAKER}},
         {{BARON, "THE WHEEL STAYS WITH THE BRICK BARONS!", ON_SPEAKER},
          {GRANNY, "SO CLOSE. ONE MORE TIME!", ON_SPEAKER}}},
        {0, BARON, "THE GOLDEN WHEEL", "", 0,
         {{GRANNY, "YOU DID IT! THE GOLDEN WHEEL COMES HOME.", ON_SPEAKER},
          {BARON, "...FINE. YOU RACE CLEANER THAN ANY OF MY CREW.", ON_SPEAKER},
          {NARRATOR, "THE FIVE ROADS ARE OPEN AGAIN. ANYONE CAN RACE TO THE TEMPLE.", TEMPLE},
          {NARRATOR, "THE END. THANKS FOR PLAYING!", FLYOVER}},
         {}, {}},
        {5, BARON, "THE SERPENT", "WIN THE RACE", 2,
         {{BARON, "WELCOME TO MY SECRET ROAD. TWO BRIDGES, ONE SPIRAL, RAMPS EVERYWHERE!", ON_SPEAKER},
          {GRANNY, "THE SERPENT EATS CARELESS DRIVERS. LAND STRAIGHT AND BRAKE YOUR ROLLS.", ON_SPEAKER},
          {GRANNY, "AND DON'T HOLD THAT L BUTTON TOO LONG - THE ENGINE WILL BURN OUT.", ON_SPEAKER},
          {PLAYER, "LET'S TAME THE SERPENT.", ON_SPEAKER}},
         {{BARON, "MY SERPENT... BEATEN BY GRANNY'S OLD CAR?!", ON_SPEAKER},
          {GRANNY, "THAT'S MY KID!", ON_SPEAKER}},
         {{BARON, "THE SERPENT ALWAYS WINS!", ON_SPEAKER},
          {GRANNY, "MIND THE RIVER NEXT TIME. AGAIN!", ON_SPEAKER}}},
    };
    return all;
}

const int ENDING = 6;

// the rival's car in a story race: car 1, the second car on the grid
const int RIVAL_CAR = 1;

} // namespace

namespace {
// The rules, a card before each race (and [R] in the chapter list): what costs health and
// how much, what a wreck means, the jumps, the L boost - the game's real numbers
struct RulesCard { const char *title; std::vector<const char *> lines; };
const RulesCard &rulesCard(int chapter)
{
    static const std::vector<RulesCard> CARDS = {
        {"HOW CRASHES WORK", {
            "EVERY CAR HAS 100 HEALTH. YOURS IS IN THE CORNER, THE OTHERS' OVER THEIR PLATES.",
            "4 SECONDS WITHOUT A HIT AND IT MENDS, 3 HEALTH A SECOND.",
            "RAM ANOTHER CAR: YOU TAKE 85% OF THE DAMAGE, THEY TAKE 15%.",
            "THE LEADER TAKES 25% MORE CRASH DAMAGE, THE LAST CAR 20% LESS.",
            "A BARRIER HEAD-ON: UP TO 30. TRAFFIC: UP TO 25. A RAIL SCRAPE: 1-4. A BARREL: 6.",
            "AT 0 HEALTH YOU ARE WRECKED: 2-3 SECONDS LOST, BACK WITH 100.",
            "A WRECK COSTS YOU TIME, NOT THE RACE: YOU ARE ALWAYS BACK IN 3, 2, 1.",
            "THE CAR THAT DID A WRECKED CAR THE MOST DAMAGE GETS A +15 BOUNTY."}},
        {"SIDE RAMPS AND LANDINGS", {
            "PINK RAMPS AT THE ROAD'S EDGE THROW YOU HIGH AND FAR.",
            "LAND POINTING ALONG THE ROAD: ONLY 1 DAMAGE.",
            "LAND CROOKED: UP TO 9 MORE (14 ON A RED RAMP). STEER IN THE AIR TO LINE UP.",
            "HIT A RAMP AT AN ANGLE AND THE CAR SPINS: STEER AGAINST THE SPIN.",
            "FLY OVER THE GUARD RAIL: OFF THE ROAD 12, IN THE RIVER 18, DOWN A GORGE 24.",
            "DRIVE INTO A RAMP'S SIDE: DAMAGE AND A HARD STOP. TAKE IT HEAD ON."}},
        {"TWISTED RAMPS", {
            "PURPLE AND ORANGE RAMPS ROLL THE CAR OVER IN THE AIR.",
            "THE ROLL GOES A LITTLE TOO FAR: BRAKE (DOWN) IN THE AIR TO SLOW IT.",
            "LAND UPRIGHT: BARREL ROLL! - AND YOUR NITRO IS FULL.",
            "LAND HALF A ROLL WRONG: UP TO 15 DAMAGE (22 ON A RED RAMP). ON THE ROOF: A WRECK.",
            "TOO SLOW AT THE RAMP: THE CAR ONLY TIPS OVER - AND LANDS ON ITS SIDE."}},
        {"THE L BOOST", {
            "HOLD L TO BOOST: 10% MORE TOP SPEED FOR AS LONG AS YOU HOLD IT.",
            "THE FIRST 3 SECONDS COST 1.5 HEALTH A SECOND.",
            "FROM 3 TO 10 SECONDS: 4 HEALTH A SECOND.",
            "10 SECONDS OR MORE: 25 A SECOND - THE ENGINE BURNS OUT AND YOU ARE WRECKED.",
            "LET GO AND THE HEAT COOLS TWICE AS FAST. NO MENDING WHILE BOOSTING.",
            "NO L BOOST IN THE 2 PLAYER SPLIT SCREEN."}},
        {"THE LAST THIRD", {
            "THE LAST THIRD OF EVERY ROAD HAS MORE OBSTACLES AND MORE RAMPS.",
            "A RED SKULL OVER A MINE: 0 HEALTH AT ONCE. DRIVE AROUND IT OR JUMP OVER IT.",
            "RAMPS WITH RED EDGES ARE HIGHER: EVERY LANDING COSTS 1.5 TIMES AS MUCH.",
            "ON THE FINAL LAP NEW OBSTACLES AND RAMPS APPEAR.",
            "A NITRO JUMP FLIES FURTHER - AND LANDS HARDER.",
            "PLACE AND HEALTH ARE ALONG THE TOP OF THE SCREEN - KEEP AN EYE ON BOTH.",
            "THE DIRT SHORTCUT IS CLOSED TO THE TOP 3 - A BARRIER STANDS ACROSS IT."}},
        {"THE FINAL RACE", {
            "CLEAN LANDINGS COST 1. CROOKED ONES UP TO 9, A BAD ROLL UP TO 15.",
            "RAMMING COSTS YOU 85% OF THE HIT - LET THE OTHERS MAKE MISTAKES.",
            "EVERY WRECK COSTS 2-3 SECONDS: DRIVE CLEAN AND THEY ADD UP FOR THE OTHERS.",
            "MOST DAMAGE TO A CAR THAT IS WRECKED: +15 HEALTH FOR YOU.",
            "SHORT L BOOSTS ARE CHEAP; HOLD IT 10 SECONDS AND THE ENGINE BURNS OUT.",
            "BEHIND? THE SHORTCUT IS ONLY OPEN FROM 4TH PLACE BACK."}},
    };
    return CARDS[size_t(std::clamp(chapter, 0, int(CARDS.size()) - 1))];
}

const int LAST_CHAPTER = 7;                              // (River Serpent's chapter, after the ending)

const std::vector<Line> &storyLinesOf(int chapter, int kind)
{
    const Chapter &ch = chapters()[size_t(std::clamp(chapter, 0, LAST_CHAPTER))];
    return kind == 1 ? ch.won : kind == 2 ? ch.lost : ch.intro;
}
} // namespace

// ===========================================================================
//  The story inside every mode
// ===========================================================================
// The chapter of a map: the five roads in their order, River Serpent's after the ending
int GLWidget::chapterForMap(int map) const
{
    for (int k = 1; k <= LAST_CHAPTER; ++k)
        if (k != ENDING && chapters()[size_t(k)].map == map) return k;
    return 1;
}

// vs CPU and split screen: the race is set up, then its chapter's scene plays at the grid
// (the very first time the prologue before it). [ENTER] goes on, [ESC] skips.
void GLWidget::beginRaceWithStory(sim::GameSim::GameMode mode)
{
    storyMode = mode;
    QSettings st("TPP", "CarRacing");
    storyDone = st.value("story/done", 0).toInt();
    const bool first = !st.value("story/prologueSeen", false).toBool();
    if (first) st.setValue("story/prologueSeen", true);
    storyBeginScene(first ? 0 : chapterForMap(selectedMapPreset), 0);
}

// Online: the same scene on every player's screen while the server holds the race; the
// countdown starts when everyone has watched it - or as soon as one person skips it
void GLWidget::beginOnlineStory()
{
    storyMode = sim::GameSim::ModeOnline;
    netStoryHold = true;
    netStoryWatched = false;
    if (!netTest.isEmpty()) {                            // (scripted test: no scene)
        netStoryWatched = true;
        if (net) net->sendStory(false);
        return;
    }
    storyBeginScene(chapterForMap(selectedMapPreset), 0);
}

void GLWidget::endOnlineStory()
{
    netStoryHold = false;
    camOverride = false;
    hideHud = false;
    actors.clear();
    driverOut.clear();
    story = StoryScreen::Off;
    storyRules = 0;
    simAccumulator = 0.0f;
    elapsedTimer.restart();
}

void GLWidget::storySkipAll()
{
    if (storyMode == sim::GameSim::ModeOnline) {         // online: it ends for everyone (S_STORY_END)
        if (net) net->sendStory(true);
        return;
    }
    storyRules = 2;
    if (storyKind == 0 && storyChapter == 0) storyChapter = chapterForMap(selectedMapPreset);   // (past the prologue too)
    storyEndScene();
}

void GLWidget::drawNetStoryWait(int W, int H, float dpr)
{
    begin2D(W, H);
    const float ps = std::clamp(float(H) / 420.0f, 1.3f, 2.4f);
    const char *t1 = "WAITING FOR THE OTHER PLAYERS' STORY...";
    const char *t2 = "[ESC] SKIP IT FOR EVERYONE";
    glColor4f(0.0f, 0.0f, 0.0f, 0.55f);
    glBegin(GL_QUADS);
    glVertex2f(0, float(H) * 0.40f); glVertex2f(float(W), float(H) * 0.40f);
    glVertex2f(float(W), float(H) * 0.52f); glVertex2f(0, float(H) * 0.52f);
    glEnd();
    drawText(t1, (float(W) - textWidth(t1, ps * 1.4f)) * 0.5f, float(H) * 0.47f, ps * 1.4f, 1.0f, 0.85f, 0.30f);
    drawText(t2, (float(W) - textWidth(t2, ps)) * 0.5f, float(H) * 0.42f, ps, 0.9f, 0.9f, 0.95f);
    (void)dpr;
}

// ===========================================================================
//  Opening the story, saving progress
// ===========================================================================
void GLWidget::openStory()
{
    QSettings st("TPP", "CarRacing");
    storyUnlocked = std::clamp(st.value("story/unlocked", 0).toInt(), 0, 5);
    storyDone = st.value("story/done", 0).toInt();
    storyPick = storyUnlocked;
    story = StoryScreen::Chapters;
    storyKeep[0] = selectedMapPreset; storyKeep[1] = cpuCount;          // story races choose their own
    storyKeep[2] = simEngine.getAiDifficulty(); storyKeep[3] = simEngine.getLaps();
    if (audio) audio->playCountdownBeep(false);
}

// Back to the main menu, with your own map, CPU racers, difficulty and laps again
void GLWidget::leaveStory()
{
    story = StoryScreen::Off;
    camOverride = false;
    hideHud = false;
    actors.clear();
    driverOut.clear();
    nameOverride.clear();
    selectedMapPreset = storyKeep[0];
    cpuCount = storyKeep[1];
    simEngine.setAiDifficulty(storyKeep[2]);
    simEngine.setLaps(storyKeep[3]);
    simEngine.setMap(selectedMapPreset);
    simEngine.setState(State::Menu);
}

// The cars of a story race or scene: the rival in car 1 in their colours, with their name
// and the best CPU level of the chapter; in the prologue and the ending the rest of the
// grid is the Brick Barons crew (black and gold)
void GLWidget::storyDressCars()
{
    if (storyChapter < 0 || storyChapter > LAST_CHAPTER) return;
    const Chapter &ch = chapters()[size_t(storyChapter)];
    auto &cars = simEngine.getCars();
    nameOverride.clear();
    if (storyMode != sim::GameSim::ModeSingle) return;   // (split screen and online: everyone keeps their own)
    nameOverride.assign(cars.size(), QByteArray());
    if (int(cars.size()) > RIVAL_CAR) {
        const CastMember &r = CAST[ch.rival];
        cars[RIVAL_CAR].r = r.car[0]; cars[RIVAL_CAR].g = r.car[1]; cars[RIVAL_CAR].b = r.car[2];
        cars[RIVAL_CAR].rosterId = -1;                   // (the story's own character, not a roster driver)
        // each rival drives like the character: Dusty Dan clean, Viper Vex blocks, Frost Nova
        // charges late, Kai Volt attacks, the Iron Baron holds a grudge
        cars[RIVAL_CAR].aiPersona = ch.rival == DAN ? sim::PersonaClean : ch.rival == VEX ? sim::PersonaBlocker
                                  : ch.rival == NOVA ? sim::PersonaLateCharger : ch.rival == KAI ? sim::PersonaAttacker
                                  : sim::PersonaGrudge;
        nameOverride[RIVAL_CAR] = r.name;
    }
    if (storyChapter == 0 || storyChapter == ENDING)
        for (size_t i = 2; i < cars.size(); ++i) {
            const float k = 0.85f + 0.05f * float(i);
            cars[i].r = 0.14f * k; cars[i].g = 0.14f * k; cars[i].b = 0.16f * k;
            nameOverride[i] = "BARON CREW";
        }
}

// The grid of a chapter: its map, you and five CPUs, its CPU levels (laps: 2 on a circuit)
void GLWidget::storySetupRace()
{
    const Chapter &ch = chapters()[size_t(std::clamp(storyChapter, 0, ENDING))];
    selectedMapPreset = ch.map;
    cpuCount = sim::MAX_RACERS - 1;
    simEngine.setAiDifficulty(std::clamp(ch.difficulty, 0, 2));
    simEngine.setLaps(2);
    startRace(sim::GameSim::ModeSingle);                 // (startRace dresses the cars for the story)
}

// ===========================================================================
//  Scenes
// ===========================================================================
void GLWidget::storyBeginScene(int chapter, int kind)
{
    storyChapter = std::clamp(chapter, 0, LAST_CHAPTER);
    storyKind = kind;
    storyLine = 0;
    storyChars = 0.0f;
    storyTime = 0.0f;
    storyLineTime = 0.0f;
    storyCrashed = false;
    storyRules = 0;
    story = StoryScreen::Scene;
    // the grid stands still while people talk (online it is the server's grid, already set up)
    if (storyMode != sim::GameSim::ModeOnline) startRace(storyMode);
    hideHud = true;
    camOverride = true;
    storyPlaceActors();
    storyShot(true);
}


int GLWidget::storyLineCount() const { return int(storyLinesOf(storyChapter, storyKind).size()); }

// Who stands where: in front of the grid, facing the camera (ahead of them): you on the
// right in front of your car, the rival on the left in front of theirs, Granny Gears
// beside you. You and the rival are out of your cars.
void GLWidget::storyPlaceActors()
{
    actors.clear();
    driverOut.clear();
    const Chapter &ch = chapters()[size_t(storyChapter)];
    const float s0 = simEngine.startLine() - 3.5f;
    auto put = [&](int who, float s, float d, float turn) {
        Actor a;
        a.who = who;
        const CastMember &c = CAST[who];
        for (int k = 0; k < 3; ++k) {
            a.look.suit[k] = c.suit[k]; a.look.trim[k] = c.trim[k]; a.look.helmet[k] = c.helmet[k];
            a.look.skin[k] = c.skin[k]; a.look.hair[k] = c.hair[k];
        }
        a.look.helmetOn = c.helmetOn;
        a.look.face = c.face;
        const int myCar = storyMode == sim::GameSim::ModeOnline ? std::max(0, mySlot) : 0;
        if (who == PLAYER && int(simEngine.getCars().size()) > myCar) {   // your overalls match your car
            const sim::Car &me = simEngine.getCars()[size_t(myCar)];
            a.look.suit[0] = me.r; a.look.suit[1] = me.g; a.look.suit[2] = me.b;
        }
        simEngine.pointAt(s, d, a.x, a.z);
        a.y = simEngine.heightAt(s, d) + 0.45f;          // standing: the hip is 0.45 m up
        a.heading = simEngine.headingAt(s) + turn;
        actors.push_back(a);
    };
    put(PLAYER, s0, 2.8f, -0.35f);
    put(ch.rival, s0, -2.8f, 0.35f);
    put(GRANNY, s0 - 1.0f, 6.4f, -0.55f);
    if (storyMode == sim::GameSim::ModeOnline) driverOut = {std::max(0, mySlot)};
    else driverOut = {0, RIVAL_CAR};
}

// The camera for the line being said: a close-up of the speaker, or the narrator's shots
void GLWidget::storyShot(bool snap)
{
    const std::vector<Line> &lines = storyLinesOf(storyChapter, storyKind);
    if (lines.empty()) return;
    const Line &ln = lines[size_t(std::clamp(storyLine, 0, int(lines.size()) - 1))];
    float eye[3] = {0, 0, 0}, at[3] = {0, 0, 0};
    const float s0 = simEngine.startLine();
    const float H = simEngine.headingAt(s0);
    const float fx = std::sin(H), fz = -std::cos(H), rx = -fz, rz = fx;
    float gx, gz;
    simEngine.pointAt(s0, 0.0f, gx, gz);
    const float gy = simEngine.heightAt(s0, 0.0f);
    const float t = storyLineTime;
    switch (ln.shot) {
    case TEMPLE: {                                       // the temple, slowly closer
        float lx, lz, rot;
        simEngine.landmark(lx, lz, rot);
        float dx = gx - lx, dz = gz - lz;
        const float l = std::max(1.0f, std::sqrt(dx * dx + dz * dz));
        dx /= l; dz /= l;
        const float dist = 70.0f - 3.0f * std::min(t, 6.0f);
        eye[0] = lx + dx * dist; eye[1] = groundAt(lx, lz) + 18.0f; eye[2] = lz + dz * dist;
        at[0] = lx; at[1] = groundAt(lx, lz) + 9.0f; at[2] = lz;
        break;
    }
    case FLYOVER: {                                      // flying along the road, low
        const float s = s0 + 30.0f + 22.0f * t;
        float x, z, ax, az;
        simEngine.pointAt(s, 0.0f, x, z);
        simEngine.pointAt(s + 45.0f, 0.0f, ax, az);
        eye[0] = x; eye[1] = simEngine.heightAt(s, 0.0f) + 6.0f; eye[2] = z;
        at[0] = ax; at[1] = simEngine.heightAt(s + 45.0f, 0.0f) + 2.0f; at[2] = az;
        break;
    }
    case GRID: {                                         // the whole grid from in front, low
        float cx, cz;
        simEngine.pointAt(s0 - 13.0f, 0.0f, cx, cz);
        eye[0] = gx + fx * 16.0f + rx * 4.0f; eye[1] = gy + 2.6f; eye[2] = gz + fz * 16.0f + rz * 4.0f;
        at[0] = cx; at[1] = simEngine.heightAt(s0 - 13.0f, 0.0f) + 1.0f; at[2] = cz;
        break;
    }
    case LIVE_CRASH: {                                   // beside the crew car that is about to crash
        const auto &cars = simEngine.getCars();
        const int k = std::min(2, int(cars.size()) - 1);
        const sim::Car &c = cars[size_t(k)];
        const float cfx = std::sin(c.heading), cfz = -std::cos(c.heading), crx = -cfz, crz = cfx;
        float tx = c.x, tz = c.z, ty = c.y + 1.0f;
        EjectedDriver ed;
        if (ejectedDriver(c, ed)) { tx = 0.5f * (tx + ed.x); tz = 0.5f * (tz + ed.z); ty = std::max(ty, 0.5f * (ty + ed.y + 0.6f)); }
        eye[0] = tx - crx * 12.0f - cfx * 5.0f; eye[1] = c.y + 3.2f; eye[2] = tz - crz * 12.0f - cfz * 5.0f;
        at[0] = tx; at[1] = ty; at[2] = tz;
        break;
    }
    default: {                                           // the speaker's face
        const Actor *a = nullptr;
        for (const Actor &ac : actors) if (ac.who == ln.who) a = &ac;
        if (!a && !actors.empty()) a = &actors[0];
        if (!a) break;
        const float afx = std::sin(a->heading), afz = -std::cos(a->heading), arx = -afz, arz = afx;
        eye[0] = a->x + afx * 3.0f + arx * 0.7f; eye[1] = a->y + 1.05f; eye[2] = a->z + afz * 3.0f + arz * 0.7f;
        at[0] = a->x; at[1] = a->y + 0.62f; at[2] = a->z;
        break;
    }
    }
    const float k = snap || ln.shot == LIVE_CRASH || ln.shot == FLYOVER ? 1.0f : 0.12f;
    for (int i = 0; i < 3; ++i) {
        camEye[i] += (eye[i] - camEye[i]) * k;
        camAt[i] += (at[i] - camAt[i]) * k;
    }
}

// A scene's clock: typing the line, the actors' small movements, the camera, and on a live
// shot the race itself (a crew car is wrecked and throws its driver out)
void GLWidget::storyUpdate(float dt)
{
    storyTime += dt;
    storyLineTime += dt;
    const std::vector<Line> &lines = storyLinesOf(storyChapter, storyKind);
    if (lines.empty()) { storyEndScene(); return; }
    const Line &ln = lines[size_t(std::clamp(storyLine, 0, int(lines.size()) - 1))];
    storyChars = std::min(float(std::strlen(ln.text)), storyChars + dt * 42.0f);

    for (Actor &a : actors) {                            // breathing, and the speaker talks with their hands
        const bool speaking = a.who == ln.who;
        const float w = storyTime * (speaking ? 5.0f : 1.2f) + float(a.who);
        a.pose.armL = speaking ? 25.0f + 30.0f * (0.5f + 0.5f * std::sin(w)) : 4.0f * std::sin(w);
        a.pose.armR = speaking ? 15.0f + 45.0f * (0.5f + 0.5f * std::sin(w * 1.3f + 1.0f)) : 4.0f * std::sin(w + 1.0f);
        a.pose.headTurn = speaking ? 6.0f * std::sin(w * 0.7f) : 0.0f;
        a.look.face = speaking && a.who != PLAYER ? CAST[a.who].face : (a.who == PLAYER ? 0 : CAST[a.who].face);
    }

    if (ln.shot == LIVE_CRASH) {
        auto &cars = simEngine.getCars();
        if (simEngine.getState() == State::Countdown) simEngine.setState(State::Racing);
        storyAcc += dt;
        while (storyAcc >= 1.0f / 60.0f) {
            storyAcc -= 1.0f / 60.0f;
            sim::PlayerInput in[2];
            in[0] = simEngine.computeAIInput(cars[0], 0, false);
            simEngine.step(in, 1);
            processSimEvents(simEngine.getEvents());
            simEngine.clearEvents();
            for (int i = 0; i < sim::MAX_RACERS && i < int(cars.size()); ++i) updateCarFx(i, cars[size_t(i)]);
        }
        if (!storyCrashed && storyLineTime > 1.6f && cars.size() > 2) {
            simEngine.forceWreck(2, true);
            storyCrashed = true;
        }
    }
    storyShot(false);
}

// Enter: show the whole line, or the next line; at the end, what comes after the scene
void GLWidget::storyNextLine()
{
    const std::vector<Line> &lines = storyLinesOf(storyChapter, storyKind);
    if (storyLine < int(lines.size()) && storyChars < float(std::strlen(lines[size_t(storyLine)].text))) {
        storyChars = float(std::strlen(lines[size_t(storyLine)].text));
        return;
    }
    const bool wasLive = storyLine < int(lines.size()) && lines[size_t(storyLine)].shot == LIVE_CRASH;
    ++storyLine;
    storyChars = 0.0f;
    storyLineTime = 0.0f;
    if (storyLine >= int(lines.size())) { storyEndScene(); return; }
    if (wasLive) {                                       // back to the grid after the live crash
        storySetupRace();
        storyPlaceActors();
    }
    storyShot(lines[size_t(storyLine)].shot != ON_SPEAKER || wasLive);
    if (audio) audio->playCountdownBeep(false);
}

void GLWidget::storyEndScene()
{
    if (storyKind == 0 && storyChapter != ENDING && storyRules == 0) { storyRules = 1; return; }   // the rules first
    storyRules = 0;
    camOverride = false;
    hideHud = false;
    actors.clear();
    driverOut.clear();
    if (storyKind == 0) {
        if (storyChapter == 0) { storyBeginScene(chapterForMap(selectedMapPreset), 0); return; }   // the prologue leads into the map's chapter
        if (storyChapter == ENDING) { story = StoryScreen::Off; nameOverride.clear(); simEngine.setState(State::Menu); return; }
        if (storyMode == sim::GameSim::ModeOnline) {     // online: tell the server; the countdown waits for everyone
            story = StoryScreen::Off;
            if (!netStoryWatched && net) { net->sendStory(false); netStoryWatched = true; }
            return;
        }
        // to the race! (vs CPU keeps the story going for the rival's reaction afterwards)
        story = storyMode == sim::GameSim::ModeSingle ? StoryScreen::Race : StoryScreen::Off;
        startRace(storyMode);                            // (a fresh grid: the prologue's live crash moved cars)
        return;
    }
    if (storyKind == 1 && storyChapter == 5) { storyBeginScene(ENDING, 0); return; }
    story = StoryScreen::Off;                            // back to the menu
    nameOverride.clear();
    simEngine.setState(State::Menu);
}

// The race is over (Enter on the results): was the chapter's goal met?
void GLWidget::storyRaceOver()
{
    const auto &cars = simEngine.getCars();
    if (cars.empty()) return;
    const sim::Car &me = cars[0];
    const int rank = simEngine.rankOf(0);
    bool won = me.finished;
    switch (storyChapter) {
    case 1: won = won && rank <= 3; break;
    case 2: won = won && int(cars.size()) > RIVAL_CAR && rank < simEngine.rankOf(RIVAL_CAR); break;
    case 4: won = won && rank == 1 && me.wrecks == 0; break;
    default: won = won && rank == 1; break;
    }
    if (won) {
        storyDone |= 1 << storyChapter;
        storyUnlocked = std::max(storyUnlocked, std::min(5, storyChapter + 1));
        QSettings st("TPP", "CarRacing");
        st.setValue("story/unlocked", storyUnlocked);
        st.setValue("story/done", storyDone);
    }
    storyBeginScene(storyChapter, won ? 1 : 2);
}

bool GLWidget::storyGoalMet() const
{
    const auto &cars = simEngine.getCars();
    if (cars.empty()) return false;
    const sim::Car &me = cars[0];
    const int rank = simEngine.rankOf(0);
    if (!me.finished) return false;
    switch (storyChapter) {
    case 1: return rank <= 3;
    case 2: return int(cars.size()) > RIVAL_CAR && rank < simEngine.rankOf(RIVAL_CAR);
    case 4: return rank == 1 && me.wrecks == 0;
    default: return rank == 1;
    }
}

// ===========================================================================
//  Keys
// ===========================================================================
bool GLWidget::handleStoryKey(QKeyEvent *event)
{
    const int k = event->key();
    const bool enter = k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Space;
    switch (story) {
    case StoryScreen::Off:
        if (isOnline() && netStoryHold && k == Qt::Key_Escape) { if (net) net->sendStory(true); return true; }
        return false;
    case StoryScreen::Chapters:
        if (k == Qt::Key_Up || k == Qt::Key_W)        storyPick = (storyPick + 5) % 6;
        else if (k == Qt::Key_Down || k == Qt::Key_S) storyPick = (storyPick + 1) % 6;
        else if (storyRules == 1) storyRules = 0;       // (any key closes the rules card)
        else if (k == Qt::Key_R) storyRules = 1;
        else if (k == Qt::Key_Escape) leaveStory();
        else if (enter && storyPick <= storyUnlocked) storyBeginScene(storyPick, 0);
        else return true;
        if (audio) audio->playCountdownBeep(false);
        return true;
    case StoryScreen::Scene:
        if (storyRules == 1 && enter) { storyRules = 2; storyEndScene(); }
        else if (k == Qt::Key_Escape) storySkipAll();    // skip the scene and the rules (online: for everyone)
        else if (storyRules != 1 && enter) storyNextLine();
        return true;
    case StoryScreen::Race:
        if (simEngine.getState() == State::Finished && enter) { storyRaceOver(); return true; }
        if ((simEngine.getState() == State::Finished || simEngine.getState() == State::Paused)
            && (k == Qt::Key_M || (k == Qt::Key_Escape && simEngine.getState() == State::Finished))) {
            story = StoryScreen::Off;                    // back to the menu
            nameOverride.clear();
            simEngine.setState(State::Menu);
            return true;
        }
        return false;                                    // the race's own keys
    }
    return false;
}

// ===========================================================================
//  Drawing
// ===========================================================================
// The chapter list over the garage: prologue and five chapters (map, rival, goal, done /
// play / locked) and the chosen chapter's rival standing on the right
void GLWidget::drawStoryChapters(int W, int H, float dpr)
{
    begin2D(W, H);
    auto rect = [](float a, float b, float c, float d) {
        glBegin(GL_QUADS); glVertex2f(a, b); glVertex2f(c, b); glVertex2f(c, d); glVertex2f(a, d); glEnd();
    };
    const float ps = std::clamp(float(H) / 420.0f, 1.3f, 2.4f);
    glColor4f(0.02f, 0.03f, 0.06f, 0.78f);
    rect(0, 0, float(W), float(H));
    const float x0 = float(W) * 0.06f, x1 = float(W) * 0.62f;
    float y = float(H) * 0.88f;
    drawText("STORY MODE", x0, y, ps * 3.0f, 1.0f, 0.80f, 0.25f);
    y -= 7.0f * ps * 1.4f + 12.0f * dpr;
    drawText("THE GOLDEN WHEEL", x0, y, ps * 1.4f, 0.85f, 0.88f, 0.95f);
    y -= 7.0f * ps * 1.4f + 26.0f * dpr;
    const float rowH = 7.0f * ps * 1.25f + 7.0f * ps * 0.9f + 36.0f * dpr;
    for (int i = 0; i <= 5; ++i) {
        const Chapter &ch = chapters()[size_t(i)];
        const bool locked = i > storyUnlocked, done = (storyDone >> i) & 1, sel = i == storyPick;
        const float ry1 = y, ry0 = y - rowH + 6.0f * dpr;
        if (sel) { glColor4f(0.10f, 0.30f, 0.45f, 0.92f); rect(x0 - 8.0f * dpr, ry0, x1, ry1); }
        else     { glColor4f(0.06f, 0.09f, 0.15f, 0.80f); rect(x0 - 8.0f * dpr, ry0, x1, ry1); }
        if (sel) { glColor4f(1.0f, 0.80f, 0.25f, 1.0f); rect(x0 - 8.0f * dpr, ry0, x0 - 3.0f * dpr, ry1); }
        char head[96];
        if (i == 0) snprintf(head, sizeof head, "PROLOGUE - %s", ch.title);
        else snprintf(head, sizeof head, "%d. %s - %s", i, sim::MAPS[ch.map].name, ch.title);
        const float lk = locked ? 0.45f : 1.0f;
        drawText(head, x0 + 6.0f * dpr, ry1 - 7.0f * ps * 1.25f - 8.0f * dpr, ps * 1.25f, 0.95f * lk, 0.95f * lk, 0.98f * lk);
        char sub[128];
        if (i == 0) snprintf(sub, sizeof sub, "HOW IT ALL BEGAN");
        else snprintf(sub, sizeof sub, "VS %s   GOAL: %s", CAST[ch.rival].name, ch.goal);
        drawText(sub, x0 + 6.0f * dpr, ry0 + 8.0f * dpr, ps * 0.9f, 0.70f * lk, 0.80f * lk, 0.92f * lk);
        const char *st = locked ? "LOCKED" : done ? "DONE" : "PLAY";
        const float sr = locked ? 0.5f : done ? 0.35f : 1.0f, sg = locked ? 0.5f : done ? 1.0f : 0.80f, sb = locked ? 0.55f : done ? 0.5f : 0.25f;
        drawText(st, x1 - 10.0f * dpr - textWidth(st, ps * 1.25f), ry1 - 7.0f * ps * 1.25f - 8.0f * dpr, ps * 1.25f, sr, sg, sb);
        y -= rowH;
    }
    const char *hint = storyPick <= storyUnlocked ? "[UP/DOWN] CHOOSE   [ENTER] PLAY   [R] RULES   [ESC] BACK"
                                                  : "WIN THE CHAPTER BEFORE TO UNLOCK THIS ONE   [R] RULES   [ESC] BACK";
    drawText(hint, x0, 18.0f * dpr, ps * 0.95f, 0.70f, 0.80f, 0.92f);

    // the rival of the chosen chapter (Granny Gears for the prologue), turning slowly
    const int who = storyPick == 0 ? GRANNY : chapters()[size_t(storyPick)].rival;
    const CastMember &c = CAST[who];
    FigureLook L;
    for (int k = 0; k < 3; ++k) {
        L.suit[k] = c.suit[k]; L.trim[k] = c.trim[k]; L.helmet[k] = c.helmet[k]; L.skin[k] = c.skin[k]; L.hair[k] = c.hair[k];
    }
    L.helmetOn = c.helmetOn;
    L.face = c.face;
    const int vx = int(float(W) * 0.66f), vw = int(float(W) * 0.30f), vy = int(float(H) * 0.18f), vh = int(float(H) * 0.62f);
    glViewport(vx, vy, vw, vh);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluPerspective(30.0, double(vw) / double(std::max(vh, 1)), 0.1, 50.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    gluLookAt(0.0, 0.55, 4.2, 0.0, 0.45, 0.0, 0, 1, 0);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glDisable(GL_FOG);
    const GLfloat lp[] = {0.4f, 1.0f, 0.8f, 0.0f}, amb[] = {0.45f, 0.45f, 0.50f, 1.0f}, dif[] = {0.85f, 0.85f, 0.80f, 1.0f};
    glLightfv(GL_LIGHT0, GL_POSITION, lp);
    glLightfv(GL_LIGHT0, GL_AMBIENT, amb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);
    glPushMatrix();
    glRotatef(180.0f + 25.0f * std::sin(float(simEngine.getTick() + int(storyTime * 60.0f)) * 0.02f) + storyTime * 30.0f, 0, 1, 0);
    glScalef(1.6f, 1.6f, 1.6f);
    FigurePose P;
    P.armR = 20.0f + 15.0f * std::sin(storyTime * 2.0f);
    drawBlockFigure(L, P);
    glPopMatrix();
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glViewport(0, 0, W, H);
    begin2D(W, H);
    drawText(c.name, float(vx) + (float(vw) - textWidth(c.name, ps * 1.4f)) * 0.5f, float(vy) - 7.0f * ps * 1.4f - 4.0f * dpr,
             ps * 1.4f, c.tag[0], c.tag[1], c.tag[2]);
}

// Over a scene: black bars top and bottom, the chapter's title card at first, and the
// dialogue box - the speaker's name and the line typing out, [ENTER] when it is done
void GLWidget::drawStoryOverlay(int W, int H, float dpr)
{
    begin2D(W, H);
    auto rect = [](float a, float b, float c, float d) {
        glBegin(GL_QUADS); glVertex2f(a, b); glVertex2f(c, b); glVertex2f(c, d); glVertex2f(a, d); glEnd();
    };
    const float ps = std::clamp(float(H) / 420.0f, 1.3f, 2.4f);
    const float bar = float(H) * 0.09f;
    glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
    rect(0, 0, float(W), bar);
    rect(0, float(H) - bar, float(W), float(H));

    if (storyRules == 1) { drawStoryRules(W, H, dpr, storyChapter); return; }
    const std::vector<Line> &lines = storyLinesOf(storyChapter, storyKind);
    if (storyKind == 0 && storyChapter >= 1 && storyChapter <= 5 && storyTime < 2.6f) {   // title card
        const Chapter &ch = chapters()[size_t(storyChapter)];
        const float a = std::clamp(std::min(storyTime / 0.4f, (2.6f - storyTime) / 0.5f), 0.0f, 1.0f);
        char t1[32], t2[64];
        snprintf(t1, sizeof t1, "CHAPTER %d", storyChapter);
        snprintf(t2, sizeof t2, "%s - %s", ch.title, sim::MAPS[ch.map].name);
        glColor4f(0.0f, 0.0f, 0.0f, 0.55f * a);
        rect(0, float(H) * 0.56f, float(W), float(H) * 0.76f);
        glColor4f(1.0f, 0.80f, 0.25f, a);
        drawText(t1, (float(W) - textWidth(t1, ps * 2.0f)) * 0.5f, float(H) * 0.69f, ps * 2.0f, 1.0f, 0.80f, 0.25f);
        drawText(t2, (float(W) - textWidth(t2, ps * 1.5f)) * 0.5f, float(H) * 0.60f, ps * 1.5f, 1.0f, 1.0f, 1.0f);
    }
    if (lines.empty() || storyLine >= int(lines.size())) return;
    const Line &ln = lines[size_t(storyLine)];
    const CastMember &c = CAST[ln.who];

    const float bx0 = float(W) * 0.10f, bx1 = float(W) * 0.90f, by0 = bar + 10.0f * dpr, by1 = bar + float(H) * 0.17f;
    glColor4f(0.03f, 0.05f, 0.10f, 0.88f);
    rect(bx0, by0, bx1, by1);
    glColor4f(c.tag[0], c.tag[1], c.tag[2], 0.9f);
    rect(bx0, by1 - 3.0f * dpr, bx1, by1);
    const float tps = ps * 1.35f;
    float ty = by1 - 12.0f * dpr - 7.0f * tps;
    if (ln.who != NARRATOR) {
        const float nps = ps * 1.2f;
        glColor4f(0.03f, 0.05f, 0.10f, 0.95f);
        rect(bx0 + 14.0f * dpr, by1, bx0 + 30.0f * dpr + textWidth(c.name, nps), by1 + 7.0f * nps + 10.0f * dpr);
        drawText(c.name, bx0 + 22.0f * dpr, by1 + 5.0f * dpr, nps, c.tag[0], c.tag[1], c.tag[2]);
    }
    // the line, typed so far, wrapped to the box
    const std::string full(ln.text);
    const std::string shown = full.substr(0, size_t(std::max(0.0f, storyChars)));
    const float maxW = bx1 - bx0 - 40.0f * dpr;
    std::string cur, word;
    std::vector<std::string> rows;
    auto flushWord = [&]() {
        if (word.empty()) return;
        const std::string trial = cur.empty() ? word : cur + " " + word;
        if (textWidth(trial.c_str(), tps) > maxW && !cur.empty()) { rows.push_back(cur); cur = word; }
        else cur = trial;
        word.clear();
    };
    for (char ch : shown) { if (ch == ' ') flushWord(); else word += ch; }
    flushWord();
    if (!cur.empty()) rows.push_back(cur);
    for (const std::string &r : rows) {
        const float tx = ln.who == NARRATOR ? (bx0 + bx1 - textWidth(r.c_str(), tps)) * 0.5f : bx0 + 20.0f * dpr;
        drawText(r.c_str(), tx, ty, tps, 0.95f, 0.95f, 0.98f);
        ty -= 7.0f * tps + 7.0f * dpr;
    }
    if (shown.size() >= full.size() && (int(storyTime * 2.0f) % 2 == 0)) {
        const char *e = storyLine + 1 < int(lines.size()) ? "[ENTER]" : "[ENTER] CONTINUE";
        drawText(e, bx1 - 14.0f * dpr - textWidth(e, ps), by0 + 8.0f * dpr, ps, 1.0f, 0.80f, 0.25f);
    }
    const char *skip = storyMode == sim::GameSim::ModeOnline ? "[ESC] SKIP FOR EVERYONE" : "[ESC] SKIP";
    drawText(skip, float(W) - 16.0f * dpr - textWidth(skip, ps * 0.9f), float(H) - bar + 0.35f * bar, ps * 0.9f, 0.6f, 0.6f, 0.65f);
}

// In a story race: the chapter's goal at the start, and after the finish whether it was met
void GLWidget::drawStoryRaceInfo(int W, int H, float dpr)
{
    begin2D(W, H);
    const float ps = std::clamp(float(H) / 420.0f, 1.3f, 2.4f);
    const Chapter &ch = chapters()[size_t(std::clamp(storyChapter, 0, ENDING))];
    if (simEngine.getState() == State::Countdown || (simEngine.getState() == State::Racing && simEngine.getRaceTimer() < 4.0f)) {
        char g[96];
        snprintf(g, sizeof g, "GOAL: %s", ch.goal);
        const float gs = ps * 1.5f, gw = textWidth(g, gs), gy = float(H) * 0.30f;
        glColor4f(0.0f, 0.0f, 0.0f, 0.6f);
        glBegin(GL_QUADS);
        glVertex2f((float(W) - gw) * 0.5f - 14.0f * dpr, gy - 10.0f * dpr); glVertex2f((float(W) + gw) * 0.5f + 14.0f * dpr, gy - 10.0f * dpr);
        glVertex2f((float(W) + gw) * 0.5f + 14.0f * dpr, gy + 7.0f * gs + 10.0f * dpr); glVertex2f((float(W) - gw) * 0.5f - 14.0f * dpr, gy + 7.0f * gs + 10.0f * dpr);
        glEnd();
        drawText(g, (float(W) - gw) * 0.5f, gy, gs, 1.0f, 0.80f, 0.25f);
    }
    if (simEngine.getState() == State::Finished) {
        const bool ok = storyGoalMet();
        const char *t = ok ? "GOAL COMPLETE!   [ENTER] CONTINUE THE STORY" : "GOAL MISSED   [ENTER] CONTINUE THE STORY";
        const float s = ps * 1.3f;
        glColor4f(0.0f, 0.0f, 0.0f, 0.7f);
        glBegin(GL_QUADS);
        glVertex2f(0, float(H) * 0.93f); glVertex2f(float(W), float(H) * 0.93f);
        glVertex2f(float(W), float(H)); glVertex2f(0, float(H));
        glEnd();
        drawText(t, (float(W) - textWidth(t, s)) * 0.5f, float(H) * 0.95f, s, ok ? 0.4f : 1.0f, ok ? 1.0f : 0.5f, ok ? 0.5f : 0.4f);
    }
    (void)dpr;
}

// The rules card: a panel in the middle of the screen, the chapter's rules one per line
// (wrapped), the keys at the bottom
void GLWidget::drawStoryRules(int W, int H, float dpr, int chapter)
{
    const RulesCard &card = rulesCard(chapter);
    begin2D(W, H);
    auto rect = [](float a, float b, float c, float d) {
        glBegin(GL_QUADS); glVertex2f(a, b); glVertex2f(c, b); glVertex2f(c, d); glVertex2f(a, d); glEnd();
    };
    const float ps = std::clamp(float(H) / 420.0f, 1.3f, 2.4f);
    const float tps = ps * 1.15f, hps = ps * 2.0f;
    const float bx0 = float(W) * 0.12f, bx1 = float(W) * 0.88f, maxW = bx1 - bx0 - 48.0f * dpr;
    // wrap every rule to the panel
    std::vector<std::string> rows;
    std::vector<bool> first;
    for (const char *line : card.lines) {
        std::string cur, word;
        bool head = true;
        auto flush = [&]() {
            if (word.empty()) return;
            const std::string trial = cur.empty() ? word : cur + " " + word;
            if (textWidth(trial.c_str(), tps) > maxW && !cur.empty()) { rows.push_back(cur); first.push_back(head); head = false; cur = word; }
            else cur = trial;
            word.clear();
        };
        for (const char *c = line; *c; ++c) { if (*c == ' ') flush(); else word += *c; }
        flush();
        if (!cur.empty()) { rows.push_back(cur); first.push_back(head); }
    }
    const float rowH = 7.0f * tps + 9.0f * dpr;
    const float panelH = 7.0f * hps + 34.0f * dpr + rowH * float(rows.size()) + 7.0f * ps + 30.0f * dpr;
    const float by0 = (float(H) - panelH) * 0.5f, by1 = by0 + panelH;
    glColor4f(0.0f, 0.0f, 0.0f, 0.45f);
    rect(0, 0, float(W), float(H));
    glColor4f(0.03f, 0.05f, 0.10f, 0.94f);
    rect(bx0, by0, bx1, by1);
    glColor4f(1.0f, 0.80f, 0.25f, 0.95f);
    rect(bx0, by1 - 4.0f * dpr, bx1, by1);
    rect(bx0, by0, bx1, by0 + 2.0f * dpr);
    char head[64];
    snprintf(head, sizeof head, "RULES: %s", card.title);
    float y = by1 - 18.0f * dpr - 7.0f * hps;
    drawText(head, (bx0 + bx1 - textWidth(head, hps)) * 0.5f, y, hps, 1.0f, 0.80f, 0.25f);
    y -= 16.0f * dpr + rowH;
    for (size_t i = 0; i < rows.size(); ++i, y -= rowH) {
        if (first[i]) {                                  // a bullet before each rule
            glColor4f(1.0f, 0.45f, 0.75f, 1.0f);
            rect(bx0 + 20.0f * dpr, y + 2.0f * tps, bx0 + 20.0f * dpr + 3.0f * tps, y + 5.0f * tps);
        }
        drawText(rows[i].c_str(), bx0 + 24.0f * dpr + 5.0f * tps, y, tps, 0.95f, 0.95f, 0.98f);
    }
    const char *keys = story == StoryScreen::Scene ? (storyMode == sim::GameSim::ModeOnline ? "[ENTER] TO THE RACE   [ESC] SKIP FOR EVERYONE" : "[ENTER] TO THE RACE")
                                                   : "ANY KEY: CLOSE";
    if (int(storyTime * 2.0f) % 2 == 0 || story != StoryScreen::Scene)
        drawText(keys, (bx0 + bx1 - textWidth(keys, ps)) * 0.5f, by0 + 12.0f * dpr, ps, 1.0f, 0.80f, 0.25f);
}

// ===========================================================================
//  Browser test: car_racing.html?storytest&mute
// ===========================================================================
// Plays every map's scenes through, as Enter would, with a race start and finish between them,
// and says on the console what it is doing, the slowest frame and the memory in use. A stop in
// the browser shows as the last line printed. Two rounds: anything that grows shows too.
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/console.h>
#include <emscripten/heap.h>
EM_JS(int, tpp_page_storytest, (), { return window.location.search.indexOf("storytest") >= 0 ? 1 : 0; });

void GLWidget::storyTestStep()
{
    static int on = -1, phase = 0, map = 0, round = 0, wait = 60;
    static double last = 0.0, worst = 0.0;
    if (on < 0) on = tpp_page_storytest();
    if (!on) return;
    const double now = emscripten_get_now();
    if (last > 0.0) worst = std::max(worst, now - last);
    last = now;
    if (wait > 0) { --wait; return; }
    auto say = [&](const char *what) {
        char b[160];
        snprintf(b, sizeof b, "[storytest] round %d map %d (%s): %s - slowest frame %.0f ms, memory %d MB", round, map,
                 sim::MAPS[map].name, what, worst, int(emscripten_get_heap_size() / (1024 * 1024)));
        emscripten_console_log(b);
    };
    auto enter = [&]() {
        QKeyEvent e(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        handleStoryKey(&e);
    };
    switch (phase) {
    case 0:                                              // the grid and the chapter's scene
        say("starting the scene");
        worst = 0.0;
        selectedMapPreset = map;
        cpuCount = sim::MAX_RACERS - 1;
        beginRaceWithStory(sim::GameSim::ModeSingle);
        phase = 1;
        wait = 8;
        break;
    case 1:                                              // through its lines and the rules
        if (story == StoryScreen::Scene) { enter(); wait = 5; break; }
        say("scene over, racing");
        phase = 2;
        wait = 300;
        break;
    case 2:                                              // the race ends (the results, the profile, the clone)
        say("the race ends");
        simEngine.setState(State::Finished);
        profileAfterRace();
        phase = 3;
        wait = 60;
        break;
    case 3:                                              // Enter on the results: the "you lost" scene
        say("results, Enter");
        story = StoryScreen::Race;
        enter();
        phase = 4;
        wait = 8;
        break;
    case 4:
        if (story == StoryScreen::Scene) { enter(); wait = 5; break; }
        say("the 'lost' scene is over");
        storyMode = sim::GameSim::ModeSingle;
        storyBeginScene(storyChapter, 1);                // the "you won" scene
        phase = 5;
        wait = 8;
        break;
    case 5:
        if (story == StoryScreen::Scene) { enter(); wait = 5; break; }
        say("the 'won' scene is over");
        simEngine.setState(State::Menu);
        story = StoryScreen::Off;
        phase = 0;
        wait = 30;
        if (++map >= sim::NUM_MAPS) {
            map = 0;
            if (++round >= 2) { emscripten_console_log("[storytest] FINISHED"); on = 0; }
        }
        break;
    default: break;
    }
}
#endif
