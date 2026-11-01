#ifndef GLWIDGET_H
#define GLWIDGET_H

#ifdef __EMSCRIPTEN__
// the browser: the game is the page's window and draws straight onto its canvas (WebGL can't share a
// widget's offscreen context with the window's, docs/browser/README.md)
#include <QOpenGLWindow>
using GLBase = QOpenGLWindow;
using GLParent = QWindow;
#else
#include <QOpenGLWidget>
using GLBase = QOpenGLWidget;
using GLParent = QWidget;
#endif
#include <QOpenGLFunctions>
#include <QTimer>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QSet>
#include <vector>
#include <deque>
#include <random>
#include <QStringList>
#include <QHash>
#include <functional>

#include "shared/gamesim.h"
#include "shared/clone.h"
#include <QJsonObject>
#include <thread>
#include <atomic>

class AudioEngine;
class HostedServer;
class RaceClient;
struct GLUquadric;

// Client-side camera and rendering view state for a vehicle
struct CarView {
    float camHeading = 0.0f;      // Smoothed heading for the chase camera
    float fov = 64.0f;            // Dynamic field of view (widens with speed/nitro)
    float shake = 0.0f;           // Camera shake after collisions
    int   camMode = 0;            // 0: Chase, 1: Hood, 2: Bumper
    float camDist = 8.0f;         // Smoothed chase distance (pulls back with speed)
    bool  snap = true;            // first frame of a race: point the camera along the car
    float camY = 0.0f;            // Smoothed height of the car (the camera rides hills gently)
};

// Client-side visual state per car (brake lights, skid-mark trails)
struct CarFx {
    float prevSpeed = 0.0f;
    float brakeGlow = 0.0f;       // 0..1, drives the brake-light brightness
    float bigAir = 0.0f;          // the last long flight, in seconds ...
    int   bigAirUntil = -1;       // ... and the tick its "BIG AIR" message ends
    bool  marking = false;        // Rear tyres currently laying rubber
    float lastX[2] = {0.0f, 0.0f}, lastZ[2] = {0.0f, 0.0f}, lastY[2] = {0.0f, 0.0f};
};

// How a car stands on the road: height, nose-up and right-side-up angles (degrees)
struct RoadPose {
    float y = 0.0f, pitch = 0.0f, roll = 0.0f;
};

// Tyre mark segment left on the asphalt while drifting / braking hard
struct SkidMark {
    float x0, z0, x1, z1;
    float alpha;
    float y0, y1;                 // road height at the two ends
};

// A block-figure driver - our own toy-brick look: square head, C-shaped hands, overalls in
// the car's colour, an open-face helmet (or hair). Drawn sitting in every car, thrown out
// of a wrecked one, and as the characters of the story.
struct FigureLook {
    float suit[3]   = {0.90f, 0.20f, 0.20f};   // overalls
    float trim[3]   = {1.00f, 1.00f, 1.00f};   // stripe down the front, helmet stripe
    float helmet[3] = {0.95f, 0.95f, 0.95f};
    float skin[3]   = {1.00f, 0.80f, 0.25f};   // classic toy yellow
    float hair[3]   = {0.30f, 0.18f, 0.08f};
    bool  helmetOn  = true;
    int   face      = 0;                        // 0 smile, 1 grin, 2 frown, 3 surprised
};
struct FigurePose {
    float hip = 0.0f;                           // legs forward (degrees; 90 = sitting)
    float legSwing = 0.0f;                      // running: one leg forward, the other back
    float armL = 0.0f, armR = 0.0f;             // arms forward (degrees; 90 = straight ahead, 180 = up)
    float headTurn = 0.0f;                      // degrees, + = to the figure's left
};

struct Particle {
    enum Type { Smoke, Flame, Spark, Dust };
    Type  type = Smoke;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    float size = 0.3f;
    float life = 1.0f;
    float decay = 1.5f;
    float floor = 0.0f;           // height it bounces off (the road where it started)
};

class GLWidget : public GLBase, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    using GameMode = sim::GameSim::GameMode;
    using State = sim::GameSim::State;

    explicit GLWidget(GLParent *parent = nullptr);
    float pixelRatio() const;               // (the browser's own in the browser, see glwidget.cpp)
    float logicalHeight() const;
    int frameW = 0, frameH = 0;             // the size of the last frame drawn, in pixels
    ~GLWidget() override;

