#ifndef CAR_TOPVIEW_H
#define CAR_TOPVIEW_H

#include "cg_math.h"
#include <string>
#include <vector>

struct CarSpec {
    int id;
    std::string name;
    std::string team;
    double maxSpeed;       // Pixels/sec in world space
    double accelRate;      // Acceleration force
    double brakeRate;      // Braking deceleration
    double turnRate;       // Steering angular velocity (rad/s)
    double driftTraction;  // Lateral grip (0.80 - 0.95)
    double nitroEfficiency;// Nitro power multiplier

    // Color Palette
    uint32_t primaryColor;
    uint32_t secondaryColor;
    uint32_t roofColor;
    uint32_t wingColor;
    uint32_t caliperColor;
    uint32_t headlightColor;
};

class TopViewCar {
public:
    TopViewCar();
    ~TopViewCar() = default;

    void reset(const Vec2D &startPos, double startHeading);
    void selectCar(int index);
    int getCurrentCarIndex() const { return currentCarIdx; }
    int getCarCount() const { return static_cast<int>(specs.size()); }
    const CarSpec& getSpec() const { return specs[currentCarIdx]; }

    // Simulation update
    void update(double dt, bool throttle, bool brake, bool steerLeft, bool steerRight, bool handbrake, bool nitro);

    // Getters & Setters
    const Vec2D& getPos() const { return pos; }
    void setPos(const Vec2D &p) { pos = p; }
    const Vec2D& getVelocity() const { return vel; }
    void setVelocity(const Vec2D &v) { vel = v; }
    double getHeading() const { return heading; }
    void setHeading(double h) { heading = h; }
    double getSpeed() const { return std::abs(speed); }
    double getRawSpeed() const { return speed; }
    void setSpeed(double s) { speed = s; }
    double getSteerAngle() const { return steerAngle; }
    double getDriftAngle() const { return driftAngle; }
    bool isDriftingActive() const { return isDrifting; }
    bool isNitroActive() const { return isNitro; }
    double getNitroLevel() const { return nitroLevel; }
    void addNitro(double amt) { nitroLevel = std::min(100.0, nitroLevel + amt); }
    bool isBrakingActive() const { return isBraking; }
    bool isReversingActive() const { return isReversing; }
    void setOffRoad(bool off) { isOffRoad = off; }
    bool isCarOffRoad() const { return isOffRoad; }

    // Smooth barrier deflection & crash slowdown
    void slideAlongBarrier(const Vec2D &tangent, double pushOutDist);
    void applyCrashSlowdown(double factor = 0.70);
    void setSpeedScale(double s) { speedScale = s; }
    double getSpeedScale() const { return speedScale; }

    // Health / Lives
    int getLives() const { return lives; }
    void setLives(int l) { lives = l; }
    void takeDamage();
    bool isInvulnerableActive() const { return isInvulnerable; }

    // Boost Pad trigger
    void triggerBoostPad(double duration = 1.6, double multiplier = 1.45);

    // Collision Box
    double getCollisionRadius() const { return 24.0; }
    std::vector<Vec2D> getOrientedBoundingBox() const;

    // Rendering in Top View
    void render(uint32_t *fb, int fbW, int fbH, const Mat2D &viewMatrix, bool drawHeadlights = true, bool isPlayer = true);

private:
    std::vector<CarSpec> specs;
    int currentCarIdx;

    Vec2D pos;
    Vec2D vel;
    double heading;        // 0 to 2*PI (0 = facing upwards / -Y)
    double speed;          // Current scalar speed (can be negative for reverse!)
    double steerAngle;     // Front wheel angle (-0.48 to +0.48 rad)
    double angularVelocity;
    double driftAngle;     // Slip angle
    bool isDrifting;
    bool isBraking;
    bool isReversing;
    bool isOffRoad;
    bool isNitro;
    double nitroLevel;     // 0 - 100%
    double speedScale;     // Speed multiplier for difficulty calibration
    double boostPadTimer;
    double boostPadMultiplier;

    // Durability & Invulnerability
    int lives;
    bool isInvulnerable;
    double invulnerableTimer;

    // Animation timers
    double flameAnimTimer;
};

#endif // CAR_TOPVIEW_H
