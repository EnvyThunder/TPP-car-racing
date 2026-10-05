#include "engine_topview.h"
#include <cmath>
#include <iomanip>
#include <sstream>
#include <algorithm>

TopViewEngine::TopViewEngine(RasterCanvas *c, QObject *parent)
    : QObject(parent),
      canvas(c),
      timer(new QTimer(this)),
      lastFrameTimeUs(16666),
      currentFps(60.0),
      cameraMode(CAM_FOLLOW_WORLD),
      cameraPos(0.0, 0.0),
      cameraHeading(0.0),
      gameState(STATE_COUNTDOWN),
      countdownTimer(3.0),
      activeCountdownLights(0),
      playerLives(3),
      currentLap(1),
      totalLaps(3),
      currentLapTime(0.0),
      bestLapTime(0.0),
      totalRaceTime(0.0),
      playerStartTrackDist(0.0),
      lastPlayerDistAlongTrack(0.0),
      isCrashed(false),
      crashTimer(0.0),
      screenShake(0.0),
      showDebugOverlay(false),
      playerRank(1),
      hadPreviousTirePos(false) {

    connect(timer, &QTimer::timeout, this, &TopViewEngine::onTick);
    connect(canvas, &RasterCanvas::keyPressedSignal, this, &TopViewEngine::onKeyPressed);

    initRace(1);
}

TopViewEngine::~TopViewEngine() {
    stop();
}

void TopViewEngine::start() {
    frameTimer.start();
    timer->start(16); // ~60 FPS
}

void TopViewEngine::stop() {
    timer->stop();
}

void TopViewEngine::addPopup(const std::string &text, uint32_t col, double dur) {
    if (!activePopups.empty() && activePopups.back().text == text) {
        activePopups.back().duration = dur;
        return;
    }
    activePopups.push_back({ text, col, dur, dur });
    if (activePopups.size() > 4) {
        activePopups.erase(activePopups.begin());
    }
}

void TopViewEngine::initRace(int trackId) {
    track.loadTrack(trackId);

    // Player in Pole Position (Grid 0)
    Vec2D pStart = track.getGridPosition(0);
    playerCar.reset(pStart, track.getStartHeading());
    playerCar.setSpeedScale(1.0); // Full 100% speed capability for player
    cameraPos = pStart;
    cameraHeading = track.getStartHeading();
    playerStartTrackDist = track.getDistanceAlongTrack(pStart);
    lastPlayerDistAlongTrack = playerStartTrackDist;

    // 4 AI Rivals placed across the wide 4-lane starting grid
    rivals.clear();
    rivals.emplace_back("VORTEX",  1, 0.0, -135.0, 1.05); // Car 1: Venom Lime (Lane 1)
    rivals.emplace_back("TITAN",   2, 0.0,  -48.0, 1.00); // Car 2: Chiron Blue (Lane 2)
    rivals.emplace_back("BLAZE",   3, 0.0,   48.0, 1.05); // Car 3: Sunfire Orange (Lane 3)
    rivals.emplace_back("PHANTOM", 4, 0.0,  135.0, 1.05); // Car 4: Ghost Violet (Lane 4)

    for (size_t i = 0; i < rivals.size(); i++) {
        rivals[i].setLevel(trackId);
        rivals[i].reset(track, static_cast<int>(i + 1));
    }

    totalLaps = track.getTotalLaps(); // 1 lap for levels 1-3, 3 laps for levels 4-5
    gameState = STATE_COUNTDOWN;
    countdownTimer = 3.0;
    activeCountdownLights = 0;
    currentLap = 1;
    currentLapTime = 0.0;
    totalRaceTime = 0.0;
    isCrashed = false;
    crashTimer = 0.0;
    screenShake = 0.0;
    playerLives = 3;
    playerCar.setLives(3);

    skidMarks.clear();
    particles.clear();
    activePopups.clear();
    hadPreviousTirePos = false;

    if (trackId == 1) {
        addPopup("LEVEL 1: DESERT SPRINT | PURE STRAIGHT | OPPONENTS VERY SLOW", rgba(34, 197, 94), 3.5);
    } else if (trackId == 2) {
        addPopup("LEVEL 2: COASTAL HIGHWAY | GENTLE CURVES (NON-CIRCULAR) | 1 LAP", rgba(56, 189, 248), 3.5);
    } else if (trackId == 3) {
        addPopup("LEVEL 3: ALPINE CANYON | WINDING S-CURVES (NON-CIRCULAR) | 1 LAP", rgba(250, 204, 21), 3.5);
    } else if (trackId == 4) {
        addPopup("LEVEL 4: MEADOW GP CIRCUIT | CLOSED LOOP (3 LAPS TO WIN!)", rgba(249, 115, 22), 3.5);
    } else {
        addPopup("LEVEL 5: NEO TOKYO EXPRESSWAY | CHAMPIONSHIP LOOP (3 LAPS TO WIN!)", rgba(239, 68, 68), 3.5);
    }
    updateLeaderboard();
}

void TopViewEngine::onKeyPressed(int key) {
    if (gameState == STATE_ROUND_CLEAR && (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space)) {
        int nextTrack = (track.getCurrentTrackId() % 5) + 1;
        initRace(nextTrack);
        return;
    }
    if ((gameState == STATE_ROUND_FAILED || gameState == STATE_GAME_OVER) && (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space)) {
        initRace(track.getCurrentTrackId());
        return;
    }

    if (key >= Qt::Key_1 && key <= Qt::Key_5) {
        playerCar.selectCar(key - Qt::Key_1);
        std::string msg = "SELECTED: " + playerCar.getSpec().name;
        addPopup(msg, rgba(56, 189, 248), 1.8);
    } else if (key == Qt::Key_Tab) {
        int nextCar = (playerCar.getCurrentCarIndex() + 1) % playerCar.getCarCount();
        playerCar.selectCar(nextCar);
        std::string msg = "SELECTED: " + playerCar.getSpec().name;
        addPopup(msg, rgba(56, 189, 248), 1.8);
    } else if (key == Qt::Key_L) {
        int nextTrack = (track.getCurrentTrackId() % 5) + 1;
        initRace(nextTrack);
    } else if (key == Qt::Key_C) {
        cameraMode = (cameraMode == CAM_FOLLOW_WORLD) ? CAM_TRACK_UP : CAM_FOLLOW_WORLD;
        std::string camMsg = (cameraMode == CAM_TRACK_UP) ? "CAMERA: TRACK-UP ROTATION" : "CAMERA: WORLD-FOLLOW";
        addPopup(camMsg, rgba(168, 85, 247), 1.6);
    } else if (key == Qt::Key_R) {
        initRace(track.getCurrentTrackId());
    } else if (key == Qt::Key_P) {
        if (gameState == STATE_PAUSED) {
            gameState = STATE_RACING;
        } else if (gameState == STATE_RACING) {
            gameState = STATE_PAUSED;
        }
    } else if (key == Qt::Key_F1) {
        showDebugOverlay = !showDebugOverlay;
    }
}

