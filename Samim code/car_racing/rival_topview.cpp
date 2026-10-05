#include "rival_topview.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>

TopViewRival::TopViewRival(const std::string &name, int carIdx, double startDist, double lateralOffset, double aggression)
    : driverName(name),
      trackDistance(startDist),
      baseLateralOffset(lateralOffset),
      currentLateralOffset(lateralOffset),
      targetLateralOffset(lateralOffset),
      aggressionFactor(aggression),
      lapsCompleted(0),
      lastDistAlongTrack(startDist),
      nitroCooldown(3.0),
      laneShiftTimer(2.5 + (rand() % 3)),
      currentLevel(1),
      speedScale(0.52),
      safetyDistance(95.0),
      mistakeProbability(0.03),
      crashWobbleTimer(0.0),
      crashWobbleDir(1.0),
      crashRecoverTimer(0.0) {
    car.selectCar(carIdx);
    car.setSpeedScale(speedScale);
}

void TopViewRival::setLevel(int level) {
    currentLevel = level;
    if (level == 1) {
        // Level 1: Straight drag road, opponents at ~52% speed (~200-210 px/s)
        // Slight difficulty increase: smooth cruising pack, easily overtakable by player (at 100%)
        speedScale = 0.52;
        safetyDistance = 100.0;
        mistakeProbability = 0.00;
        nitroCooldown = 999.0;
    } else if (level == 2) {
        // Level 2: Gentle coastal curves, opponents at ~63% speed (~245-255 px/s)
        // Gradual increase: nice steady road pace
        speedScale = 0.63;
        safetyDistance = 80.0;
        mistakeProbability = 0.04;
        nitroCooldown = 18.0;
    } else if (level == 3) {
        // Level 3: Winding canyon, opponents at ~73% speed (~285-295 px/s)
        // Gradual increase: moderately challenging through canyon curves
        speedScale = 0.73;
        safetyDistance = 60.0;
        mistakeProbability = 0.10;
        nitroCooldown = 12.0;
    } else if (level == 4) {
        // Level 4: Closed loop circuit (3 laps!), opponents at ~82% speed (~320-330 px/s)
        // Competitive racing pack, player maintains clear lead with good lines & nitro
        speedScale = 0.82;
        safetyDistance = 45.0;
        mistakeProbability = 0.16;
        nitroCooldown = 8.0;
    } else {
        // Level 5: Championship loop (3 laps!), opponents at ~90% speed (~350-365 px/s)
        // Thrilling championship pace, player retains full speed (100%) + nitro advantage
        speedScale = 0.90;
        safetyDistance = 38.0;
        mistakeProbability = 0.22;
        nitroCooldown = 5.5;
    }
    car.setSpeedScale(speedScale);
}

void TopViewRival::triggerCrashWobble(const Vec2D &impactNormal) {
    crashWobbleTimer = 0.75;
    crashRecoverTimer = 0.85;
    double fwdDot = impactNormal.x * std::cos(car.getHeading()) + impactNormal.y * std::sin(car.getHeading());
    crashWobbleDir = (fwdDot > 0.0) ? 1.0 : -1.0;
}

void TopViewRival::applyCrashSlowdown(double factor) {
    car.applyCrashSlowdown(factor);
}

void TopViewRival::reset(const TopViewTrack &track, int gridIndex) {
    Vec2D gridPos = track.getGridPosition(gridIndex);
    car.reset(gridPos, track.getStartHeading());
    car.setSpeedScale(speedScale);
    trackDistance = track.getDistanceAlongTrack(gridPos);
    lastDistAlongTrack = trackDistance;
    lapsCompleted = 0;
    nitroCooldown = (currentLevel == 1) ? 999.0 : (2.5 + (gridIndex * 0.8));
    currentLateralOffset = baseLateralOffset;
    targetLateralOffset = baseLateralOffset;
    laneShiftTimer = 3.0 + gridIndex;
    crashWobbleTimer = 0.0;
    crashRecoverTimer = 0.0;
}

double TopViewRival::getTotalDistance(double trackLength, bool isLoop) const {
    if (isLoop) {
        if (lapsCompleted >= 1) {
            return trackLength * lapsCompleted + trackDistance;
        }
        return trackDistance;
    }
    // Point-to-point sprint track: if crossed finish line, give trackLength bonus
    if (lapsCompleted >= 1) {
        return trackLength + trackDistance;
    }
    return trackDistance;
}

