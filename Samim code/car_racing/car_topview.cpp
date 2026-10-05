#include "car_topview.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>

TopViewCar::TopViewCar()
    : currentCarIdx(0),
      pos(0.0, 0.0),
      vel(0.0, 0.0),
      heading(0.0),
      speed(0.0),
      steerAngle(0.0),
      angularVelocity(0.0),
      driftAngle(0.0),
      isDrifting(false),
      isBraking(false),
      isReversing(false),
      isOffRoad(false),
      isNitro(false),
      nitroLevel(100.0),
      speedScale(1.0),
      boostPadTimer(0.0),
      boostPadMultiplier(1.0),
      lives(3),
      isInvulnerable(false),
      invulnerableTimer(0.0),
      flameAnimTimer(0.0) {

    // 5 High-Performance Supercars & Hypercars
    specs = {
        // Car 0: APEX FALCON (Scuderia Rosso) - Balanced Italian GT
        {
            0, "APEX FALCON", "SCUDERIA CORSE",
            380.0, 205.0, 320.0, 1.20, 0.94, 1.25,
            rgba(220, 38, 38),   // Primary: Rosso Corsa Red
            rgba(255, 255, 255), // Secondary: Pure White Stripe
            rgba(30, 41, 59),    // Roof: Dark Slate
            rgba(15, 23, 42),    // Wing: Carbon Black
            rgba(239, 68, 68),   // Caliper: Red
            rgba(254, 240, 138, 180) // Headlights: Warm Xenon
        },
        // Car 1: VENOM STRYKER (Lambo Lime) - Sharp Cornering Specialist
        {
            1, "VENOM STRYKER", "BULL VELOCE",
            395.0, 220.0, 340.0, 1.35, 0.95, 1.30,
            rgba(34, 197, 94),   // Primary: Electric Lime Green
            rgba(15, 23, 42),    // Secondary: Carbon Black Stripe
            rgba(20, 25, 35),    // Roof: Matte Black
            rgba(234, 179, 8),   // Wing: Gold Carbon Endplates
            rgba(234, 179, 8),   // Caliper: Gold
            rgba(240, 253, 244, 200) // Headlights: Ice White
        },
        // Car 2: CHIRON MIRAGE (Royal Cobalt) - Maximum Top Speed King
        {
            2, "CHIRON MIRAGE", "MOLSHEIM APEX",
            430.0, 195.0, 310.0, 1.10, 0.93, 1.45,
            rgba(29, 78, 216),   // Primary: Deep Royal Blue
            rgba(56, 189, 248),  // Secondary: Electric Cyan Arc
            rgba(241, 245, 249), // Roof: Polished Aluminum Silver
            rgba(30, 58, 138),   // Wing: Deep Blue
            rgba(56, 189, 248),  // Caliper: Cyan
            rgba(186, 230, 253, 210) // Headlights: Blue Xenon
        },
        // Car 3: SUNFIRE HYPER (Papaya Orange) - Precision Cornering Specialist
        {
            3, "SUNFIRE HYPER", "WOKING RACING",
            385.0, 230.0, 350.0, 1.40, 0.96, 1.28,
            rgba(249, 115, 22),  // Primary: McLaren Papaya Orange
            rgba(15, 23, 42),    // Secondary: Stealth Black
            rgba(30, 41, 59),    // Roof: Dark Gray
            rgba(249, 115, 22),  // Wing: Papaya Wing
            rgba(249, 115, 22),  // Caliper: Orange
            rgba(254, 243, 199, 190) // Headlights: Amber Glow
        },
        // Car 4: GHOST SPECTER (Cyber Onyx) - Prototype Stealth Interceptor
        {
            4, "GHOST SPECTER", "CYBERTECH LABS",
            410.0, 215.0, 330.0, 1.25, 0.93, 1.40,
            rgba(51, 65, 85),    // Primary: Gunmetal Gray
            rgba(168, 85, 247),  // Secondary: Cyber Violet Accents
            rgba(15, 23, 42),    // Roof: Stealth Onyx
            rgba(147, 51, 234),  // Wing: Violet Wing
            rgba(168, 85, 247),  // Caliper: Neon Purple
            rgba(233, 213, 255, 220) // Headlights: Laser Violet
        }
    };

    reset(Vec2D(0.0, 0.0), 0.0);
}