void TopViewEngine::onTick() {
    qint64 nowUs = frameTimer.nsecsElapsed() / 1000;
    qint64 deltaUs = nowUs - lastFrameTimeUs;
    lastFrameTimeUs = nowUs;

    double dt = static_cast<double>(deltaUs) / 1000000.0;
    dt = std::max(0.001, std::min(0.05, dt));
    currentFps = 0.95 * currentFps + 0.05 * (1.0 / dt);

    if (screenShake > 0.0) {
        screenShake = std::max(0.0, screenShake - dt * 4.0);
    }

    for (auto it = activePopups.begin(); it != activePopups.end();) {
        it->duration -= dt;
        if (it->duration <= 0.0) {
            it = activePopups.erase(it);
        } else {
            ++it;
        }
    }

    if (gameState == STATE_COUNTDOWN) {
        countdownTimer -= dt;
        activeCountdownLights = 3 - static_cast<int>(countdownTimer);
        if (countdownTimer <= 0.0) {
            gameState = STATE_RACING;
            addPopup("GO! FULL THROTTLE!", rgba(34, 197, 94), 2.0);
        }
    } else if (gameState == STATE_RACING) {
        updatePhysics(dt);
    }

    for (auto it = skidMarks.begin(); it != skidMarks.end();) {
        it->alpha -= dt * 0.015;
        if (it->alpha <= 0.0) {
            it = skidMarks.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = particles.begin(); it != particles.end();) {
        it->life -= dt;
        it->pos = it->pos + it->vel * dt;
        if (it->life <= 0.0) {
            it = particles.erase(it);
        } else {
            ++it;
        }
    }

    track.update(dt);

    // Smooth Camera Follow
    Vec2D targetCamPos = playerCar.getPos() + playerCar.getVelocity() * 0.22;
    cameraPos = cameraPos + (targetCamPos - cameraPos) * std::min(1.0, dt * 8.0);

    if (cameraMode == CAM_TRACK_UP) {
        double headingDiff = playerCar.getHeading() - cameraHeading;
        const double PI = 3.141592653589793;
        const double TWO_PI = 6.283185307179586;
        while (headingDiff > PI)  headingDiff -= TWO_PI;
        while (headingDiff < -PI) headingDiff += TWO_PI;
        cameraHeading += headingDiff * std::min(1.0, dt * 9.0);
    } else {
        cameraHeading = 0.0;
    }

    renderFrame();
    canvas->update();
}

void TopViewEngine::updatePhysics(double dt) {
    currentLapTime += dt;
    totalRaceTime += dt;

    bool up    = canvas->isKeyPressed(Qt::Key_Up)    || canvas->isKeyPressed(Qt::Key_W);
    bool down  = canvas->isKeyPressed(Qt::Key_Down)  || canvas->isKeyPressed(Qt::Key_S);
    bool left  = canvas->isKeyPressed(Qt::Key_Left)  || canvas->isKeyPressed(Qt::Key_A);
    bool right = canvas->isKeyPressed(Qt::Key_Right) || canvas->isKeyPressed(Qt::Key_D);
    bool nitro = canvas->isKeyPressed(Qt::Key_Space);
    bool drift = canvas->isKeyPressed(Qt::Key_Shift);

    // Off-road check
    double distFromCenter = 0.0;
    bool onTrack = track.isPointOnTrack(playerCar.getPos(), distFromCenter);
    playerCar.setOffRoad(!onTrack);

    playerCar.update(dt, up, down, left, right, drift, nitro);

    // Rubber skid marks
    Vec2D forward = Vec2D(std::sin(playerCar.getHeading()), -std::cos(playerCar.getHeading()));
    Vec2D rDir    = Vec2D(std::cos(playerCar.getHeading()),  std::sin(playerCar.getHeading()));
    Vec2D rearLeftTire  = playerCar.getPos() - forward * 17.0 - rDir * 14.5;
    Vec2D rearRightTire = playerCar.getPos() - forward * 17.0 + rDir * 14.5;

    if (playerCar.isDriftingActive() || (down && playerCar.getSpeed() > 180.0) || playerCar.isNitroActive()) {
        if (hadPreviousTirePos) {
            skidMarks.push_back({ lastLeftTirePos, rearLeftTire, 0.85 });
            skidMarks.push_back({ lastRightTirePos, rearRightTire, 0.85 });
            if (skidMarks.size() > 400) {
                skidMarks.erase(skidMarks.begin(), skidMarks.begin() + 20);
            }
        }
        spawnDriftSmoke(rearLeftTire);
        spawnDriftSmoke(rearRightTire);
    }
    lastLeftTirePos = rearLeftTire;
    lastRightTirePos = rearRightTire;
    hadPreviousTirePos = true;

    // Smooth Barrier Slide
    Vec2D pushOut(0.0, 0.0);
    if (track.checkBarrierCollision(playerCar.getPos(), playerCar.getCollisionRadius(), pushOut)) {
        playerCar.setPos(playerCar.getPos() + pushOut);
        Vec2D tangent = track.getTangentAtPoint(playerCar.getPos());
        playerCar.slideAlongBarrier(tangent, pushOut.length());

        spawnCollisionSparks(playerCar.getPos(), pushOut.normalized());

        // Only damage on severe high-speed impact
        if (playerCar.getSpeed() > 240.0 && !playerCar.isInvulnerableActive()) {
            playerCar.takeDamage();
            playerLives = playerCar.getLives();
            screenShake = 0.6;
            addPopup("BARRIER SCRAPE! -1 LIFE", rgba(239, 68, 68), 1.8);
            if (playerLives <= 0) {
                gameState = STATE_GAME_OVER;
                return;
            }
        }
    }

    // Boost Pad Trigger
    if (track.checkBoostPad(playerCar.getPos(), playerCar.getCollisionRadius())) {
        playerCar.triggerBoostPad();
        addPopup("HYPER BOOST PAD! +45% SPEED", rgba(234, 179, 8), 1.8);
        screenShake = 0.3;
    }

    // Oil Slick Trigger
    if (track.checkOilSlick(playerCar.getPos(), playerCar.getCollisionRadius())) {
        playerCar.setVelocity(playerCar.getVelocity().rotated(0.25));
        addPopup("OIL SLICK! TRACTION LOSS", rgba(168, 85, 247), 1.5);
        spawnDriftSmoke(playerCar.getPos());
    }

    // Nitro Pickup Canister
    if (track.checkNitroPickup(playerCar.getPos(), playerCar.getCollisionRadius())) {
        playerCar.addNitro(35.0);
        addPopup("NITRO CANISTER! +35%", rgba(6, 182, 212), 1.8);
    }

    // AI Rivals Update & Comprehensive Bump / Crash Collisions
    std::vector<Vec2D> otherCarsPos;
    otherCarsPos.push_back(playerCar.getPos());
    for (auto &r : rivals) {
        otherCarsPos.push_back(r.getCar().getPos());
    }

    for (auto &r : rivals) {
        r.update(dt, track, otherCarsPos);
    }

    // 1. Player vs Rival Collisions
    for (auto &r : rivals) {
        Vec2D diff = playerCar.getPos() - r.getCar().getPos();
        double dist = diff.length();
        double minDist = playerCar.getCollisionRadius() + r.getCar().getCollisionRadius();

        if (dist > 0.01 && dist < minDist) {
            Vec2D normal = diff.normalized();
            double overlap = minDist - dist;

            playerCar.setPos(playerCar.getPos() + normal * (overlap * 0.55));
            r.getCar().setPos(r.getCar().getPos() - normal * (overlap * 0.55));

            playerCar.setVelocity(playerCar.getVelocity() + normal * 65.0);
            r.getCar().setVelocity(r.getCar().getVelocity() - normal * 65.0);

            // Both Player and Rival experience crash slowdown!
            playerCar.applyCrashSlowdown(0.72);
            r.getCar().applyCrashSlowdown(0.72);
            r.triggerCrashWobble(-normal);

            spawnCollisionSparks((playerCar.getPos() + r.getCar().getPos()) * 0.5, normal);
            screenShake = std::max(screenShake, 0.45);

            if (playerCar.isNitroActive()) {
                addPopup("NITRO IMPACT BUMP! OPPONENT SLOWED", rgba(245, 158, 11), 1.6);
            } else {
                addPopup("CAR COLLISION! SPEED REDUCED", rgba(239, 68, 68), 1.4);
            }
        }
    }

    // 2. Rival vs Rival Collisions (All pairs i < j)
    for (size_t i = 0; i < rivals.size(); i++) {
        for (size_t j = i + 1; j < rivals.size(); j++) {
            Vec2D diff = rivals[i].getCar().getPos() - rivals[j].getCar().getPos();
            double dist = diff.length();
            double minDist = rivals[i].getCar().getCollisionRadius() + rivals[j].getCar().getCollisionRadius();

            if (dist > 0.01 && dist < minDist) {
                Vec2D normal = diff.normalized();
                double overlap = minDist - dist;

                // Push rivals apart
                rivals[i].getCar().setPos(rivals[i].getCar().getPos() + normal * (overlap * 0.55));
                rivals[j].getCar().setPos(rivals[j].getCar().getPos() - normal * (overlap * 0.55));

                // Exchange collision impulse
                rivals[i].getCar().setVelocity(rivals[i].getCar().getVelocity() + normal * 60.0);
                rivals[j].getCar().setVelocity(rivals[j].getCar().getVelocity() - normal * 60.0);

                // Both rivals slow down upon crashing into each other!
                rivals[i].getCar().applyCrashSlowdown(0.70);
                rivals[j].getCar().applyCrashSlowdown(0.70);

                // Trigger crash wobble and recovery pause
                rivals[i].triggerCrashWobble(normal);
                rivals[j].triggerCrashWobble(-normal);

                Vec2D contactPt = (rivals[i].getCar().getPos() + rivals[j].getCar().getPos()) * 0.5;
                spawnCollisionSparks(contactPt, normal);

                // If collision happens near player view, shake camera and alert player
                double distToPlayer = (contactPt - playerCar.getPos()).length();
                if (distToPlayer < 750.0) {
                    screenShake = std::max(screenShake, 0.25);
                    std::string crashAlert = "RIVAL CRASH! " + rivals[i].getName() + " & " + rivals[j].getName() + " SLOW DOWN!";
                    addPopup(crashAlert, rgba(249, 115, 22), 1.8);
                }
            }
        }
    }

    // Lap Counting & Finish Line Crossing
    double trackLen = track.getTrackLength();
    double currentDist = track.getDistanceAlongTrack(playerCar.getPos());

    if (!track.isLoop()) {
        // Point-to-point sprint track (Level 1, Level 2, Level 3)
        // Finish line is located near the end of track (trackLen - 60 px)
        if (currentDist >= trackLen - 120.0) {
            updateLeaderboard();
            if (playerRank <= 3) {
                gameState = STATE_ROUND_CLEAR;
            } else {
                gameState = STATE_ROUND_FAILED;
            }
            return;
        }
    } else {
        // Closed loop circuit (Level 4, Level 5)
        if (lastPlayerDistAlongTrack > trackLen * 0.85 && currentDist < trackLen * 0.15) {
            if (bestLapTime == 0.0 || currentLapTime < bestLapTime) {
                bestLapTime = currentLapTime;
                addPopup("NEW BEST LAP!", rgba(34, 197, 94), 2.5);
            }

            currentLap++;
            currentLapTime = 0.0;

            if (currentLap > totalLaps) {
                updateLeaderboard();
                if (playerRank <= 3) {
                    gameState = STATE_ROUND_CLEAR;
                } else {
                    gameState = STATE_ROUND_FAILED;
                }
                return;
            } else {
                std::string lapMsg = "LAP " + std::to_string(currentLap) + " / " + std::to_string(totalLaps);
                addPopup(lapMsg, rgba(250, 204, 21), 2.2);
            }
        }
    }
    lastPlayerDistAlongTrack = currentDist;

    updateLeaderboard();
}

void TopViewEngine::updateLeaderboard() {
    double trackLen = track.getTrackLength();
    leaderboard.clear();

    double playerDist = 0.0;
    if (track.isLoop()) {
        playerDist = (currentLap - 1) * trackLen + track.getDistanceAlongTrack(playerCar.getPos());
    } else {
        double d = track.getDistanceAlongTrack(playerCar.getPos());
        if (d >= trackLen - 120.0) {
            playerDist = trackLen + d;
        } else {
            playerDist = d;
        }
    }

    leaderboard.push_back({
        "PLAYER",
        playerCar.getCurrentCarIndex(),
        playerDist,
        1,
        true,
        playerCar.getSpec().primaryColor,
        0.0
    });

    for (const auto &r : rivals) {
        leaderboard.push_back({
            r.getName(),
            r.getCarIndex(),
            r.getTotalDistance(trackLen, track.isLoop()),
            1,
            false,
            r.getColor(),
            0.0
        });
    }

    std::sort(leaderboard.begin(), leaderboard.end(), [](const CompetitorRank &a, const CompetitorRank &b) {
        return a.totalDistance > b.totalDistance;
    });

    for (size_t i = 0; i < leaderboard.size(); i++) {
        leaderboard[i].position = static_cast<int>(i + 1);
        if (leaderboard[i].isPlayer) {
            playerRank = leaderboard[i].position;
        }
        if (i > 0) {
            leaderboard[i].gapToLeader = (leaderboard[0].totalDistance - leaderboard[i].totalDistance) / 100.0;
        }
    }
}

void TopViewEngine::spawnCollisionSparks(const Vec2D &pos, const Vec2D &normal) {
    for (int i = 0; i < 18; i++) {
        double angle = ((rand() % 360) * 3.14159) / 180.0;
        double spd = 60.0 + (rand() % 160);
        Vec2D vel = (normal + Vec2D(std::cos(angle), std::sin(angle)) * 0.6).normalized() * spd;
        uint32_t col = (i % 2 == 0) ? rgba(250, 204, 21) : rgba(249, 115, 22);
        particles.push_back({ pos, vel, 0.35 + (rand() % 20) * 0.01, 0.45, col, 2.0 });
    }
}

void TopViewEngine::spawnDriftSmoke(const Vec2D &pos) {
    if (rand() % 2 == 0) {
        double angle = ((rand() % 360) * 3.14159) / 180.0;
        Vec2D vel = Vec2D(std::cos(angle), std::sin(angle)) * (10.0 + (rand() % 25));
        uint32_t col = rgba(226, 232, 240, 160);
        particles.push_back({ pos, vel, 0.5 + (rand() % 20) * 0.01, 0.6, col, 4.0 });
    }
}

void TopViewEngine::renderFrame() {
    uint32_t *fb = canvas->getBuffer();
    int fbW = canvas->getCanvasWidth();
    int fbH = canvas->getCanvasHeight();

    Mat2D toCenter = Mat2D::translation(-cameraPos.x, -cameraPos.y);
    Mat2D rot = (cameraMode == CAM_TRACK_UP) ? Mat2D::rotation(-cameraHeading) : Mat2D::identity();
    Mat2D toScreenCenter = Mat2D::translation(fbW / 2.0, fbH / 2.0);

    Mat2D viewMatrix = Mat2D::multiply(toScreenCenter, Mat2D::multiply(rot, toCenter));

    if (screenShake > 0.0) {
        int sx = (rand() % 7) - 3;
        int sy = (rand() % 7) - 3;
        viewMatrix = Mat2D::multiply(Mat2D::translation(sx, sy), viewMatrix);
    }

    // 1. Render Wide 4-Lane Track
    track.render(fb, fbW, fbH, viewMatrix, cameraPos, 1600.0);

    // 2. Render Skid Marks
    renderSkidMarks(fb, fbW, fbH, viewMatrix);

    // 3. Render Particles
    renderParticles(fb, fbW, fbH, viewMatrix);

    // 4. Render AI Competitors
    for (auto &r : rivals) {
        r.render(fb, fbW, fbH, viewMatrix);
    }

    // 5. Render Player Supercar in Top View
    playerCar.render(fb, fbW, fbH, viewMatrix, true, true);

    // 6. HUD & Telemetry
    renderHUD(fb, fbW, fbH);

    // 7. Full Circuit Minimap Radar
    renderMinimap(fb, fbW, fbH);

    // 8. Speedometer & Tachometer
    renderSpeedometer(fb, fbW, fbH);

    // 9. Modals
    if (gameState == STATE_COUNTDOWN) {
        renderCountdown(fb, fbW, fbH);
    } else if (gameState == STATE_ROUND_CLEAR) {
        renderPodiumModal(fb, fbW, fbH, true);
    } else if (gameState == STATE_ROUND_FAILED) {
        renderPodiumModal(fb, fbW, fbH, false);
    } else if (gameState == STATE_GAME_OVER) {
        renderGameOverModal(fb, fbW, fbH);
    } else if (gameState == STATE_PAUSED) {
        drawArcadeText(fb, fbW, fbH, fbW / 2 - 80, fbH / 2 - 20, "GAME PAUSED", rgba(250, 204, 21), 2, true);
        drawArcadeText(fb, fbW, fbH, fbW / 2 - 100, fbH / 2 + 15, "PRESS [P] TO RESUME", rgba(241, 245, 249), 1, true);
    }

    // 10. CG Lab Telemetry Overlay
    if (showDebugOverlay) {
        renderDebugOverlay(fb, fbW, fbH);
    }
}

void TopViewEngine::renderSkidMarks(uint32_t *fb, int fbW, int fbH, const Mat2D &viewMatrix) {
    for (const auto &seg : skidMarks) {
        Vec2D s0 = viewMatrix.transform(seg.p0);
        Vec2D s1 = viewMatrix.transform(seg.p1);

        if (s0.x < -20 || s0.x > fbW + 20 || s0.y < -20 || s0.y > fbH + 20) continue;

        uint8_t a = static_cast<uint8_t>(std::min(220.0, seg.alpha * 240.0));
        uint32_t markCol = rgba(17, 24, 39, a);
        drawBresenhamLine(fb, fbW, fbH,
                          static_cast<int>(s0.x), static_cast<int>(s0.y),
                          static_cast<int>(s1.x), static_cast<int>(s1.y),
                          markCol, 3);
    }
}

void TopViewEngine::renderParticles(uint32_t *fb, int fbW, int fbH, const Mat2D &viewMatrix) {
    for (const auto &p : particles) {
        Vec2D s = viewMatrix.transform(p.pos);
        if (s.x < 0 || s.x >= fbW || s.y < 0 || s.y >= fbH) continue;

        double lifeRatio = p.life / p.maxLife;
        uint8_t a = static_cast<uint8_t>(getAlpha(p.color) * lifeRatio);
        uint32_t col = rgba(getRed(p.color), getGreen(p.color), getBlue(p.color), a);

        fillMidpointCircle(fb, fbW, fbH, static_cast<int>(s.x), static_cast<int>(s.y), static_cast<int>(p.size * (1.8 - lifeRatio)), col);
    }
}

void TopViewEngine::renderHUD(uint32_t *fb, int fbW, int fbH) {
    // 1. Top-Left: Position & Mini Leaderboard
    int lbX = 14;
    int lbY = 14;
    int lbW = 195;
    int lbH = 118;

    for (int y = lbY; y < lbY + lbH; y++) {
        fillScanlineBlend(fb, fbW, fbH, y, lbX, lbX + lbW, rgba(15, 23, 42, 210));
    }
    drawBresenhamLine(fb, fbW, fbH, lbX, lbY, lbX + lbW, lbY, rgba(239, 68, 68), 2);
    drawBresenhamLine(fb, fbW, fbH, lbX, lbY + lbH, lbX + lbW, lbY + lbH, rgba(239, 68, 68), 2);

    uint32_t posCol = (playerRank == 1) ? rgba(34, 197, 94) : (playerRank <= 3) ? rgba(250, 204, 21) : rgba(239, 68, 68);
    std::string posStr = "POS " + std::to_string(playerRank) + " / " + std::to_string(leaderboard.size());
    drawArcadeText(fb, fbW, fbH, lbX + 10, lbY + 8, posStr, posCol, 2, true);

    for (size_t i = 0; i < std::min(size_t(5), leaderboard.size()); i++) {
        int rowY = lbY + 32 + static_cast<int>(i * 16);
        const auto &entry = leaderboard[i];
        uint32_t textCol = entry.isPlayer ? rgba(250, 204, 21) : rgba(203, 213, 225);

        std::string rankStr = "P" + std::to_string(entry.position) + " " + entry.name;
        drawArcadeText(fb, fbW, fbH, lbX + 10, rowY, rankStr, textCol, 1, false);

        if (i > 0) {
            std::ostringstream ss;
            ss << "+" << std::fixed << std::setprecision(1) << entry.gapToLeader << "s";
            drawArcadeText(fb, fbW, fbH, lbX + 135, rowY, ss.str(), rgba(148, 163, 184), 1, false);
        } else {
            drawArcadeText(fb, fbW, fbH, lbX + 140, rowY, "LEAD", rgba(34, 197, 94), 1, false);
        }
    }

    // 2. Top-Right: Laps, Timer & 3 Lives Board
    int trW = 260;
    int trH = 88;
    int trX = fbW - trW - 14;
    int trY = 14;

    for (int y = trY; y < trY + trH; y++) {
        fillScanlineBlend(fb, fbW, fbH, y, trX, trX + trW, rgba(15, 23, 42, 210));
    }
    drawBresenhamLine(fb, fbW, fbH, trX, trY, trX + trW, trY, rgba(56, 189, 248), 2);
    drawBresenhamLine(fb, fbW, fbH, trX, trY + trH, trX + trW, trY + trH, rgba(56, 189, 248), 2);

    std::string livesStr = "LIVES: ";
    drawArcadeText(fb, fbW, fbH, trX + 10, trY + 8, livesStr, rgba(241, 245, 249), 1, true);
    for (int i = 0; i < 3; i++) {
        int hX = trX + 68 + (i * 22);
        int hY = trY + 11;
        uint32_t heartCol = (i < playerLives) ? rgba(239, 68, 68) : rgba(71, 85, 105);
        fillMidpointCircle(fb, fbW, fbH, hX - 3, hY - 2, 4, heartCol);
        fillMidpointCircle(fb, fbW, fbH, hX + 3, hY - 2, 4, heartCol);
        fillTriangle(fb, fbW, fbH, hX - 7, hY, hX + 7, hY, hX, hY + 7, heartCol);
    }

    std::string lapStr;
    if (!track.isLoop()) {
        double distAlong = track.getDistanceAlongTrack(playerCar.getPos());
        double trackLen = track.getTrackLength();
        int pct = static_cast<int>(std::max(0.0, std::min(100.0, (distAlong / (trackLen - 120.0)) * 100.0)));
        lapStr = "STAGE: SPRINT " + std::to_string(pct) + "% (1 LAP)";
    } else {
        lapStr = "LAP " + std::to_string(std::min(totalLaps, currentLap)) + " / " + std::to_string(totalLaps);
    }
    drawArcadeText(fb, fbW, fbH, trX + 10, trY + 28, lapStr, rgba(250, 204, 21), 1, true);

    int mins = static_cast<int>(currentLapTime) / 60;
    int secs = static_cast<int>(currentLapTime) % 60;
    int ms = static_cast<int>((currentLapTime - std::floor(currentLapTime)) * 100);
    std::ostringstream ssTime;
    ssTime << "TIME " << std::setfill('0') << std::setw(2) << mins << ":"
           << std::setw(2) << secs << "." << std::setw(2) << ms;
    drawArcadeText(fb, fbW, fbH, trX + 10, trY + 46, ssTime.str(), rgba(241, 245, 249), 1, true);

    drawArcadeText(fb, fbW, fbH, trX + 10, trY + 66, track.getTheme().name, rgba(56, 189, 248), 1, true);

    // 3. Bottom-Center Nitro Tank Bar
    int nbW = 200;
    int nbH = 14;
    int nbX = (fbW - nbW) / 2;
    int nbY = fbH - 32;

    for (int y = nbY; y < nbY + nbH; y++) {
        fillScanlineBlend(fb, fbW, fbH, y, nbX, nbX + nbW, rgba(15, 23, 42, 220));
    }
    drawBresenhamLine(fb, fbW, fbH, nbX, nbY, nbX + nbW, nbY, rgba(148, 163, 184), 1);
    drawBresenhamLine(fb, fbW, fbH, nbX, nbY + nbH, nbX + nbW, nbY + nbH, rgba(148, 163, 184), 1);

    int fillW = static_cast<int>((playerCar.getNitroLevel() / 100.0) * (nbW - 4));
    uint32_t nitroBarCol = playerCar.isNitroActive() ? rgba(6, 182, 212) : rgba(234, 179, 8);
    for (int y = nbY + 2; y < nbY + nbH - 2; y++) {
        fillScanline(fb, fbW, fbH, y, nbX + 2, nbX + 2 + fillW, nitroBarCol);
    }
    drawArcadeText(fb, fbW, fbH, nbX + 50, nbY + 3, "NITRO BOOST [SPACE]", rgba(255, 255, 255), 1, true);

    // 4. Stunt Popups
    int popupY = 140;
    for (const auto &pop : activePopups) {
        int pw = static_cast<int>(pop.text.size() * 8 + 20);
        int px = (fbW - pw) / 2;
        for (int y = popupY; y < popupY + 20; y++) {
            fillScanlineBlend(fb, fbW, fbH, y, px, px + pw, rgba(15, 23, 42, 210));
        }
        drawBresenhamLine(fb, fbW, fbH, px, popupY, px + pw, popupY, pop.color, 1);
        drawBresenhamLine(fb, fbW, fbH, px, popupY + 20, px + pw, popupY + 20, pop.color, 1);
        drawArcadeText(fb, fbW, fbH, px + 10, popupY + 6, pop.text, pop.color, 1, true);
        popupY += 26;
    }
}

void TopViewEngine::renderMinimap(uint32_t *fb, int fbW, int fbH) {
    int mmSize = 130;
    int mmX = 14;
    int mmY = fbH - mmSize - 14;

    for (int y = mmY; y < mmY + mmSize; y++) {
        fillScanlineBlend(fb, fbW, fbH, y, mmX, mmX + mmSize, rgba(15, 23, 42, 210));
    }
    drawBresenhamLine(fb, fbW, fbH, mmX, mmY, mmX + mmSize, mmY, rgba(56, 189, 248), 1);
    drawBresenhamLine(fb, fbW, fbH, mmX, mmY + mmSize, mmX + mmSize, mmY + mmSize, rgba(56, 189, 248), 1);
    drawBresenhamLine(fb, fbW, fbH, mmX, mmY, mmX, mmY + mmSize, rgba(56, 189, 248), 1);
    drawBresenhamLine(fb, fbW, fbH, mmX + mmSize, mmY, mmX + mmSize, mmY + mmSize, rgba(56, 189, 248), 1);

    drawArcadeText(fb, fbW, fbH, mmX + 6, mmY + 5, "GPS RADAR", rgba(148, 163, 184), 1, false);

    // Dynamic track bounds
    double minX = 0, maxX = 2000, minY = 0, maxY = 2000;
    track.getTrackBounds(minX, maxX, minY, maxY);

    double spanX = std::max(100.0, maxX - minX);
    double spanY = std::max(100.0, maxY - minY);
    double scaleX = (mmSize - 24) / spanX;
    double scaleY = (mmSize - 24) / spanY;

    auto worldToMini = [&](const Vec2D &p) -> Vec2D {
        return Vec2D(
            mmX + 12 + (p.x - minX) * scaleX,
            mmY + 12 + (p.y - minY) * scaleY
        );
    };

    const auto &nodes = track.getNodes();
    for (size_t i = 0; i + 2 < nodes.size(); i += 2) {
        Vec2D p0 = worldToMini(nodes[i].center);
        Vec2D p1 = worldToMini(nodes[i + 2].center);
        drawBresenhamLine(fb, fbW, fbH, static_cast<int>(p0.x), static_cast<int>(p0.y),
                          static_cast<int>(p1.x), static_cast<int>(p1.y), rgba(100, 116, 139), 2);
    }
    if (track.isLoop() && nodes.size() > 2) {
        Vec2D p0 = worldToMini(nodes.back().center);
        Vec2D p1 = worldToMini(nodes.front().center);
        drawBresenhamLine(fb, fbW, fbH, static_cast<int>(p0.x), static_cast<int>(p0.y),
                          static_cast<int>(p1.x), static_cast<int>(p1.y), rgba(100, 116, 139), 2);
    } else if (!nodes.empty()) {
        Vec2D startM = worldToMini(nodes.front().center);
        Vec2D finishM = worldToMini(nodes.back().center);
        fillMidpointCircle(fb, fbW, fbH, static_cast<int>(startM.x), static_cast<int>(startM.y), 3, rgba(34, 197, 94));
        fillMidpointCircle(fb, fbW, fbH, static_cast<int>(finishM.x), static_cast<int>(finishM.y), 3, rgba(250, 204, 21));
    }

    for (const auto &r : rivals) {
        Vec2D rM = worldToMini(r.getCar().getPos());
        fillMidpointCircle(fb, fbW, fbH, static_cast<int>(rM.x), static_cast<int>(rM.y), 3, r.getColor());
    }

    Vec2D pM = worldToMini(playerCar.getPos());
    fillMidpointCircle(fb, fbW, fbH, static_cast<int>(pM.x), static_cast<int>(pM.y), 4, rgba(34, 197, 94));
    Vec2D pHead = pM + Vec2D(std::sin(playerCar.getHeading()), -std::cos(playerCar.getHeading())) * 8.0;
    drawBresenhamLine(fb, fbW, fbH, static_cast<int>(pM.x), static_cast<int>(pM.y),
                      static_cast<int>(pHead.x), static_cast<int>(pHead.y), rgba(255, 255, 255), 1);
}

void TopViewEngine::renderSpeedometer(uint32_t *fb, int fbW, int fbH) {
    int dialCX = fbW - 74;
    int dialCY = fbH - 74;
    int radius = 54;

    fillMidpointCircle(fb, fbW, fbH, dialCX, dialCY, radius, rgba(15, 23, 42, 215));
    drawMidpointCircle(fb, fbW, fbH, dialCX, dialCY, radius, rgba(56, 189, 248), 2);

    double rawSpeed = playerCar.getRawSpeed();
    double speedKmH = std::abs(rawSpeed) * 0.85;
    double maxSpeedKmH = playerCar.getSpec().maxSpeed * 0.85;
    double speedRatio = std::min(1.0, speedKmH / maxSpeedKmH);

    double startAngle = 2.356;
    double endAngle = 7.068;
    double currentAngle = startAngle + speedRatio * (endAngle - startAngle);

    for (double a = startAngle; a <= currentAngle; a += 0.05) {
        int ax0 = dialCX + static_cast<int>((radius - 10) * std::cos(a));
        int ay0 = dialCY + static_cast<int>((radius - 10) * std::sin(a));
        int ax1 = dialCX + static_cast<int>((radius - 3) * std::cos(a));
        int ay1 = dialCY + static_cast<int>((radius - 3) * std::sin(a));

        uint32_t tickCol = (a > 6.0) ? rgba(239, 68, 68) : (a > 4.5) ? rgba(234, 179, 8) : rgba(6, 182, 212);
        drawBresenhamLine(fb, fbW, fbH, ax0, ay0, ax1, ay1, tickCol, 2);
    }

    int nx = dialCX + static_cast<int>((radius - 14) * std::cos(currentAngle));
    int ny = dialCY + static_cast<int>((radius - 14) * std::sin(currentAngle));
    drawBresenhamLine(fb, fbW, fbH, dialCX, dialCY, nx, ny, rgba(239, 68, 68), 2);
    fillMidpointCircle(fb, fbW, fbH, dialCX, dialCY, 4, rgba(255, 255, 255));

    std::string kmhStr = std::to_string(static_cast<int>(speedKmH));
    drawArcadeText(fb, fbW, fbH, dialCX - 14, dialCY - 14, kmhStr, rgba(255, 255, 255), 2, true);
    drawArcadeText(fb, fbW, fbH, dialCX - 12, dialCY + 10, "KM/H", rgba(148, 163, 184), 1, false);

    std::string gearStr;
    if (playerCar.isReversingActive()) {
        gearStr = "REV";
    } else {
        int gear = std::max(1, std::min(6, static_cast<int>(speedKmH / 50.0) + 1));
        gearStr = "G" + std::to_string(gear);
    }
    drawArcadeText(fb, fbW, fbH, dialCX - 10, dialCY + 24, gearStr, playerCar.isReversingActive() ? rgba(56, 189, 248) : rgba(250, 204, 21), 1, false);
}

void TopViewEngine::renderCountdown(uint32_t *fb, int fbW, int fbH) {
    int cx = fbW / 2;
    int cy = fbH / 2 - 30;

    int gantryW = 180;
    int gantryH = 64;
    int gx = cx - gantryW / 2;
    int gy = cy - gantryH / 2;

    for (int y = gy; y < gy + gantryH; y++) {
        fillScanlineBlend(fb, fbW, fbH, y, gx, gx + gantryW, rgba(15, 23, 42, 230));
    }
    drawBresenhamLine(fb, fbW, fbH, gx, gy, gx + gantryW, gy, rgba(148, 163, 184), 2);
    drawBresenhamLine(fb, fbW, fbH, gx, gy + gantryH, gx + gantryW, gy + gantryH, rgba(148, 163, 184), 2);

    for (int i = 0; i < 3; i++) {
        int lx = cx - 48 + (i * 48);
        int ly = cy;
        bool lit = (i < activeCountdownLights);
        uint32_t lightCol = lit ? rgba(239, 68, 68) : rgba(51, 65, 85);
        fillMidpointCircle(fb, fbW, fbH, lx, ly, 15, lightCol);
        if (lit) {
            fillMidpointCircle(fb, fbW, fbH, lx, ly, 8, rgba(254, 202, 202));
        }
    }

    std::string countStr = std::to_string(std::max(1, static_cast<int>(countdownTimer)));
    drawArcadeText(fb, fbW, fbH, cx - 8, cy + 42, countStr, rgba(250, 204, 21), 3, true);
}

void TopViewEngine::renderPodiumModal(uint32_t *fb, int fbW, int fbH, bool victory) {
    int mW = 440;
    int mH = 260;
    int mX = (fbW - mW) / 2;
    int mY = (fbH - mH) / 2;

    for (int y = mY; y < mY + mH; y++) {
        fillScanlineBlend(fb, fbW, fbH, y, mX, mX + mW, rgba(15, 23, 42, 240));
    }
    uint32_t borderCol = victory ? rgba(34, 197, 94) : rgba(239, 68, 68);
    drawBresenhamLine(fb, fbW, fbH, mX, mY, mX + mW, mY, borderCol, 3);
    drawBresenhamLine(fb, fbW, fbH, mX, mY + mH, mX + mW, mY + mH, borderCol, 3);
    drawBresenhamLine(fb, fbW, fbH, mX, mY, mX, mY + mH, borderCol, 3);
    drawBresenhamLine(fb, fbW, fbH, mX + mW, mY, mX + mW, mY + mH, borderCol, 3);

    if (victory) {
        drawArcadeText(fb, fbW, fbH, mX + 80, mY + 22, "ROUND QUALIFIED! PODIUM FINISH!", rgba(34, 197, 94), 1, true);
        std::string rankStr = "YOUR FINISH POSITION: P" + std::to_string(playerRank);
        drawArcadeText(fb, fbW, fbH, mX + 90, mY + 52, rankStr, rgba(250, 204, 21), 2, true);
        int nextLevel = (track.getCurrentTrackId() % 5) + 1;
        std::string advStr = (track.getCurrentTrackId() < 5) ? ("[ ADVANCE TO LEVEL " + std::to_string(nextLevel) + " ]")
                                                             : "[ GRAND CHAMPION! VICTORY LAP ]";
        drawArcadeText(fb, fbW, fbH, mX + 85, mY + 95, advStr, rgba(56, 189, 248), 1, true);
    } else {
        drawArcadeText(fb, fbW, fbH, mX + 45, mY + 22, "YOU HAVE FAILED THIS ROUND!", rgba(239, 68, 68), 1, true);
        drawArcadeText(fb, fbW, fbH, mX + 65, mY + 45, "NOT IN TOP THREE COMPETITORS", rgba(239, 68, 68), 1, true);
        std::string rankStr = "YOUR POSITION: P" + std::to_string(playerRank);
        drawArcadeText(fb, fbW, fbH, mX + 130, mY + 75, rankStr, rgba(241, 245, 249), 2, true);
    }

    int mins = static_cast<int>(totalRaceTime) / 60;
    int secs = static_cast<int>(totalRaceTime) % 60;
    int ms = static_cast<int>((totalRaceTime - std::floor(totalRaceTime)) * 100);
    std::ostringstream ssTot;
    ssTot << "TOTAL TIME: " << std::setfill('0') << std::setw(2) << mins << ":"
          << std::setw(2) << secs << "." << std::setw(2) << ms;
    drawArcadeText(fb, fbW, fbH, mX + 110, mY + 135, ssTot.str(), rgba(203, 213, 225), 1, true);

    int bMins = static_cast<int>(bestLapTime) / 60;
    int bSecs = static_cast<int>(bestLapTime) % 60;
    int bMs = static_cast<int>((bestLapTime - std::floor(bestLapTime)) * 100);
    std::ostringstream ssBest;
    ssBest << "BEST LAP:   " << std::setfill('0') << std::setw(2) << bMins << ":"
           << std::setw(2) << bSecs << "." << std::setw(2) << bMs;
    drawArcadeText(fb, fbW, fbH, mX + 110, mY + 155, ssBest.str(), rgba(250, 204, 21), 1, true);

    if (victory) {
        drawArcadeText(fb, fbW, fbH, mX + 45, mY + 195, "PRESS [ENTER / L] ADVANCE LEVEL  |  [R] RESTART", rgba(56, 189, 248), 1, true);
    } else {
        drawArcadeText(fb, fbW, fbH, mX + 50, mY + 195, "PRESS [ENTER / R] RETRY ROUND  |  [L] LEVEL SELECT", rgba(239, 68, 68), 1, true);
    }
    drawArcadeText(fb, fbW, fbH, mX + 90, mY + 220, "PRESS [1] TO [5] TO SWITCH HYPERCAR", rgba(148, 163, 184), 1, false);
}

void TopViewEngine::renderGameOverModal(uint32_t *fb, int fbW, int fbH) {
    int mW = 400;
    int mH = 200;
    int mX = (fbW - mW) / 2;
    int mY = (fbH - mH) / 2;

    for (int y = mY; y < mY + mH; y++) {
        fillScanlineBlend(fb, fbW, fbH, y, mX, mX + mW, rgba(15, 23, 42, 245));
    }
    drawBresenhamLine(fb, fbW, fbH, mX, mY, mX + mW, mY, rgba(239, 68, 68), 3);
    drawBresenhamLine(fb, fbW, fbH, mX, mY + mH, mX + mW, mY + mH, rgba(239, 68, 68), 3);

    drawArcadeText(fb, fbW, fbH, mX + 105, mY + 30, "CAR DESTROYED!", rgba(239, 68, 68), 2, true);
    drawArcadeText(fb, fbW, fbH, mX + 110, mY + 70, "ALL 3 LIVES DEPLETED", rgba(241, 245, 249), 1, true);
    drawArcadeText(fb, fbW, fbH, mX + 55, mY + 115, "PRESS [ENTER / R] TO REPAIR & RESTART", rgba(250, 204, 21), 1, true);
    drawArcadeText(fb, fbW, fbH, mX + 85, mY + 145, "PRESS [1-5] TO SELECT NEW VEHICLE", rgba(148, 163, 184), 1, false);
}

void TopViewEngine::renderDebugOverlay(uint32_t *fb, int fbW, int fbH) {
    int dX = 220;
    int dY = 14;
    int dW = 340;
    int dH = 160;

    for (int y = dY; y < dY + dH; y++) {
        fillScanlineBlend(fb, fbW, fbH, y, dX, dX + dW, rgba(3, 7, 18, 235));
    }
    drawBresenhamLine(fb, fbW, fbH, dX, dY, dX + dW, dY, rgba(34, 197, 94), 2);
    drawBresenhamLine(fb, fbW, fbH, dX, dY + dH, dX + dW, dY + dH, rgba(34, 197, 94), 2);

    drawArcadeText(fb, fbW, fbH, dX + 8, dY + 6, "ACADEMIC CG LAB TELEMETRY [F1]", rgba(34, 197, 94), 1, true);

    std::ostringstream ss;
    ss << "FPS: " << static_cast<int>(currentFps) << " | FRAME: " << lastFrameTimeUs / 1000 << "ms";
    drawArcadeText(fb, fbW, fbH, dX + 8, dY + 24, ss.str(), rgba(241, 245, 249), 1, false);

    ss.str("");
    ss << "CIRCUIT: LEVEL " << track.getCurrentTrackId() << " | ROAD: 380px (4 GIANT LANES)";
    drawArcadeText(fb, fbW, fbH, dX + 8, dY + 40, ss.str(), rgba(56, 189, 248), 1, false);

    double aiSpd = rivals.empty() ? 0.78 : rivals[0].getSpeedScale();
    double aiMistake = rivals.empty() ? 0.03 : rivals[0].getMistakeProbability();
    ss.str("");
    ss << "AI SPEED: " << static_cast<int>(aiSpd * 100) << "% | CRASH RISK: " << static_cast<int>(aiMistake * 100) << "%";
    drawArcadeText(fb, fbW, fbH, dX + 8, dY + 56, ss.str(), rgba(250, 204, 21), 1, false);

    ss.str("");
    ss << "PLAYER SPEED: " << static_cast<int>(playerCar.getSpeed() * 0.85) << " km/h | GEAR: " << (playerCar.isReversingActive() ? "REV" : "FWD");
    drawArcadeText(fb, fbW, fbH, dX + 8, dY + 72, ss.str(), rgba(241, 245, 249), 1, false);

    ss.str("");
    ss << "CAM MODE: " << ((cameraMode == CAM_TRACK_UP) ? "TRACK-UP ROTATION" : "WORLD FOLLOW");
    drawArcadeText(fb, fbW, fbH, dX + 8, dY + 88, ss.str(), rgba(168, 85, 247), 1, false);

    drawArcadeText(fb, fbW, fbH, dX + 8, dY + 108, "CG MODULE 1: BRESENHAM LINE & THICKNESS", rgba(148, 163, 184), 1, false);
    drawArcadeText(fb, fbW, fbH, dX + 8, dY + 124, "CG MODULE 2: MIDPOINT CIRCLE/ELLIPSE", rgba(148, 163, 184), 1, false);
    drawArcadeText(fb, fbW, fbH, dX + 8, dY + 140, "CG MODULE 4: 2D AFFINE HOMOGENEOUS MATRIX", rgba(148, 163, 184), 1, false);
}
