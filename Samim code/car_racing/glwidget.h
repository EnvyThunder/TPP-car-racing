#ifndef GLWIDGET_H
#define GLWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QTimer>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QSet>
#include <vector>
#include <deque>
#include <random>
#include <QStringList>

#include "shared/gamesim.h"

class AudioEngine;
class RaceServer;
class RaceClient;
struct GLUquadric;

// Client-side camera and rendering view state for a vehicle
struct CarView {
    float camHeading = 0.0f;      // Smoothed heading for the chase camera
    float fov = 64.0f;            // Dynamic field of view (widens with speed/nitro)
    float shake = 0.0f;           // Camera shake after collisions
    int   camMode = 0;            // 0: Chase, 1: Hood, 2: Bumper
    float camDist = 8.0f;         // Smoothed chase distance (pulls back with speed)
};

// Client-side visual state per car (brake lights, skid-mark trails)
struct CarFx {
    float prevSpeed = 0.0f;
    float brakeGlow = 0.0f;       // 0..1, drives the brake-light brightness
    bool  marking = false;        // Rear tyres currently laying rubber
    float lastX[2] = {0.0f, 0.0f}, lastZ[2] = {0.0f, 0.0f};
};

// Tyre mark segment left on the asphalt while drifting / braking hard
struct SkidMark {
    float x0, z0, x1, z1;
    float alpha;
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
};

class GLWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    using GameMode = sim::GameSim::GameMode;
    using State = sim::GameSim::State;

    explicit GLWidget(QWidget *parent = nullptr);
    ~GLWidget() override;

protected:
    void initializeGL() override;
    void paintGL() override;
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

    std::vector<Particle> particles;
    CarFx carFx[sim::MAX_RACERS];
    std::vector<SkidMark> skidMarks;

    // ---- Online multiplayer ("ONLINE MULTIPLAYER (8P)") -- glwidget_online.cpp ----
    enum class NetScreen { Off, Choose, EnterCode, Busy, Lobby, Race };
    NetScreen netScreen = NetScreen::Off;
    int netChoice = 0;                     // Choose screen: 0 host, 1 join, 2 back
    QString joinText;                      // room code being typed
    QString netMessage;                    // status or error line
    RaceServer *hostServer = nullptr;      // room server running inside this game while hosting
    RaceClient *net = nullptr;
    int mySlot = 0;                        // my car in the online race
    QStringList racerNames;
    std::vector<sim::SimEvent> netEvents;  // sounds/particles sent by the server
    struct SentInput { quint32 seq; sim::PlayerInput in; };
    std::deque<SentInput> sentInputs;      // inputs the server hasn't confirmed yet
    quint32 inputSeq = 0;
    sim::Car predicted;                    // my car, simulated locally so it reacts instantly
    float corrX = 0.0f, corrZ = 0.0f;      // smooths out small prediction corrections
    struct Sample { qint64 t; std::vector<sim::Car> cars; };
    std::deque<Sample> history;            // recent snapshots: other cars are drawn slightly in the past
    QElapsedTimer netClock;
    QString netTest;                       // CAR_RACING_NETTEST: scripted online test (see runNetAutoShot)
    int statSnapshots = 0, statSmallFix = 0, statSnaps = 0;   // how often the prediction was corrected
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
    QString playerName() const;
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

    // ---- Input & helper ----
    bool isDown(int key) const { return keys.contains(key); }
    float rnd(float a, float b);
    float roadCenter(float z) const { return simEngine.roadCenter(z); }
    float roadAngle(float z) const { return simEngine.roadAngle(z); }
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
    void drawRoad(float camZ);
    void drawArch(float z, bool finish);
    void drawProps(float camZ);
    void drawHouse(int palette);
    void drawPine(bool snow = false);
    void drawCactus();
    void drawRock(int palette, bool mesa);
    void drawBuilding(float heightScale, int palette, int seed);
    void drawSea(float camZ);
    void drawRoundTree();
    void drawPalmTree();
    void drawLighthouse();
    void drawLamp();
    void drawTorch(int palette, float phase);
    void drawObstacles(float camZ);
    void drawNitroPickups(float camZ);
    void drawNitroCanister();
    void drawCar(float x, float z, float heading, float roll, float pitch,
                 float steer, float spin, float r, float g, float b, bool nitro,
                 const char *badge = nullptr, float brake = 0.0f);
    void drawTruck(float x, float z, float heading, float spin, float r, float g, float b);
    void drawWheel(float x, float y, float z, float steerDeg, float spin, float side, float radius);
    void updateCarFx(int idx, const sim::Car &c);
    void drawSkidMarks(float camZ);
    void drawShadow(float x, float z, float heading, float hw, float hl);
    void drawHUD(const sim::Car &me, const sim::Car &other, int playerNum, int vw, int vh);
    void drawMinimap(int W, int H, float dpr);
    void drawShowcaseScene(int W, int H);
    void drawMainMenu(int W, int H, float dpr);
    void drawMenuModal(int W, int H, float dpr);
    void drawPauseOverlay(int W, int H, float dpr);
    void drawFinishOverlay(int W, int H, float dpr);
    void drawText(const char *s, float x, float y, float ps, float r, float g, float b);
    float textWidth(const char *s, float ps) const;
};

#endif // GLWIDGET_H