void TopViewRival::update(double dt, const TopViewTrack &track, const std::vector<Vec2D> &otherCarPositions) {
    double trackLen = track.getTrackLength();
    if (trackLen < 1.0) return;

    trackDistance = track.getDistanceAlongTrack(car.getPos());

    // Lap counting
    if (!track.isLoop()) {
        double finishDist = trackLen - 120.0;
        if (trackDistance >= finishDist) {
            lapsCompleted = 1;
        }
    } else {
        if (lastDistAlongTrack > trackLen * 0.85 && trackDistance < trackLen * 0.15) {
            lapsCompleted++;
        } else if (lastDistAlongTrack < trackLen * 0.15 && trackDistance > trackLen * 0.85) {
            lapsCompleted = std::max(0, lapsCompleted - 1);
        }
    }
    lastDistAlongTrack = trackDistance;

    // Crash recovery & post-collision wobble timers
    if (crashRecoverTimer > 0.0) {
        crashRecoverTimer -= dt;
    }
    if (crashWobbleTimer > 0.0) {
        crashWobbleTimer -= dt;
        double wobbleDelta = crashWobbleDir * std::sin(crashWobbleTimer * 18.0) * 0.035;
        car.setHeading(car.getHeading() + wobbleDelta);
    }

    // Smooth lane transition towards targetLateralOffset
    currentLateralOffset += (targetLateralOffset - currentLateralOffset) * dt * 2.5;

    // AI Dynamic Lane Changing & Overtaking
    laneShiftTimer -= dt;
    if (laneShiftTimer <= 0.0) {
        laneShiftTimer = (currentLevel == 1) ? (3.5 + (rand() % 4)) :
                         (currentLevel == 2) ? (2.2 + (rand() % 3)) :
                                               (1.2 + (rand() % 2));

        // Check if another car is directly ahead in our lane
        bool carAheadInLane = false;
        Vec2D forward = Vec2D(std::sin(car.getHeading()), -std::cos(car.getHeading()));

        for (const auto &otherPos : otherCarPositions) {
            Vec2D toOther = otherPos - car.getPos();
            double fwdDist = toOther.dot(forward);
            double sideDist = std::abs(toOther.cross(forward));

            if (fwdDist > 15.0 && fwdDist < (120.0 + currentLevel * 30.0) && sideDist < 45.0) {
                carAheadInLane = true;
                break;
            }
        }

        if (carAheadInLane) {
            // Shift to adjacent lane to overtake across massive 540px road!
            if (currentLateralOffset < 0.0) {
                targetLateralOffset = std::min(200.0, currentLateralOffset + 120.0);
            } else {
                targetLateralOffset = std::max(-200.0, currentLateralOffset - 120.0);
            }
        } else if (rand() % 3 == 0) {
            // Return to preferred base lane
            targetLateralOffset = baseLateralOffset;
        }
    }

    // AI Waypoint Navigation
    double lookAheadDist = 130.0 + (car.getSpeed() / 320.0) * 90.0;
    double targetDist = trackDistance + lookAheadDist;
    if (!track.isLoop()) {
        targetDist = std::min(trackLen - 10.0, targetDist);
    }
    Vec2D targetCenter = track.getPointAtDistance(targetDist);
    Vec2D tangent = track.getTangentAtPoint(targetCenter);
    Vec2D normal = Vec2D(-tangent.y, tangent.x);

    Vec2D targetPos = targetCenter + normal * currentLateralOffset;

    // Car-to-Car separation avoidance and contested battles
    for (const auto &otherPos : otherCarPositions) {
        Vec2D toOther = otherPos - car.getPos();
        double d = toOther.length();
        if (d > 0.1 && d < safetyDistance) {
            bool willYield = ((rand() % 100) >= static_cast<int>(mistakeProbability * 100));
            if (willYield) {
                Vec2D avoidDir = (car.getPos() - otherPos).normalized();
                targetPos = targetPos + avoidDir * (safetyDistance - d) * 0.55;
            } else {
                // Aggressive squeeze: contest track position
                Vec2D contestDir = (otherPos - car.getPos()).normalized();
                targetPos = targetPos + contestDir * 12.0;
            }
        }
    }

    // Steering towards target
    Vec2D toTarget = (targetPos - car.getPos()).normalized();
    double desiredHeading = std::atan2(toTarget.x, -toTarget.y);

    double headingDiff = desiredHeading - car.getHeading();
    const double PI = 3.141592653589793;
    const double TWO_PI = 6.283185307179586;
    while (headingDiff > PI)  headingDiff -= TWO_PI;
    while (headingDiff < -PI) headingDiff += TWO_PI;

    bool steerLeft  = (headingDiff < -0.04);
    bool steerRight = (headingDiff >  0.04);

    // Throttle & Braking
    bool throttle = true;
    bool brake = false;

    // If finished open track sprint, brake to a stop
    if (!track.isLoop() && trackDistance >= trackLen - 70.0) {
        throttle = false;
        brake = true;
    }
    // If recovering from crash, car is slowed down and cannot immediately throttle at 100%
    else if (crashRecoverTimer > 0.40) {
        throttle = false;
        brake = true;
    } else if (crashRecoverTimer > 0.0) {
        throttle = false;
        brake = false;
    }

    // Smooth corner speed moderation (scaled by level speed)
    double maxTurnSpeed = 220.0 * speedScale;
    if (std::abs(headingDiff) > 0.42 && car.getSpeed() > maxTurnSpeed) {
        throttle = false;
        brake = true;
    }

    // AI Nitro on straightaways (Only for level 2 and above!)
    bool useNitro = false;
    if (currentLevel >= 2) {
        nitroCooldown -= dt;
        double nitroThreshold = (currentLevel == 2) ? 0.05 : (currentLevel == 3) ? 0.08 : (currentLevel == 4) ? 0.12 : 0.15;
        if (nitroCooldown <= 0.0 && std::abs(headingDiff) < nitroThreshold && car.getSpeed() > (190.0 * speedScale)) {
            useNitro = true;
            if (car.getNitroLevel() < 15.0) {
                nitroCooldown = (currentLevel == 2) ? 10.0 : (currentLevel == 3) ? 7.5 : (currentLevel == 4) ? 5.5 : 4.0;
            }
        }
    }

    car.update(dt, throttle, brake, steerLeft, steerRight, false, useNitro);

    // Keep rival within road boundaries and slow down on barrier impact
    Vec2D pushOut(0.0, 0.0);
    if (track.checkBarrierCollision(car.getPos(), car.getCollisionRadius(), pushOut)) {
        car.setPos(car.getPos() + pushOut);
        Vec2D t = track.getTangentAtPoint(car.getPos());
        car.slideAlongBarrier(t, pushOut.length());
        car.applyCrashSlowdown(0.85); // Barrier scrape slows down car
    }
}

void TopViewRival::render(uint32_t *fb, int fbW, int fbH, const Mat2D &viewMatrix) {
    car.render(fb, fbW, fbH, viewMatrix, true, false);
}