void TopViewCar::reset(const Vec2D &startPos, double startHeading) {
    pos = startPos;
    heading = startHeading;
    vel = Vec2D(0.0, 0.0);
    speed = 0.0;
    steerAngle = 0.0;
    angularVelocity = 0.0;
    driftAngle = 0.0;
    isDrifting = false;
    isBraking = false;
    isReversing = false;
    isOffRoad = false;
    isNitro = false;
    nitroLevel = 100.0;
    boostPadTimer = 0.0;
    boostPadMultiplier = 1.0;
    lives = 3;
    isInvulnerable = false;
    invulnerableTimer = 0.0;
    flameAnimTimer = 0.0;
}

void TopViewCar::selectCar(int index) {
    if (index >= 0 && index < static_cast<int>(specs.size())) {
        currentCarIdx = index;
    }
}

void TopViewCar::triggerBoostPad(double duration, double multiplier) {
    boostPadTimer = duration;
    boostPadMultiplier = multiplier;
    addNitro(30.0);
}

void TopViewCar::takeDamage() {
    if (isInvulnerable) return;
    lives = std::max(0, lives - 1);
    isInvulnerable = true;
    invulnerableTimer = 2.2;
}

void TopViewCar::slideAlongBarrier(const Vec2D &tangent, double /*pushOutDist*/) {
    if (isReversing || speed < -5.0) {
        speed = std::max(-72.0, speed * 0.85);
        vel = tangent * speed;
        return;
    }
    double fwd = vel.dot(tangent);
    if (fwd < 40.0) fwd = 40.0; // Preserve forward drive!
    speed = fwd * 0.90;
    vel = tangent * speed;

    // Gently align car heading with track curve
    double targetAngle = std::atan2(tangent.x, -tangent.y);
    double diff = targetAngle - heading;
    const double PI = 3.141592653589793;
    const double TWO_PI = 6.283185307179586;
    while (diff > PI)  diff -= TWO_PI;
    while (diff < -PI) diff += TWO_PI;
    heading += diff * 0.15;
}

void TopViewCar::applyCrashSlowdown(double factor) {
    speed *= factor;
    vel = vel * factor;
}

