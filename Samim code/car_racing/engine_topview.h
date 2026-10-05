#ifndef ENGINE_TOPVIEW_H
#define ENGINE_TOPVIEW_H

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <vector>
#include <string>
#include "raster_canvas.h"
#include "track_topview.h"
#include "car_topview.h"
#include "rival_topview.h"

enum EngineState {
    STATE_COUNTDOWN    = 0,
    STATE_RACING       = 1,
    STATE_CRASH        = 2,
    STATE_ROUND_CLEAR  = 3, // Finished in Top 3 -> Win & Advance!
    STATE_ROUND_FAILED = 4, // Finished P4 or P5 -> Failed round!
    STATE_GAME_OVER    = 5, // All 3 lives lost
    STATE_PAUSED       = 6
};

enum CameraMode {
    CAM_FOLLOW_WORLD   = 0, // Smooth follow, car rotates in world coordinates
    CAM_TRACK_UP       = 1  // Camera rotates with car (car always points UP)
};

struct CompetitorRank {
    std::string name;
    int carIdx;
    double totalDistance;
    int position;
    bool isPlayer;
    uint32_t color;
    double gapToLeader;
};

struct StuntPopup {
    std::string text;
    uint32_t color;
    double duration;
    double maxDuration;
};

struct SkidSegment {
    Vec2D p0;
    Vec2D p1;
    double alpha;
};

struct Particle {
    Vec2D pos;
    Vec2D vel;
    double life;
    double maxLife;
    uint32_t color;
    double size;
};

class TopViewEngine : public QObject {
    Q_OBJECT
public:
    explicit TopViewEngine(RasterCanvas *canvas, QObject *parent = nullptr);
    ~TopViewEngine() override;

    void start();
    void stop();

private slots:
    void onTick();
    void onKeyPressed(int key);

private:
    RasterCanvas *canvas;
    QTimer *timer;
    QElapsedTimer frameTimer;
    qint64 lastFrameTimeUs;
    double currentFps;

    // Simulation
    TopViewTrack track;
    TopViewCar playerCar;
    std::vector<TopViewRival> rivals;

    // Camera
    CameraMode cameraMode;
    Vec2D cameraPos;
    double cameraHeading;

    // Game state
    EngineState gameState;
    double countdownTimer;
    int activeCountdownLights;
    int playerLives;
    int currentLap;
    int totalLaps;
    double currentLapTime;
    double bestLapTime;
    double totalRaceTime;
    double playerStartTrackDist;
    double lastPlayerDistAlongTrack;
    bool isCrashed;
    double crashTimer;
    double screenShake;
    bool showDebugOverlay;

    // Leaderboard
    std::vector<CompetitorRank> leaderboard;
    int playerRank;

    // Skid marks & Particles
    std::vector<SkidSegment> skidMarks;
    std::vector<Particle> particles;
    Vec2D lastLeftTirePos;
    Vec2D lastRightTirePos;
    bool hadPreviousTirePos;

    // Stunt popups
    std::vector<StuntPopup> activePopups;
    void addPopup(const std::string &text, uint32_t col, double dur = 2.2);

    // Initialization & Logic
    void initRace(int trackId);
    void updatePhysics(double dt);
    void updateLeaderboard();
    void spawnCollisionSparks(const Vec2D &pos, const Vec2D &normal);
    void spawnDriftSmoke(const Vec2D &pos);

    // Rendering pipeline
    void renderFrame();
    void renderSkidMarks(uint32_t *fb, int fbW, int fbH, const Mat2D &viewMatrix);
    void renderParticles(uint32_t *fb, int fbW, int fbH, const Mat2D &viewMatrix);
    void renderHUD(uint32_t *fb, int fbW, int fbH);
    void renderMinimap(uint32_t *fb, int fbW, int fbH);
    void renderSpeedometer(uint32_t *fb, int fbW, int fbH);
    void renderDebugOverlay(uint32_t *fb, int fbW, int fbH);
    void renderCountdown(uint32_t *fb, int fbW, int fbH);
    void renderPodiumModal(uint32_t *fb, int fbW, int fbH, bool victory);
    void renderGameOverModal(uint32_t *fb, int fbW, int fbH);
};

#endif // ENGINE_TOPVIEW_H
