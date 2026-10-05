#ifndef RIVAL_TOPVIEW_H
#define RIVAL_TOPVIEW_H

#include "car_topview.h"
#include "track_topview.h"
#include <string>

class TopViewRival {
public:
    TopViewRival(const std::string &name, int carIdx, double startDist, double lateralOffset, double aggression);
    ~TopViewRival() = default;

    const std::string& getName() const { return driverName; }
    TopViewCar& getCar() { return car; }
    const TopViewCar& getCar() const { return car; }

    void setLevel(int level);
    int getLevel() const { return currentLevel; }
    void triggerCrashWobble(const Vec2D &impactNormal);
    void applyCrashSlowdown(double factor = 0.70);
    double getSpeedScale() const { return speedScale; }
    double getMistakeProbability() const { return mistakeProbability; }

    void reset(const TopViewTrack &track, int gridIndex);
    void update(double dt, const TopViewTrack &track, const std::vector<Vec2D> &otherCarPositions);

    double getTotalDistance(double trackLength, bool isLoop = true) const;
    void render(uint32_t *fb, int fbW, int fbH, const Mat2D &viewMatrix);

    int getCarIndex() const { return car.getCurrentCarIndex(); }
    uint32_t getColor() const { return car.getSpec().primaryColor; }

    int getLapsCompleted() const { return lapsCompleted; }

private:
    std::string driverName;
    TopViewCar car;
    double trackDistance;
    double baseLateralOffset;
    double currentLateralOffset;
    double targetLateralOffset;
    double aggressionFactor;
    int lapsCompleted;
    double lastDistAlongTrack;
    double nitroCooldown;
    double laneShiftTimer;

    // Level & Crash Progression Attributes
    int currentLevel;
    double speedScale;
    double safetyDistance;
    double mistakeProbability;
    double crashWobbleTimer;
    double crashWobbleDir;
    double crashRecoverTimer;
};

#endif // RIVAL_TOPVIEW_H