protected:
    void initializeGL() override;
    void paintGL() override;
    void paintFrame();
    void resizeGL(int w, int h) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private slots:
    void updateGame();

private:
    // ---- Simulation Engine ----
    sim::GameSim simEngine;

    // Client-side vehicle view parameters
    CarView view1;
    CarView view2;
    float nitroBannerTimer[sim::MAX_RACERS] = {};

    int menuSelection = 0;        // 0 to 6
    enum MenuModal { ModalNone, ModalOnline, ModalCarSelect, ModalMapSelect, ModalProfile, ModalSettings };
    MenuModal activeModal = ModalNone;
    int selectedCarPreset = 0;
    int selectedMapPreset = 0;
    int cpuCount = 3;             // vs CPU: how many CPU racers (1 .. MAX_RACERS - 1)
    bool autoDrive = false;       // scripted test: the CPU logic drives your car
    float autoLaneS = -1.0f, autoLaneD = 0.0f;
    int stuntTick[sim::MAX_RACERS] = {-1, -1, -1, -1, -1, -1};   // when each car's last landing word was shown   // scripted test: hold this lane until s (a side ramp)

    std::vector<Particle> particles;
    CarFx carFx[sim::MAX_RACERS];
    std::vector<SkidMark> skidMarks;

    // ---- Online multiplayer ("ONLINE MULTIPLAYER (6P)") -- glwidget_online.cpp ----
    enum class NetScreen { Off, Choose, EnterCode, Busy, Lobby, Race };
    NetScreen netScreen = NetScreen::Off;
    int netChoice = 0;                     // Choose screen: 0 host here, 1 host on the internet server, 2 join, 3 back
    QString raceUrl() const;               // the internet race server: wss://<api site>/race (CAR_RACING_RACE overrides)
    void netHostInternet();
    QString joinText;                      // room code being typed
    bool nameEditing = false;              // lobby: typing a new name for yourself
    QString nameText;
    QString netMessage;                    // status or error line
    HostedServer *hostServer = nullptr;    // room server running inside this game (own thread) while hosting
    RaceClient *net = nullptr;
    int mySlot = 0;                        // my car in the online race
    QStringList racerNames;
    std::vector<sim::SimEvent> netEvents;  // sounds/particles sent by the server
    struct SentInput { quint32 seq; sim::PlayerInput in; };
    std::deque<SentInput> sentInputs;      // inputs the server hasn't confirmed yet
    quint32 inputSeq = 0;
    sim::Car predicted;                    // my car, simulated locally so it reacts instantly
    float corrX = 0.0f, corrZ = 0.0f;      // smooths out small prediction corrections
    struct Sample { double t; std::vector<sim::Car> cars; };   // t = server time (ms) of the snapshot
    std::deque<Sample> history;            // recent snapshots: other cars are drawn slightly in the past
    QElapsedTimer netClock;
    double serverClockAhead = 0.0;         // server time minus netClock, from the quickest snapshots
    QString netTest;                       // CAR_RACING_NETTEST: scripted online test (see runNetAutoShot)
    int statSnapshots = 0, statSmallFix = 0, statSnaps = 0;   // how often the prediction was corrected
    int statLandings = 0;                                    // jumps your car landed
    // more scripted-test measurements: frame times, drawing time, gaps between snapshots
    int statFrames = 0, statSlowFrames = 0, statSnapLate = 0;
    double statPaintMs = 0.0, statPaintMax = 0.0, statSnapGapMax = 0.0, statSnapBytes = 0.0;
    qint64 statLastSnap = -1;
    bool trackMismatch = false;            // my track differs from the host's (different game version)
    bool isOnline() const { return netScreen == NetScreen::Race; }
    int localCar() const { return isOnline() ? mySlot : 0; }
    void openOnline();
    void netHost();
    void netJoin();
    void netLeave(const QString &message);
    void netUpdate(float realDt);
    void onNetSnapshot(quint32 lastSeq, const QByteArray &body);
    void composeOnlineCars();
    bool handleOnlineKey(QKeyEvent *event);
    void drawOnlineScreen(int W, int H, float dpr);
    void drawOnlineResults(int W, int H, float dpr);
    void runNetAutoShot();
    void myCarColor(float &r, float &g, float &b) const;
    QString savedName() const;             // name chosen in a lobby before; empty = CAR1, CAR2, ...
    static QString lanAddress();

    // Screenshots: F12 saves one; CAR_RACING_AUTOSHOT=<dir> runs a scripted capture and quits
    QString autoShotDir;
    int autoShotFrame = 0;
    void saveScreenshot(const QString &path);
    void runAutoShot();

    QSet<int> keys;
    QTimer *timer = nullptr;
    QElapsedTimer elapsedTimer;
    float simAccumulator = 0.0f;
    std::mt19937 rng;
    AudioEngine *audio = nullptr;

    GLUquadric *quad = nullptr;
    GLuint texGrass = 0, texAsphalt = 0, texStone = 0, texPlaster = 0, texRoof = 0;
    GLuint texEnv = 0;            // Sphere map used for glossy paint / glass reflections
    GLuint texSoft = 0;           // Soft round sprite for smoke, dust, flames and sparks
    GLuint texSand = 0, texSnow = 0, texPavement = 0;   // ground for the desert / snow / city maps
    int    envTheme = -1;         // which map's sky the reflection map currently shows
    bool   nightMode = false;     // current map is at night (lamps glow)
    int    themeIndex() const { return int(simEngine.mapDef().theme); }
    void   ensureEnvMap(int theme);
    GLuint carBodyList = 0;       // Display lists for the sculpted car model (built once)
    GLuint carGlassList = 0;
    GLuint carTrimList = 0;
    QHash<int, GLuint> modelLists;  // scenery and wheel models, recorded once (see drawCachedModel)
    void drawCachedModel(int key, const std::function<void()> &draw);

    // ---- Input & helper ----
    bool isDown(int key) const { return keys.contains(key); }
    float rnd(float a, float b);
    float roadHalf() const { return simEngine.roadHalf(); }       // wider in online rooms
    float railOffset() const { return simEngine.railOffset(); }
    int   getTick() const { return simEngine.getTick(); }
    void startRace(GameMode mode);
    void processSimEvents(const std::vector<sim::SimEvent> &events);
    bool cycleMenuOption(int item, int dir);   // car / map / CPU difficulty pickers in the menu

    // ---- particles ----
    void spawnParticle(Particle::Type t, float x, float y, float z,
                       float vx, float vy, float vz,
                       float r, float g, float b, float a,
                       float size, float life, float decay);
    void spawnSparks(float x, float y, float z, int count = 6);
    void spawnDebris(float x, float y, float z, float r, float g, float b, int count = 8);
    void updateParticles(float dt);
    void drawParticles();

    // ---- procedural raster textures ----
    void   buildTextures();
    GLuint makeTexture(int w, int h, const std::vector<unsigned char> &rgb);

    // ---- rendering ----
    void renderView(const sim::Car &me, const CarView &meView,
                    const sim::Car &other, const CarView &otherView,
                    int playerNum, int vx, int vy, int vw, int vh);
    void begin2D(int w, int h);
    void drawSky(int vw, int vh);
    void drawSunMountainsClouds(float ex, float ez);
    void drawTemple(float ex, float ez);
    void drawGround(float ex, float ez);
    // what the current view can see: the camera's place and direction on the ground
    float viewX = 0.0f, viewZ = 0.0f, viewFx = 0.0f, viewFz = -1.0f;
    bool  inView(float x, float z, float radius = 0.0f) const;
    void  placeOnRoad(float s, float d) const;  // model frame there, facing along the road
    struct RoadChunk { GLuint surface = 0, marks = 0; float x = 0.0f, z = 0.0f; bool dirt = false; };
    std::vector<RoadChunk> roadChunks;          // display lists of the road, about 50 m each
    int   roadKey = -1;                         // map and lanes the lists were built for
    // Tunnels, bridges and Neon City's raised highway, from the slices' markers: slices
    // [a, b); kind 1 tunnel, 2 bridge, 3 raised highway; side = the mountain side of a gallery
    struct Stretch { int a = 0, b = 0, kind = 0; float side = 1.0f; };
    std::vector<Stretch> stretches;
    int   stretchKey = -1;
    void  findStretches();
    float tunnelShade(float s) const;           // 0 outside a tunnel, 1 well inside
    void  drawWater();                          // under the bridges over a river or the harbour
    float forkSide = 1.0f, mergeSide = 1.0f;    // the side of the main road the shortcut leaves / joins on
    void  buildRoad();
    int   trackKey() const { return simEngine.getMap() * 16 + simEngine.laneCount(); }
    void  ensureTrackGeometry();                // (re)build road and ground for this map and width

    // Ground: a height map in an 8 m grid around the circuit (for looks only)
    std::vector<float> groundH;                 // heights, row by row
    float groundX0 = 0.0f, groundZ0 = 0.0f, groundMin = 0.0f;
    int   groundW = 0, groundD = 0;             // vertices across (x) and deep (z)
    struct GroundChunk { GLuint list = 0; float x = 0.0f, z = 0.0f; };
    std::vector<GroundChunk> groundChunks;      // 64 m squares
    int   groundKey = -1;
    void  buildGround();
    float groundAt(float x, float z) const;
    struct PropBase { float base = 0.0f, top = 0.0f; };   // lowest and highest ground under a prop
    std::vector<PropBase> propBase;
    int   propBaseKey = -1;

    RoadPose poseOn(float s, float d, float heading, int path = 0) const;
    void drawRoad();
    void drawArch(float s0, const char *word = "FINISH");   // the start/finish gantry at s0 and its scene
    void drawRoadEnds();                        // a sprint: barriers across both ends of the road
    void drawProps();
    void drawHouse(int palette);
    void drawPine(bool snow = false);
    void drawCactus();
    void drawRock(int palette, bool mesa);
    void drawBuilding(float heightScale, int palette, int seed);
    void drawSea();
    void drawRoundTree();
    void drawPalmTree();
    void drawLighthouse();
    void drawLamp();
    void drawTorch(int palette, float phase);
    void drawObstacles();
    void drawNitroPickups();
    void drawNitroCanister();
    // health >= 0: a health bar over the name plate; driver: the figure sitting inside
    void drawCar(float x, float z, float heading, float roll, float pitch,
                 float steer, float spin, float r, float g, float b, bool nitro,
                 const char *badge = nullptr, float brake = 0.0f, const RoadPose &pose = RoadPose(),
                 float health = -1.0f, const FigureLook *driver = nullptr);
    void drawPlate(const char *badge, float health);   // name plate (+ health bar) over the car's frame
    // ---- crashes (milestone "crashes"): block-figure drivers, wrecks, health ----
    void drawBlockFigure(const FigureLook &look, const FigurePose &pose);
    FigureLook driverLook(int carIdx, const sim::Car &c) const;
    // a driver thrown out of a wreck: where they are, how they lie / run (false: in the car)
    struct EjectedDriver { float x = 0, y = 0, z = 0, heading = 0, rotX = 0, rotZ = 0; FigurePose pose; int face = 0; };
    bool ejectedDriver(const sim::Car &c, EjectedDriver &out) const;
    float floorUnder(float x, float z, const sim::Car &ref) const;   // road or ground below a point
    void drawWreck(const sim::Car &c, int idx, const char *badge);   // tumbling wreck, then waiting
    QByteArray carName(int i) const;            // "CPU2", "YOU", "P1", a name online or in the story
    bool isLocalCar(int i) const;               // a player at this computer drives car i
    std::vector<QByteArray> nameOverride;       // story races: the rivals' names
    // ---- Story mode ("STORY MODE") -- glwidget_story.cpp ----
    enum class StoryScreen { Off, Chapters, Scene, Race };
    StoryScreen story = StoryScreen::Off;
    int   storyChapter = 0;                     // 0 prologue, 1-5 the chapters, 6 the ending
    int   storyPick = 0;                        // cursor in the chapter list
    int   storyUnlocked = 0;                    // highest chapter you may play (saved)
    int   storyDone = 0;                        // a bit for every chapter won (saved)
    int   storyKind = 0;                        // scene: 0 before the race, 1 won, 2 lost
    int   storyLine = 0;                        // the line being said
    float storyChars = 0.0f;                    // letters of it typed so far
    float storyTime = 0.0f, storyLineTime = 0.0f, storyAcc = 0.0f;
    bool  storyCrashed = false;                 // the prologue's live crash has happened
    int   storyRules = 0;                       // the rules card: 0 not yet, 1 showing, 2 seen (scene) / 1 open (list)
    // The story plays inside every mode: before each race (vs CPU, split screen, online) the
    // scene of the map's chapter at the grid, skippable; vs CPU also the rival's reaction after
    sim::GameSim::GameMode storyMode = sim::GameSim::ModeSingle;
    bool  netStoryHold = false;                 // online: the race waits for the story scene (S_STORY_END)
    bool  netStoryWatched = false;              // online: we told the server we have watched it
    void  beginRaceWithStory(sim::GameSim::GameMode mode);
    void  beginOnlineStory();
    void  endOnlineStory();
    int   chapterForMap(int map) const;
    void  storySkipAll();                       // [ESC] in a scene (online: for everyone)
    void  drawNetStoryWait(int W, int H, float dpr);

    // ---- The player profile (glwidget_profile.cpp, shared/playermodel.*) ----
    bool  profileOpen = false;                  // the MY PROFILE screen over the menu
    bool  profileSaved = false;                 // this race is already in the profile
    bool  showAiDebug = false;                  // F3: the player model's live view of you
    bool  showCpuMinds = false;                 // F4: each CPU's personality, mood, tactic and skill on its plate
    bool  tauntsOn = true;                      // the TAUNTS menu item (saved)
    QByteArray tauntText;                       // a named driver's line to you, shown for a moment
    float tauntCol[3] = {1, 1, 1};
    int   tauntUntil = -1;
    QByteArray rivalSummary;                    // after a race: your rival and the head-to-head
    void  drawTauntBubble(int vw, int vh, float ps);
    void  drawRivalLine(int W, int H, float dpr);
    float cpuSkillFor(const sim::PlayerProfile &p) const;   // the skill the CPUs spread around
    static sim::PlayerHints hintsFor(const sim::PlayerProfile &p, int map);   // what the director may know
    sim::PlayerProfile shownProfile;            // (what MY PROFILE shows, read when it opens)
    std::string profileName(int player) const;  // 0: you, 1: player 2 in split screen
    sim::PlayerProfile loadProfile(const std::string &name) const;
    void  saveProfile(const sim::PlayerProfile &p) const;
    void  profileAfterRace();

    // ---- The clone of your driving (glwidget_profile.cpp, shared/clone.*) ----
    bool  cloneOn = true;                       // the CLONE menu item (saved): your clone races vs CPU
    sim::CloneBook cloneBook;                   // your recordings (read from disk at each race start)
    sim::CloneRecorder cloneRec;
    bool  cloneRecording = false;               // this race is being recorded (vs CPU, offline)
    sim::Brain cloneBrain{};                    // your trained clone ...
    sim::CloneStats cloneStats;                 // ... and how well it copies you
    bool  cloneReady = false;
    bool  cloneResetArmed = false;              // MY PROFILE: R pressed once (again deletes it)
    std::thread cloneThread;                    // trains after a race, in the background
    std::atomic<bool> cloneTraining{false}, cloneDone{false};
    sim::Brain cloneNew{};
    sim::CloneStats cloneNewStats;
    QByteArray cloneSummary;                    // after a race: what your clone learned
    std::string cloneName;                      // (scripted tests: whose clone; empty: yours)
    std::string cloneOwner() const { return cloneName.empty() ? profileName(0) : cloneName; }
    void  cloneLoad();
    void  cloneSetupRace(int mode);
    void  cloneAfterRace();
    void  clonePoll();
    void  cloneReset();
    int   cloneLastRace = -1;                   // the race just recorded (its samples in cloneBook), -1: none

    // ---- Your account on the game server (glwidget_account.cpp) ----
    enum class AccountScreen { Off, Login, Create };
    AccountScreen accountScreen = AccountScreen::Off;   // the LOG IN / CREATE ACCOUNT screen
    QString acctUser, acctPass, acctPass2;              // what is typed (the password is dropped after use)
    bool  acctShare = false;                            // CREATE: share my races for training
    int   acctRow = 0;
    QString acctStatus;                                 // the last answer or problem
    bool  acctStatusBad = false, acctBusy = false;
    struct AcctHit { float x0, y0, x1, y1; int row; };
    std::vector<AcctHit> acctHits;                      // (mouse: where each row was drawn)
    QString accountUser;                                // logged in as ("": not logged in)
    QString accountToken;                               // the session token (never the password)
    bool  accountShare = false;
    enum class SyncState { Off, Syncing, Synced, Pending };
    SyncState syncState = SyncState::Off;
    class QNetworkAccessManager *api = nullptr;
    QString apiBase() const;
    QString acctApp() const;
    void  apiCall(const char *method, const QString &path, const QJsonObject &body,
                  std::function<void(int, const QJsonObject &)> done);
    void  accountLoad();
    void  accountForget();
    void  accountLoggedIn(const QString &user, const QString &token, bool share);
    void  accountLogout();
    void  accountSyncAfterLogin();
    void  accountPushAll();
    void  accountSyncDone();
    void  accountSyncFailed();
    void  openAccountScreen(bool create);
    void  accountSubmit();
    bool  handleAccountKey(QKeyEvent *event);
    bool  accountClick(float mx, float my);
    void  drawAccountScreen(int W, int H, float dpr);
    void  drawShortcutGate();                   // the shortcut closed to you (top 3): a barrier across it
    QByteArray accountLine() const;
    // races up, the leaderboard down (B3)
    bool  raceFlushing = false;
    void  accountRaceUpload(const QJsonObject &race);   // queued in a file first: sent when the server answers
    void  accountFlushRaces();
    void  accountSetShare(bool on);
    bool  boardOpen = false;                     // the LEADERBOARD screen
    int   boardMap = 0;
    bool  boardBusy = false;
    QString boardStatus;
    struct BoardRow { int rank; QString user; float best; int races; };
    std::vector<BoardRow> boardRows;
    void  openLeaderboard(int map);
    void  drawLeaderboard(int W, int H, float dpr);
    void  drawProfileScreen(int W, int H, float dpr);
    void  drawAiDebug(int W, int H, float dpr);
    void  drawCpuMinds(int W, int H, float dpr);   // F4
    void  drawStoryRules(int W, int H, float dpr, int chapter);
    struct Actor { FigureLook look; FigurePose pose; float x = 0, y = 0, z = 0, heading = 0; int who = 0; };
    std::vector<Actor> actors;                  // characters standing in a scene
    std::vector<int> driverOut;                 // cars whose driver is out, standing in the scene
    bool  hideHud = false;
    int   storyKeep[4] = {0, 0, 0, 0};          // your menu choices (map, CPUs, difficulty, laps), put back after
    void openStory();
    void leaveStory();
    void storyDressCars();
    void storySetupRace();
    void storyBeginScene(int chapter, int kind);
    int  storyLineCount() const;
    void storyPlaceActors();
    void storyShot(bool snap);
    void storyUpdate(float dt);
    void storyNextLine();
    void storyEndScene();
    void storyRaceOver();
    bool storyGoalMet() const;
    bool handleStoryKey(QKeyEvent *event);
    void drawStoryChapters(int W, int H, float dpr);
    void drawStoryOverlay(int W, int H, float dpr);
    void drawStoryRaceInfo(int W, int H, float dpr);
    // a fixed camera instead of the chase camera (story scenes, screenshots)
    bool camOverride = false;
    float camEye[3] = {0.0f, 0.0f, 0.0f}, camAt[3] = {0.0f, 0.0f, 0.0f};
    struct DamagePopup { int car = 0; int amount = 0; bool fault = false; int born = 0; };
    std::vector<DamagePopup> popups;            // "-18" rising from a car that was hit
    struct HudMsg { QByteArray text; float r = 1, g = 1, b = 1; int until = 0; };
    std::deque<HudMsg> carMsgs[sim::MAX_RACERS];   // what happened to a player's own car
    std::deque<HudMsg> feed;                    // everyone's wrecks, top right
    void pushMsg(std::deque<HudMsg> &q, const QByteArray &text, float r, float g, float b, int ticks);
    void drawTruck(float x, float z, float heading, float spin, float r, float g, float b,
                   const RoadPose &pose = RoadPose());
    void drawWheel(float x, float y, float z, float steerDeg, float spin, float side, float radius);
    void updateCarFx(int idx, const sim::Car &c);
    void drawSkidMarks();
    void drawShadow(float x, float z, float heading, float hw, float hl, const RoadPose &pose = RoadPose(),
                    float fade = 1.0f);
    RoadPose carPose(const sim::Car &c) const;  // on the road, or flying
    void drawRamps();                           // jump ramps: paint, lip, JUMP boards
    void drawHUD(const sim::Car &me, const sim::Car &other, int playerNum, int vw, int vh);
    void drawMinimap(int W, int H, float dpr);
    void drawShowcaseScene(int W, int H);
    void drawMainMenu(int W, int H, float dpr);
    void drawMenuModal(int W, int H, float dpr);
    void drawPauseOverlay(int W, int H, float dpr);
    void drawFinishOverlay(int W, int H, float dpr);
    void drawText(const char *s, float x, float y, float ps, float r, float g, float b);
    float textWidth(const char *s, float ps) const;
    // text size at which s fits into maxWidth (never bigger than ps): long names get smaller
    float fitSize(const char *s, float ps, float maxWidth) const;
};

#endif // GLWIDGET_H