void TopViewCar::update(double dt, bool throttle, bool brake, bool steerLeft, bool steerRight, bool handbrake, bool nitro) {
    const CarSpec &spec = specs[currentCarIdx];
    flameAnimTimer += dt * 25.0;

    // Invulnerability timer
    if (isInvulnerable) {
        invulnerableTimer -= dt;
        if (invulnerableTimer <= 0.0) {
            isInvulnerable = false;
            invulnerableTimer = 0.0;
        }
    }

    // Boost Pad timer
    if (boostPadTimer > 0.0) {
        boostPadTimer -= dt;
        if (boostPadTimer <= 0.0) {
            boostPadTimer = 0.0;
            boostPadMultiplier = 1.0;
        }
    }

    // Forward direction in world space (heading 0 = facing Upwards along -Y)
    Vec2D forward = Vec2D(std::sin(heading), -std::cos(heading));
    Vec2D right   = Vec2D(std::cos(heading),  std::sin(heading));

    // Nitro handling (only active forward)
    isNitro = nitro && (nitroLevel > 0.5) && (throttle || speed > 40.0) && !isReversing;
    double nitroBonus = 1.0;
    if (isNitro) {
        nitroBonus = spec.nitroEfficiency;
        nitroLevel = std::max(0.0, nitroLevel - dt * 20.0);
    } else {
        nitroLevel = std::min(100.0, nitroLevel + dt * 3.5);
    }

    // Steer angle interpolation with progressive build-up & realistic gentle lock
    double maxSteerLimit = 0.24; // Gentle, realistic wheel lock (~13.8 deg)
    double targetSteer = 0.0;
    if (steerLeft)  targetSteer -= maxSteerLimit;
    if (steerRight) targetSteer += maxSteerLimit;

    // Ultra-smooth, progressive steering response: gentle turn-in for effortless tracking
    double steerSpeed = (std::abs(targetSteer) > 0.001) ? 2.8 : 6.0;
    steerAngle += (targetSteer - steerAngle) * dt * steerSpeed;

    // Reverse Gear Logic:
    // When holding brake and speed <= 25.0 px/s: engage Reverse gear!
    if (brake && speed <= 25.0) {
        isReversing = true;
    } else if (throttle) {
        isReversing = false;
    }

    isBraking = brake && !isReversing && (speed > 25.0);
    // Explicit handbrake required for drifting — normal brake+steering stays gripped to help get back on track!
    isDrifting = (handbrake && speed > 80.0 && !isReversing);

    // Speed-Sensitive Steering Stability:
    // High-speed attenuation prevents sudden twitching/oversteering on straights and curves.
    // Low-speed authority lets the player easily pivot and guide the car back onto the track!
    double absSpeed = std::abs(speed);
    double speedFactor = 1.0;
    if (absSpeed > 100.0) {
        // Smoothly stabilize steering at high speeds for silky smooth highway cornering
        speedFactor = std::max(0.38, 1.0 - ((absSpeed - 100.0) / 450.0) * 0.62);
    } else if (absSpeed < 30.0) {
        // Controlled, gentle pivot when stopped or backing up
        speedFactor = 0.75;
    }

    // Steering direction in reverse: gentle, predictable realignment
    double dirSign = (speed < -5.0) ? -1.0 : 1.0;
    double steerResponse = spec.turnRate * (isDrifting ? 1.25 : 1.0);
    double reverseMod = (speed < -5.0) ? 0.55 : 1.0;
    angularVelocity = (steerAngle / maxSteerLimit) * steerResponse * speedFactor * dirSign * reverseMod;
    heading += angularVelocity * dt;

    const double TWO_PI = 6.283185307179586;
    while (heading < 0.0) heading += TWO_PI;
    while (heading >= TWO_PI) heading -= TWO_PI;

    // Acceleration & Braking
    double offRoadDamp = isOffRoad ? 0.72 : 1.0;
    double effectiveMaxSpeed = spec.maxSpeed * speedScale * (isNitro ? 1.35 : 1.0) * boostPadMultiplier * offRoadDamp;

    // Reverse speed: calm, slow crawl (~35 km/h) specifically tuned for backing out easily
    double maxReverseSpeed = -45.0;

    double baseAccel = spec.accelRate * speedScale * nitroBonus * boostPadMultiplier * (isOffRoad ? 0.80 : 1.0);

    if (throttle) {
        if (speed < 0.0) {
            // Smooth transition from reverse back to forward
            speed = std::min(0.0, speed + 180.0 * dt);
        } else {
            // Progressive forward acceleration: smooth gradual ramp-up from stationary
            double speedRatio = std::max(0.0, std::min(1.0, speed / std::max(1.0, effectiveMaxSpeed)));
            double gradualFactor = 0.40 + 0.60 * (speed / 140.0);
            if (speed >= 140.0) {
                gradualFactor = 1.0 - 0.22 * std::pow(speedRatio, 2.0); // Natural engine power taper
            }
            if (isNitro) {
                gradualFactor = 1.25; // Instant surge on nitro boost!
            }
            double effectiveAccel = baseAccel * std::max(0.35, gradualFactor);
            speed += effectiveAccel * dt;
            if (speed > effectiveMaxSpeed) {
                speed = std::max(effectiveMaxSpeed, speed - dt * 50.0);
            }
        }
    } else if (brake) {
        if (isReversing) {
            // Calm, slow reverse movement to easily back away from walls
            double reverseAccel = 45.0;
            speed = std::max(maxReverseSpeed, speed - reverseAccel * dt);
        } else {
            // Forward braking: smooth, comfortable deceleration
            double smoothBrake = 190.0;
            speed = std::max(0.0, speed - smoothBrake * dt);
        }
    } else {
        // Natural rolling friction & aerodynamic drag
        double drag = (25.0 + 0.0014 * std::abs(speed) * std::abs(speed));
        if (speed > 0.0) {
            speed = std::max(0.0, speed - drag * dt);
        } else if (speed < 0.0) {
            speed = std::min(0.0, speed + drag * dt);
        }
    }

    // Drifting physics (slip angle & lateral grip)
    double lateralGrip = spec.driftTraction;
    if (isDrifting) {
        lateralGrip *= 0.65;
        nitroLevel = std::min(100.0, nitroLevel + dt * 20.0);
    }
    if (isOffRoad) {
        lateralGrip *= 0.85;
    }

    double currentLateralSpeed = vel.dot(right);
    double newForwardSpeed = speed;
    double newLateralSpeed = currentLateralSpeed * std::pow(lateralGrip, dt * 60.0);

    vel = forward * newForwardSpeed + right * newLateralSpeed;
    pos = pos + vel * dt;

    if (vel.length() > 20.0) {
        Vec2D velNorm = vel.normalized();
        driftAngle = std::asin(std::max(-1.0, std::min(1.0, forward.cross(velNorm))));
    } else {
        driftAngle = 0.0;
    }
}

std::vector<Vec2D> TopViewCar::getOrientedBoundingBox() const {
    Vec2D forward = Vec2D(std::sin(heading), -std::cos(heading));
    Vec2D right   = Vec2D(std::cos(heading),  std::sin(heading));

    double halfL = 30.0;
    double halfW = 15.0;

    return {
        pos + forward * halfL - right * halfW,
        pos + forward * halfL + right * halfW,
        pos - forward * halfL + right * halfW,
        pos - forward * halfL - right * halfW
    };
}

void TopViewCar::render(uint32_t *fb, int fbW, int fbH, const Mat2D &viewMatrix, bool drawHeadlights, bool /*isPlayer*/) {
    if (isInvulnerable && (static_cast<int>(invulnerableTimer * 24.0) % 2 == 0)) {
        return; // Invulnerability flicker
    }

    const CarSpec &spec = specs[currentCarIdx];

    // Car transformation matrix
    Mat2D carToWorld = Mat2D::multiply(Mat2D::translation(pos.x, pos.y), Mat2D::rotation(heading));
    Mat2D carToScreen = Mat2D::multiply(viewMatrix, carToWorld);

    auto toScreen = [&](double u, double v) -> Vec2D {
        return carToScreen.transform(u, v);
    };

    // 1. Drop Shadow
    {
        Mat2D shadowToWorld = Mat2D::multiply(Mat2D::translation(pos.x + 4.0, pos.y + 5.0), Mat2D::rotation(heading));
        Mat2D shadowToScreen = Mat2D::multiply(viewMatrix, shadowToWorld);
        std::vector<Vec2D> shadowPoly = {
            shadowToScreen.transform(-16.0,  31.0),
            shadowToScreen.transform( 16.0,  31.0),
            shadowToScreen.transform( 17.0, -30.0),
            shadowToScreen.transform(-17.0, -30.0)
        };
        fillConvexPolygonBlend(fb, fbW, fbH, shadowPoly, rgba(15, 23, 42, 130));
    }

    // 2. Headlight Projection Beams
    if (drawHeadlights) {
        Vec2D leftLightScreen  = toScreen(-10.0, 30.0);
        Vec2D rightLightScreen = toScreen( 10.0, 30.0);
        Vec2D forwardScreen = (toScreen(0.0, 32.0) - toScreen(0.0, 0.0)).normalized();

        uint32_t innerBeam = spec.headlightColor;
        uint32_t outerBeam = rgba(getRed(innerBeam), getGreen(innerBeam), getBlue(innerBeam), 0);

        drawHeadlightCone(fb, fbW, fbH, leftLightScreen,  forwardScreen, 0.42, 120.0, innerBeam, outerBeam);
        drawHeadlightCone(fb, fbW, fbH, rightLightScreen, forwardScreen, 0.42, 120.0, innerBeam, outerBeam);
    }

    // 3. Four Wheels (with Steerable Front Wheels & 5-Spoke Alloy Rims!)
    auto drawWheel = [&](double cx, double cy, double wheelSteer) {
        Mat2D wheelLocal = Mat2D::multiply(Mat2D::translation(cx, cy), Mat2D::rotation(wheelSteer));
        Mat2D wheelToScreen = Mat2D::multiply(carToScreen, wheelLocal);

        double hw = 4.2;
        double hl = 8.5;

        std::vector<Vec2D> tirePoly = {
            wheelToScreen.transform(-hw,  hl),
            wheelToScreen.transform( hw,  hl),
            wheelToScreen.transform( hw, -hl),
            wheelToScreen.transform(-hw, -hl)
        };
        fillConvexPolygon(fb, fbW, fbH, tirePoly, rgba(17, 24, 39));
        drawPolygonOutline(fb, fbW, fbH, tirePoly, rgba(3, 7, 18), 1);

        Vec2D rimCenter = wheelToScreen.transform(0.0, 0.0);
        fillMidpointCircle(fb, fbW, fbH, static_cast<int>(rimCenter.x), static_cast<int>(rimCenter.y), 3, rgba(75, 85, 99));

        // 5-Spoke Star Rim lines
        for (int sp = 0; sp < 5; sp++) {
            double ang = wheelSteer + (sp * 2.0 * 3.14159 / 5.0);
            Vec2D spTip = wheelToScreen.transform(std::cos(ang) * (hw - 1.0), std::sin(ang) * (hl - 2.5));
            drawBresenhamLine(fb, fbW, fbH, static_cast<int>(rimCenter.x), static_cast<int>(rimCenter.y),
                              static_cast<int>(spTip.x), static_cast<int>(spTip.y), rgba(209, 213, 219), 1);
        }
        fillMidpointCircle(fb, fbW, fbH, static_cast<int>(rimCenter.x), static_cast<int>(rimCenter.y), 1, rgba(255, 255, 255));

        Vec2D caliperPos = wheelToScreen.transform(0.0, 3.5);
        putPixelSafe(fb, fbW, fbH, static_cast<int>(caliperPos.x), static_cast<int>(caliperPos.y), spec.caliperColor);
    };

    // Rear Wheels (fixed)
    drawWheel(-14.5, -17.0, 0.0);
    drawWheel( 14.5, -17.0, 0.0);

    // Front Wheels (dynamically angled with steerAngle!)
    drawWheel(-14.5,  17.0, steerAngle);
    drawWheel( 14.5,  17.0, steerAngle);

    // 4. Front Aerodynamic Splitter & Dive Canards
    {
        std::vector<Vec2D> splitterPoly = {
            toScreen(-14.0, 33.5),
            toScreen( 14.0, 33.5),
            toScreen( 15.0, 26.5),
            toScreen(-15.0, 26.5)
        };
        fillConvexPolygon(fb, fbW, fbH, splitterPoly, rgba(15, 23, 42));
        drawPolygonOutline(fb, fbW, fbH, splitterPoly, spec.secondaryColor, 1);

        // Aerodynamic canards/winglets
        drawBresenhamLine(fb, fbW, fbH,
                          static_cast<int>(toScreen(-15.0, 31.0).x), static_cast<int>(toScreen(-15.0, 31.0).y),
                          static_cast<int>(toScreen(-18.0, 27.0).x), static_cast<int>(toScreen(-18.0, 27.0).y),
                          rgba(15, 23, 42), 2);
        drawBresenhamLine(fb, fbW, fbH,
                          static_cast<int>(toScreen(15.0, 31.0).x), static_cast<int>(toScreen(15.0, 31.0).y),
                          static_cast<int>(toScreen(18.0, 27.0).x), static_cast<int>(toScreen(18.0, 27.0).y),
                          rgba(15, 23, 42), 2);
    }

    // 5. Main Sculpted Hypercar Chassis with Bevel Highlights
    {
        std::vector<Vec2D> bodyPoly = {
            toScreen( -9.0,  31.0),
            toScreen(  9.0,  31.0),
            toScreen( 14.0,  25.0),
            toScreen( 15.5,  15.0),
            toScreen( 13.5,   5.0),
            toScreen( 14.0,  -6.0),
            toScreen( 16.0, -16.0),
            toScreen( 14.5, -28.0),
            toScreen(  9.0, -31.0),
            toScreen( -9.0, -31.0),
            toScreen(-14.5, -28.0),
            toScreen(-16.0, -16.0),
            toScreen(-14.0,  -6.0),
            toScreen(-13.5,   5.0),
            toScreen(-15.5,  15.0),
            toScreen(-14.0,  25.0)
        };
        fillConvexPolygon(fb, fbW, fbH, bodyPoly, spec.primaryColor);
        drawPolygonOutline(fb, fbW, fbH, bodyPoly, scaleBrightness(spec.primaryColor, 0.65), 1);

        // Fender highlights & waistline scallops
        Vec2D fL0 = toScreen(-13.5, 23.0), fL1 = toScreen(-14.5, 15.0);
        Vec2D fR0 = toScreen( 13.5, 23.0), fR1 = toScreen( 14.5, 15.0);
        uint32_t highlightCol = scaleBrightness(spec.primaryColor, 1.30);
        drawBresenhamLine(fb, fbW, fbH, static_cast<int>(fL0.x), static_cast<int>(fL0.y), static_cast<int>(fL1.x), static_cast<int>(fL1.y), highlightCol, 1);
        drawBresenhamLine(fb, fbW, fbH, static_cast<int>(fR0.x), static_cast<int>(fR0.y), static_cast<int>(fR1.x), static_cast<int>(fR1.y), highlightCol, 1);
    }

    // 6. Daytime Running Lights (DRL LED Eyebrows)
    {
        Vec2D drlL0 = toScreen(-13.0, 28.5);
        Vec2D drlL1 = toScreen( -8.0, 31.0);
        drawBresenhamLine(fb, fbW, fbH, static_cast<int>(drlL0.x), static_cast<int>(drlL0.y),
                          static_cast<int>(drlL1.x), static_cast<int>(drlL1.y), rgba(255, 255, 255), 2);

        Vec2D drlR0 = toScreen( 13.0, 28.5);
        Vec2D drlR1 = toScreen(  8.0, 31.0);
        drawBresenhamLine(fb, fbW, fbH, static_cast<int>(drlR0.x), static_cast<int>(drlR0.y),
                          static_cast<int>(drlR1.x), static_cast<int>(drlR1.y), rgba(255, 255, 255), 2);
    }

    // 7. Center Racing Stripe & Hood Air Extractors
    {
        std::vector<Vec2D> stripePoly = {
            toScreen(-2.5,  30.5),
            toScreen( 2.5,  30.5),
            toScreen( 2.5, -29.0),
            toScreen(-2.5, -29.0)
        };
        fillConvexPolygon(fb, fbW, fbH, stripePoly, spec.secondaryColor);

        // Hood cooling vents with black mesh
        std::vector<Vec2D> leftScoop = {
            toScreen(-8.0, 22.0), toScreen(-5.0, 22.0),
            toScreen(-6.0, 16.0), toScreen(-8.5, 16.0)
        };
        fillConvexPolygon(fb, fbW, fbH, leftScoop, rgba(15, 23, 42));

        std::vector<Vec2D> rightScoop = {
            toScreen(5.0, 22.0), toScreen(8.0, 22.0),
            toScreen(8.5, 16.0), toScreen(6.0, 16.0)
        };
        fillConvexPolygon(fb, fbW, fbH, rightScoop, rgba(15, 23, 42));
    }

    // 8. Tinted Windshield with Double Specular Glint & Wiper
    {
        std::vector<Vec2D> windshieldPoly = {
            toScreen(-10.5, 15.0),
            toScreen( 10.5, 15.0),
            toScreen(  9.0,  7.0),
            toScreen( -9.0,  7.0)
        };
        fillConvexPolygon(fb, fbW, fbH, windshieldPoly, rgba(15, 23, 42, 245));

        // Specular glint
        Vec2D glint0 = toScreen(-7.0, 14.0);
        Vec2D glint1 = toScreen(-2.0,  8.0);
        drawBresenhamLine(fb, fbW, fbH, static_cast<int>(glint0.x), static_cast<int>(glint0.y),
                          static_cast<int>(glint1.x), static_cast<int>(glint1.y), rgba(255, 255, 255, 230), 2);

        // Race wiper blade
        Vec2D wip0 = toScreen(0.0, 7.5);
        Vec2D wip1 = toScreen(5.0, 13.5);
        drawBresenhamLine(fb, fbW, fbH, static_cast<int>(wip0.x), static_cast<int>(wip0.y),
                          static_cast<int>(wip1.x), static_cast<int>(wip1.y), rgba(30, 41, 59), 1);
    }

    // 9. Roof Panel with White Racing Number Roundel
    {
        std::vector<Vec2D> roofPoly = {
            toScreen(-9.0,   7.0),
            toScreen( 9.0,   7.0),
            toScreen( 8.5,  -5.0),
            toScreen(-8.5,  -5.0)
        };
        fillConvexPolygon(fb, fbW, fbH, roofPoly, spec.roofColor);
        drawPolygonOutline(fb, fbW, fbH, roofPoly, rgba(15, 23, 42), 1);

        // White racing roundel for sharp number contrast
        Vec2D roofCenter = toScreen(0.0, 1.0);
        fillMidpointCircle(fb, fbW, fbH, static_cast<int>(roofCenter.x), static_cast<int>(roofCenter.y), 6, rgba(255, 255, 255));
        drawMidpointCircle(fb, fbW, fbH, static_cast<int>(roofCenter.x), static_cast<int>(roofCenter.y), 6, rgba(15, 23, 42), 1);

        std::string numStr = std::to_string(currentCarIdx + 1);
        drawArcadeText(fb, fbW, fbH,
                       static_cast<int>(roofCenter.x - 3),
                       static_cast<int>(roofCenter.y - 3),
                       numStr, rgba(15, 23, 42), 1, false);
    }

    // 10. Rear Glass & Engine Bay
    {
        std::vector<Vec2D> engineGlass = {
            toScreen(-8.5,  -5.0),
            toScreen( 8.5,  -5.0),
            toScreen( 7.5, -17.0),
            toScreen(-7.5, -17.0)
        };
        fillConvexPolygon(fb, fbW, fbH, engineGlass, rgba(15, 23, 42, 230));

        // Engine cylinder bank details
        Vec2D engL = toScreen(-3.5, -11.0);
        Vec2D engR = toScreen( 3.5, -11.0);
        fillMidpointCircle(fb, fbW, fbH, static_cast<int>(engL.x), static_cast<int>(engL.y), 3, rgba(203, 213, 225));
        fillMidpointCircle(fb, fbW, fbH, static_cast<int>(engR.x), static_cast<int>(engR.y), 3, rgba(203, 213, 225));

        for (double v = -7.0; v >= -15.0; v -= 3.0) {
            Vec2D sl0 = toScreen(-5.5, v);
            Vec2D sl1 = toScreen( 5.5, v);
            drawBresenhamLine(fb, fbW, fbH,
                              static_cast<int>(sl0.x), static_cast<int>(sl0.y),
                              static_cast<int>(sl1.x), static_cast<int>(sl1.y),
                              rgba(100, 116, 139), 1);
        }
    }

    // 11. Side Mirrors with Reflective Glass
    {
        std::vector<Vec2D> leftMirror = {
            toScreen(-13.0, 9.0), toScreen(-18.0, 9.0),
            toScreen(-17.5, 6.5), toScreen(-12.5, 6.5)
        };
        fillConvexPolygon(fb, fbW, fbH, leftMirror, spec.primaryColor);
        drawPolygonOutline(fb, fbW, fbH, leftMirror, rgba(15, 23, 42), 1);
        putPixelSafe(fb, fbW, fbH, static_cast<int>(toScreen(-16.0, 7.8).x), static_cast<int>(toScreen(-16.0, 7.8).y), rgba(226, 232, 240));

        std::vector<Vec2D> rightMirror = {
            toScreen(13.0, 9.0), toScreen(18.0, 9.0),
            toScreen(17.5, 6.5), toScreen(12.5, 6.5)
        };
        fillConvexPolygon(fb, fbW, fbH, rightMirror, spec.primaryColor);
        drawPolygonOutline(fb, fbW, fbH, rightMirror, rgba(15, 23, 42), 1);
        putPixelSafe(fb, fbW, fbH, static_cast<int>(toScreen(16.0, 7.8).x), static_cast<int>(toScreen(16.0, 7.8).y), rgba(226, 232, 240));
    }

    // 12. GT Rear Wing with Aerodynamic Endplates
    {
        Vec2D pylonL0 = toScreen(-6.0, -22.0);
        Vec2D pylonL1 = toScreen(-6.0, -28.0);
        drawBresenhamLine(fb, fbW, fbH, static_cast<int>(pylonL0.x), static_cast<int>(pylonL0.y),
                          static_cast<int>(pylonL1.x), static_cast<int>(pylonL1.y), rgba(15, 23, 42), 2);

        Vec2D pylonR0 = toScreen(6.0, -22.0);
        Vec2D pylonR1 = toScreen(6.0, -28.0);
        drawBresenhamLine(fb, fbW, fbH, static_cast<int>(pylonR0.x), static_cast<int>(pylonR0.y),
                          static_cast<int>(pylonR1.x), static_cast<int>(pylonR1.y), rgba(15, 23, 42), 2);

        // Wing Blade
        std::vector<Vec2D> wingPoly = {
            toScreen(-16.0, -26.0), toScreen( 16.0, -26.0),
            toScreen( 16.0, -30.0), toScreen(-16.0, -30.0)
        };
        fillConvexPolygon(fb, fbW, fbH, wingPoly, spec.wingColor);
        drawPolygonOutline(fb, fbW, fbH, wingPoly, rgba(15, 23, 42), 1);

        // Prominent Endplates in secondary/team color
        std::vector<Vec2D> leftEndplate = {
            toScreen(-17.5, -24.0), toScreen(-15.5, -24.0),
            toScreen(-15.5, -31.5), toScreen(-17.5, -31.5)
        };
        fillConvexPolygon(fb, fbW, fbH, leftEndplate, spec.secondaryColor);
        drawPolygonOutline(fb, fbW, fbH, leftEndplate, rgba(15, 23, 42), 1);

        std::vector<Vec2D> rightEndplate = {
            toScreen(15.5, -24.0), toScreen(17.5, -24.0),
            toScreen(17.5, -31.5), toScreen(15.5, -31.5)
        };
        fillConvexPolygon(fb, fbW, fbH, rightEndplate, spec.secondaryColor);
        drawPolygonOutline(fb, fbW, fbH, rightEndplate, rgba(15, 23, 42), 1);
    }

    // 12. Taillights, Brake Lights, & Reverse Lights!
    {
        Vec2D tailL = toScreen(-10.0, -31.0);
        Vec2D tailR = toScreen( 10.0, -31.0);

        if (isReversing) {
            // Bright White/Cyan Reverse Lamps!
            fillMidpointCircle(fb, fbW, fbH, static_cast<int>(tailL.x), static_cast<int>(tailL.y), 4, rgba(255, 255, 255));
            fillMidpointCircle(fb, fbW, fbH, static_cast<int>(tailR.x), static_cast<int>(tailR.y), 4, rgba(255, 255, 255));
        } else {
            uint32_t brakeCol = isBraking ? rgba(255, 30, 30, 255) : rgba(185, 28, 28, 220);
            int glowRad = isBraking ? 4 : 2;

            fillMidpointCircle(fb, fbW, fbH, static_cast<int>(tailL.x), static_cast<int>(tailL.y), glowRad, brakeCol);
            fillMidpointCircle(fb, fbW, fbH, static_cast<int>(tailR.x), static_cast<int>(tailR.y), glowRad, brakeCol);

            if (isBraking) {
                putPixelSafe(fb, fbW, fbH, static_cast<int>(tailL.x), static_cast<int>(tailL.y), rgba(255, 255, 255));
                putPixelSafe(fb, fbW, fbH, static_cast<int>(tailR.x), static_cast<int>(tailR.y), rgba(255, 255, 255));
            }
        }
    }

    // 13. Chrome Exhaust Tips & Dynamic Animated Nitro Flames
    {
        Vec2D exhL = toScreen(-6.0, -31.5);
        Vec2D exhR = toScreen( 6.0, -31.5);

        drawMidpointCircle(fb, fbW, fbH, static_cast<int>(exhL.x), static_cast<int>(exhL.y), 2, rgba(203, 213, 225));
        drawMidpointCircle(fb, fbW, fbH, static_cast<int>(exhR.x), static_cast<int>(exhR.y), 2, rgba(203, 213, 225));

        if ((isNitro || boostPadTimer > 0.0) && !isReversing) {
            double flameLength = (boostPadTimer > 0.0) ? 40.0 : 30.0;
            double flicker = std::sin(flameAnimTimer) * 4.0;
            flameLength += flicker;

            Vec2D rearDir = (toScreen(0.0, -32.0) - toScreen(0.0, 0.0)).normalized();

            auto drawFlame = [&](const Vec2D &origin) {
                Vec2D flameTip = origin + rearDir * flameLength;
                Vec2D flameSideL = origin + rearDir.rotated( 1.57) * 4.0;
                Vec2D flameSideR = origin + rearDir.rotated(-1.57) * 4.0;

                std::vector<Vec2D> outerFlame = { origin, flameSideL, flameTip, flameSideR };
                uint32_t flameOuterCol = (boostPadTimer > 0.0) ? rgba(250, 204, 21, 230) : rgba(6, 182, 212, 230);
                fillConvexPolygonBlend(fb, fbW, fbH, outerFlame, flameOuterCol);

                Vec2D innerTip = origin + rearDir * (flameLength * 0.55);
                std::vector<Vec2D> innerFlame = { origin, flameSideL * 0.5 + origin * 0.5, innerTip, flameSideR * 0.5 + origin * 0.5 };
                fillConvexPolygon(fb, fbW, fbH, innerFlame, rgba(255, 255, 255));
            };

            drawFlame(exhL);
            drawFlame(exhR);
        } else if (speed > 80.0 && (rand() % 4 == 0)) {
            Vec2D rearDir = (toScreen(0.0, -32.0) - toScreen(0.0, 0.0)).normalized();
            Vec2D popL = exhL + rearDir * (4.0 + (rand() % 5));
            Vec2D popR = exhR + rearDir * (4.0 + (rand() % 5));
            fillMidpointCircle(fb, fbW, fbH, static_cast<int>(popL.x), static_cast<int>(popL.y), 2, rgba(251, 146, 60, 200));
            fillMidpointCircle(fb, fbW, fbH, static_cast<int>(popR.x), static_cast<int>(popR.y), 2, rgba(251, 146, 60, 200));
        }
    }
}
