#include "track_topview.h"
#include <cmath>
#include <algorithm>

TopViewTrack::TopViewTrack()
    : currentTrackId(1),
      roadWidth(540.0), // Ultra-wide 540px 4-lane highway!
      totalTrackLength(0.0),
      startHeading(0.0),
      isLoopTrack(false) {
    loadTrack(1);
}

void TopViewTrack::loadTrack(int trackId) {
    currentTrackId = trackId;
    roadWidth = 540.0;
    controlPoints.clear();
    nodes.clear();
    boostPads.clear();
    nitroPickups.clear();
    oilSlicks.clear();
    roadsideProps.clear();

    if (trackId == 1) {
        // LEVEL 1: Pure Straight Sprint (1 Lap)
        isLoopTrack = false;
        theme = {
            "LEVEL 1: DESERT OASIS HIGHWAY", "PURE STRAIGHT (1 SPRINT LAP)",
            rgba(217, 130, 43),   // Warm Golden Dune Sand
            rgba(194, 110, 30),   // Desert Shadow Ribs
            rgba(39, 39, 42),     // Smooth Dark Asphalt
            rgba(255, 255, 255),  // White Border Lines
            rgba(234, 179, 8),    // Yellow Kerb
            rgba(255, 255, 255),  // White Kerb
            rgba(250, 204, 21),   // Yellow Centerline
            rgba(241, 245, 249),  // White Lane Lines
            rgba(100, 116, 139),  // Steel Guardrails
            rgba(16, 185, 129)    // Oasis Greenery
        };

        // Pure straight 4-lane drag highway heading straight North (-Y)
        controlPoints = {
            Vec2D(1000, 4800), // Far South Start Area
            Vec2D(1000, 4000), // Start Line & Staging Zone
            Vec2D(1000, 3000), // High Speed Blast
            Vec2D(1000, 2000), // Midpoint Acceleration
            Vec2D(1000, 1000), // Approach to Finish
            Vec2D(1000,  300)  // Stage Finish Line North
        };
    }
    else if (trackId == 2) {
        // LEVEL 2: Gentle Curving Highway (Non-Circular, 1 Lap)
        isLoopTrack = false;
        theme = {
            "LEVEL 2: COASTAL PARADISE", "GENTLE CURVES (NON-CIRCULAR, 1 LAP)",
            rgba(34, 197, 94),    // Vibrant Tropical Emerald Grass
            rgba(22, 163, 74),    // Deep Lush Lawn
            rgba(39, 44, 52),     // Smooth Coastal Highway Tarmac
            rgba(255, 255, 255),  // White Borders
            rgba(239, 68, 68),    // Red Kerb
            rgba(255, 255, 255),  // White Kerb
            rgba(250, 204, 21),   // Yellow Centerline
            rgba(241, 245, 249),  // White Lane Lines
            rgba(234, 179, 8),    // Gold Guardrail
            rgba(16, 185, 129)    // Coastal Flora
        };

        // Gentle sweeping S-curve point-to-point highway (NOT circular)
        controlPoints = {
            Vec2D( 800, 5000), // Start Straight South
            Vec2D( 800, 4200), // Start Line Straight
            Vec2D(1050, 3500), // Gentle Right Sweeper
            Vec2D(1350, 2800), // Flowing Apex
            Vec2D(1150, 2100), // Gentle Left Transition
            Vec2D( 850, 1400), // Gentle Left Sweeper
            Vec2D( 950,  700), // Final Gentle Sweeper
            Vec2D(1150,  200)  // Finish Line Sprint North
        };
    }
    else if (trackId == 3) {
        // LEVEL 3: Winding Alpine Canyon (Non-Circular, 1 Lap)
        isLoopTrack = false;
        theme = {
            "LEVEL 3: ALPINE CANYON PASS", "WINDING S-CURVES (NON-CIRCULAR, 1 LAP)",
            rgba(30, 41, 59),     // Mountain Slate
            rgba(20, 83, 45),     // Deep Pine Forest
            rgba(45, 55, 72),     // Canyon Asphalt
            rgba(255, 255, 255),  // White Borders
            rgba(250, 204, 21),   // Yellow Kerb
            rgba(56, 189, 248),   // Cyan Kerb
            rgba(250, 204, 21),   // Yellow Centerline
            rgba(241, 245, 249),  // White Lane Lines
            rgba(239, 68, 68),    // Red Safety Barrier
            rgba(15, 23, 42)      // Canyon Boulders
        };

        // More technical winding point-to-point highway (NOT circular)
        controlPoints = {
            Vec2D( 600, 5400), // Canyon Entrance South
            Vec2D( 600, 4600), // Approach Straight
            Vec2D(1100, 3900), // Canyon Turn 1
            Vec2D(1500, 3300), // High Ridge Sweeper
            Vec2D( 950, 2600), // Mountain S-Curve 1
            Vec2D(1550, 1900), // Mountain S-Curve 2
            Vec2D(1100, 1100), // Gorge Bridge Section
            Vec2D( 700,  600), // Canyon Exit Sweeper
            Vec2D( 800,  150)  // Alpine Finish Line North
        };
    }
    else if (trackId == 4) {
        // LEVEL 4: Meadow Grand Prix Circuit (Closed Loop, 3 Laps!)
        isLoopTrack = true;
        theme = {
            "LEVEL 4: MEADOW GP CIRCUIT", "CLOSED LOOP (3 LAPS TO WIN)",
            rgba(34, 139, 34),    // Grass Base
            rgba(46, 160, 67),    // Lawn Mowing Stripes
            rgba(51, 65, 85),     // Smooth Dark Asphalt
            rgba(255, 255, 255),  // White Border Lines
            rgba(220, 38, 38),    // Kerb Red
            rgba(255, 255, 255),  // Kerb White
            rgba(250, 204, 21),   // Centerline Yellow
            rgba(241, 245, 249),  // Lane Dashes White
            rgba(185, 28, 28),    // Barrier Red
            rgba(20, 83, 45)      // Trees
        };

        // High-speed flowing closed loop circuit
        controlPoints = {
            Vec2D( 500, 1850), // Main Straight South
            Vec2D( 500, 1200), // Start/Finish Straight
            Vec2D( 500,  650), // Main Straight North
            Vec2D( 680,  380), // Gentle Turn 1 Entry
            Vec2D(1050,  260), // Turn 1 Sweeping Apex
            Vec2D(1500,  280), // North Straight Sweeper
            Vec2D(1950,  450), // Gentle Turn 2 Entry
            Vec2D(2280,  800), // Turn 2 Sweeping Apex
            Vec2D(2350, 1300), // Back Straight
            Vec2D(2250, 1800), // Gentle Turn 3 Sweeper
            Vec2D(1800, 2100), // South Sweeper Apex
            Vec2D(1150, 2100), // Return Sweeper
            Vec2D( 750, 2000)  // Gentle Entry into Main Straight
        };
    }
    else {
        // LEVEL 5: Neo Tokyo Grand Prix Circuit (Closed Loop, 3 Laps!)
        isLoopTrack = true;
        theme = {
            "LEVEL 5: NEO TOKYO EXPRESSWAY", "CHAMPIONSHIP LOOP (3 LAPS TO WIN)",
            rgba(15, 23, 42),     // Dark Pavement
            rgba(30, 41, 59),     // Sidewalks
            rgba(39, 39, 42),     // Wet Dark Asphalt
            rgba(6, 182, 212),    // Neon Cyan Borders
            rgba(236, 72, 153),   // Neon Pink Kerb
            rgba(6, 182, 212),    // Neon Cyan Kerb
            rgba(250, 204, 21),   // Centerline Neon Yellow
            rgba(6, 182, 212),    // Lane Dashes Cyan
            rgba(168, 85, 247),   // Cyber Barrier
            rgba(14, 165, 233)    // Holograms
        };

        controlPoints = {
            Vec2D( 500, 1800), // Shinjuku Avenue South
            Vec2D( 500, 1100), // Start Line Straight
            Vec2D( 500,  600), // Shinjuku Avenue North
            Vec2D( 750,  300), // Expressway Overpass Entry
            Vec2D(1250,  220), // Shibuya Flyover
            Vec2D(1750,  320), // Akihabara Neon Straight
            Vec2D(2200,  700), // Rainbow Bridge Incline
            Vec2D(2250, 1400), // Tokyo Bay Coastal Section
            Vec2D(1950, 1900), // Roppongi Sweeper
            Vec2D(1450, 2050), // Ginza Underpass Link
            Vec2D( 950, 2000)  // Curve back to Main Strip
        };
    }

    buildTrackGeometry();
    generateDecorations();
}

static Vec2D catmullRom(const Vec2D &p0, const Vec2D &p1, const Vec2D &p2, const Vec2D &p3, double t) {
    double t2 = t * t;
    double t3 = t2 * t;

    double f0 = -0.5 * t3 + t2 - 0.5 * t;
    double f1 =  1.5 * t3 - 2.5 * t2 + 1.0;
    double f2 = -1.5 * t3 + 2.0 * t2 + 0.5 * t;
    double f3 =  0.5 * t3 - 0.5 * t2;

    return Vec2D(
        p0.x * f0 + p1.x * f1 + p2.x * f2 + p3.x * f3,
        p0.y * f0 + p1.y * f1 + p2.y * f2 + p3.y * f3
    );
}

void TopViewTrack::buildTrackGeometry() {
    int numPoints = static_cast<int>(controlPoints.size());
    if (numPoints < 2) return;

    int samplesPerSegment = 24;
    nodes.clear();
    double accumDist = 0.0;

    if (isLoopTrack) {
        for (int i = 0; i < numPoints; i++) {
            const Vec2D &p0 = controlPoints[(i - 1 + numPoints) % numPoints];
            const Vec2D &p1 = controlPoints[i];
            const Vec2D &p2 = controlPoints[(i + 1) % numPoints];
            const Vec2D &p3 = controlPoints[(i + 2) % numPoints];

            for (int step = 0; step < samplesPerSegment; step++) {
                double t = static_cast<double>(step) / samplesPerSegment;
                Vec2D pt = catmullRom(p0, p1, p2, p3, t);

                double tNext = t + 0.01;
                Vec2D ptNext = catmullRom(p0, p1, p2, p3, tNext);
                Vec2D tangent = (ptNext - pt).normalized();
                Vec2D normal = Vec2D(-tangent.y, tangent.x);

                double halfW = roadWidth / 2.0;
                Vec2D leftEdge  = pt - normal * halfW;
                Vec2D rightEdge = pt + normal * halfW;
                Vec2D leftKerb  = pt - normal * (halfW + 18.0);
                Vec2D rightKerb = pt + normal * (halfW + 18.0);

                if (!nodes.empty()) {
                    accumDist += (pt - nodes.back().center).length();
                }

                TrackNode node;
                node.center = pt;
                node.tangent = tangent;
                node.normal = normal;
                node.leftEdge = leftEdge;
                node.rightEdge = rightEdge;
                node.leftKerb = leftKerb;
                node.rightKerb = rightKerb;
                node.distFromStart = accumDist;
                node.isCorner = false;

                nodes.push_back(node);
            }
        }
    } else {
        // Clamped Catmull-Rom for Open / Non-circular sprint tracks
        int numSegments = numPoints - 1;
        for (int i = 0; i < numSegments; i++) {
            const Vec2D &p0 = controlPoints[std::max(0, i - 1)];
            const Vec2D &p1 = controlPoints[i];
            const Vec2D &p2 = controlPoints[i + 1];
            const Vec2D &p3 = controlPoints[std::min(numPoints - 1, i + 2)];

            int steps = (i == numSegments - 1) ? (samplesPerSegment + 1) : samplesPerSegment;
            for (int step = 0; step < steps; step++) {
                double t = static_cast<double>(step) / samplesPerSegment;
                Vec2D pt = catmullRom(p0, p1, p2, p3, t);

                double tNext = t + 0.01;
                Vec2D ptNext = catmullRom(p0, p1, p2, p3, tNext);
                Vec2D tangent = (ptNext - pt).normalized();
                Vec2D normal = Vec2D(-tangent.y, tangent.x);

                double halfW = roadWidth / 2.0;
                Vec2D leftEdge  = pt - normal * halfW;
                Vec2D rightEdge = pt + normal * halfW;
                Vec2D leftKerb  = pt - normal * (halfW + 18.0);
                Vec2D rightKerb = pt + normal * (halfW + 18.0);

                if (!nodes.empty()) {
                    accumDist += (pt - nodes.back().center).length();
                }

                TrackNode node;
                node.center = pt;
                node.tangent = tangent;
                node.normal = normal;
                node.leftEdge = leftEdge;
                node.rightEdge = rightEdge;
                node.leftKerb = leftKerb;
                node.rightKerb = rightKerb;
                node.distFromStart = accumDist;
                node.isCorner = false;

                nodes.push_back(node);
            }
        }
    }

    totalTrackLength = accumDist;

    int totalNodes = static_cast<int>(nodes.size());
    for (int i = 0; i < totalNodes; i++) {
        int lookIdx = isLoopTrack ? ((i + 8) % totalNodes) : std::min(totalNodes - 1, i + 8);
        const TrackNode &curr = nodes[i];
        const TrackNode &next = nodes[lookIdx];
        double dotProd = curr.tangent.dot(next.tangent);
        if (dotProd < 0.96) {
            nodes[i].isCorner = true;
        }
    }

    if (!nodes.empty()) {
        const Vec2D &t = nodes[0].tangent;
        startHeading = std::atan2(t.x, -t.y);
    }
}

void TopViewTrack::generateDecorations() {
    int totalNodes = static_cast<int>(nodes.size());
    if (totalNodes < 10) return;

    if (currentTrackId == 1) {
        // Level 1: Clean straight drag sprint - 2 boost pads & nitro canisters, zero oil slicks!
        std::vector<int> boostIndices = { totalNodes / 3, (totalNodes * 2) / 3 };
        for (int idx : boostIndices) {
            const TrackNode &n = nodes[idx];
            BoostPad pad;
            pad.pos = n.center;
            pad.angle = std::atan2(n.tangent.x, -n.tangent.y);
            pad.width = roadWidth * 0.70;
            pad.height = 42.0;
            pad.pulseAnim = 0.0;
            boostPads.push_back(pad);
        }

        std::vector<int> nitroIndices = { totalNodes / 4, (totalNodes * 3) / 4 };
        for (int idx : nitroIndices) {
            const TrackNode &n = nodes[idx];
            NitroPickup np;
            np.pos = n.center + n.normal * (roadWidth * 0.28);
            np.active = true;
            np.respawnTimer = 0.0;
            nitroPickups.push_back(np);
        }
    } else {
        std::vector<int> boostIndices = {
            totalNodes / 5,
            (totalNodes * 3) / 6,
            (totalNodes * 4) / 5
        };

        for (int idx : boostIndices) {
            const TrackNode &n = nodes[idx];
            BoostPad pad;
            pad.pos = n.center;
            pad.angle = std::atan2(n.tangent.x, -n.tangent.y);
            pad.width = roadWidth * 0.70;
            pad.height = 42.0;
            pad.pulseAnim = 0.0;
            boostPads.push_back(pad);
        }

        std::vector<int> nitroIndices = {
            totalNodes / 4,
            (totalNodes * 3) / 5,
            (totalNodes * 7) / 8
        };

        for (int idx : nitroIndices) {
            const TrackNode &n = nodes[idx];
            NitroPickup np;
            np.pos = n.center + n.normal * (roadWidth * 0.28);
            np.active = true;
            np.respawnTimer = 0.0;
            nitroPickups.push_back(np);
        }

        std::vector<int> oilIndices = {
            totalNodes / 3,
            (totalNodes * 7) / 10
        };

        for (int idx : oilIndices) {
            const TrackNode &n = nodes[idx];
            OilSlick os;
            os.pos = n.center - n.normal * (roadWidth * 0.30);
            os.radius = 28.0;
            oilSlicks.push_back(os);
        }
    }

    // Generate Roadside Nature & Scenery Props along Left and Right Track Edges
    double halfW = roadWidth / 2.0;
    int step = 7;
    for (int i = 0; i < totalNodes; i += step) {
        const TrackNode &n = nodes[i];

        // Random offsets to create organic natural clusters
        double leftDist1  = halfW + 42.0 + (rand() % 45);
        double leftDist2  = halfW + 115.0 + (rand() % 75);
        double rightDist1 = halfW + 42.0 + (rand() % 45);
        double rightDist2 = halfW + 115.0 + (rand() % 75);

        Vec2D pL1 = n.center - n.normal * leftDist1;
        Vec2D pL2 = n.center - n.normal * leftDist2;
        Vec2D pR1 = n.center + n.normal * rightDist1;
        Vec2D pR2 = n.center + n.normal * rightDist2;

        auto createProp = [this](int trackId, bool) -> RoadsideProp {
            RoadsideProp p;
            p.scale = 0.85 + (rand() % 35) * 0.01;
            p.rotation = (rand() % 628) * 0.01;

            if (trackId == 1) { // Desert Oasis: Date Palms, Cacti/Shrubs, Desert Rocks, Lights
                int r = rand() % 10;
                if (r < 4) {
                    p.type = PROP_PALM_TREE;
                    p.color1 = rgba(16, 185, 129);
                    p.color2 = rgba(4, 120, 87);
                } else if (r < 7) {
                    p.type = PROP_BOULDER;
                    p.color1 = rgba(180, 83, 9);
                    p.color2 = rgba(217, 119, 6);
                } else if (r < 9) {
                    p.type = PROP_BUSH_CLUSTER;
                    p.color1 = rgba(101, 163, 13);
                    p.color2 = rgba(163, 230, 53);
                } else {
                    p.type = PROP_LAMP_POST;
                    p.color1 = rgba(148, 163, 184);
                    p.color2 = rgba(254, 240, 138);
                }
            } else if (trackId == 2) { // Coastal Tropical: Palms, Oaks, Hibiscus Bushes, Wildflowers
                int r = rand() % 10;
                if (r < 4) {
                    p.type = PROP_PALM_TREE;
                    p.color1 = rgba(16, 185, 129);
                    p.color2 = rgba(5, 150, 105);
                } else if (r < 7) {
                    p.type = PROP_OAK_TREE;
                    p.color1 = rgba(22, 101, 52);
                    p.color2 = rgba(34, 197, 94);
                } else if (r < 9) {
                    p.type = PROP_FLOWER_PATCH;
                    p.color1 = rgba(244, 63, 94);
                    p.color2 = rgba(250, 204, 21);
                } else {
                    p.type = PROP_BUSH_CLUSTER;
                    p.color1 = rgba(34, 197, 94);
                    p.color2 = rgba(74, 222, 128);
                }
            } else if (trackId == 3) { // Alpine Canyon: Dense Pine Trees, Mountain Boulders, Tire Walls
                int r = rand() % 10;
                if (r < 5) {
                    p.type = PROP_PINE_TREE;
                    p.color1 = rgba(20, 83, 45);
                    p.color2 = rgba(22, 101, 52);
                } else if (r < 8) {
                    p.type = PROP_BOULDER;
                    p.color1 = rgba(71, 85, 105);
                    p.color2 = rgba(148, 163, 184);
                } else {
                    p.type = PROP_TIRE_STACK;
                    p.color1 = rgba(220, 38, 38);
                    p.color2 = rgba(241, 245, 249);
                }
            } else if (trackId == 4) { // Meadow GP: English Countryside Oaks, Pines, Flower patches, Hedges
                int r = rand() % 10;
                if (r < 4) {
                    p.type = PROP_OAK_TREE;
                    p.color1 = rgba(22, 101, 52);
                    p.color2 = rgba(34, 197, 94);
                } else if (r < 6) {
                    p.type = PROP_PINE_TREE;
                    p.color1 = rgba(20, 83, 45);
                    p.color2 = rgba(34, 197, 94);
                } else if (r < 8) {
                    p.type = PROP_BUSH_CLUSTER;
                    p.color1 = rgba(74, 222, 128);
                    p.color2 = rgba(132, 204, 22);
                } else if (r == 8) {
                    p.type = PROP_FLOWER_PATCH;
                    p.color1 = rgba(250, 204, 21);
                    p.color2 = rgba(236, 72, 153);
                } else {
                    p.type = PROP_TIRE_STACK;
                    p.color1 = rgba(220, 38, 38);
                    p.color2 = rgba(241, 245, 249);
                }
            } else { // Neo Tokyo Expressway: Glowing Sakura Cherry Blossoms, Cyber Lamps, Palms
                int r = rand() % 10;
                if (r < 4) {
                    p.type = PROP_OAK_TREE; // Sakura cherry blossom!
                    p.color1 = rgba(244, 114, 182); // Sakura Pink
                    p.color2 = rgba(251, 207, 232); // Light Sakura
                } else if (r < 7) {
                    p.type = PROP_LAMP_POST;
                    p.color1 = rgba(6, 182, 212);
                    p.color2 = rgba(56, 189, 248);
                } else if (r < 9) {
                    p.type = PROP_TIRE_STACK;
                    p.color1 = rgba(236, 72, 153);
                    p.color2 = rgba(6, 182, 212);
                } else {
                    p.type = PROP_PALM_TREE;
                    p.color1 = rgba(168, 85, 247);
                    p.color2 = rgba(192, 132, 252);
                }
            }
            return p;
        };

        RoadsideProp pL_inner = createProp(currentTrackId, false);
        pL_inner.pos = pL1;
        roadsideProps.push_back(pL_inner);

        if (rand() % 2 == 0) {
            RoadsideProp pL_outer = createProp(currentTrackId, true);
            pL_outer.pos = pL2;
            roadsideProps.push_back(pL_outer);
        }

        RoadsideProp pR_inner = createProp(currentTrackId, false);
        pR_inner.pos = pR1;
        roadsideProps.push_back(pR_inner);

        if (rand() % 2 == 0) {
            RoadsideProp pR_outer = createProp(currentTrackId, true);
            pR_outer.pos = pR2;
            roadsideProps.push_back(pR_outer);
        }
    }
}

Vec2D TopViewTrack::getPointAtDistance(double dist) const {
    if (nodes.empty()) return Vec2D(0.0, 0.0);
    if (!isLoopTrack) {
        if (dist <= 0.0) return nodes.front().center;
        if (dist >= totalTrackLength) return nodes.back().center;
    } else {
        while (dist < 0.0) dist += totalTrackLength;
        while (dist >= totalTrackLength) dist -= totalTrackLength;
    }

    for (size_t i = 0; i + 1 < nodes.size(); i++) {
        if (dist >= nodes[i].distFromStart && dist <= nodes[i+1].distFromStart) {
            double segLen = nodes[i+1].distFromStart - nodes[i].distFromStart;
            double t = (segLen > 1e-4) ? (dist - nodes[i].distFromStart) / segLen : 0.0;
            return nodes[i].center * (1.0 - t) + nodes[i+1].center * t;
        }
    }
    return nodes.back().center;
}

double TopViewTrack::getDistanceAlongTrack(const Vec2D &point) const {
    if (nodes.empty()) return 0.0;
    double bestDistSq = 1e12;
    double bestDist = 0.0;

    for (const auto &n : nodes) {
        double dSq = (point - n.center).lengthSquared();
        if (dSq < bestDistSq) {
            bestDistSq = dSq;
            bestDist = n.distFromStart;
        }
    }
    return bestDist;
}

bool TopViewTrack::isPointOnTrack(const Vec2D &point, double &distFromCenter) const {
    if (nodes.empty()) {
        distFromCenter = 999.0;
        return false;
    }

    double bestDistSq = 1e12;
    for (const auto &n : nodes) {
        double dSq = (point - n.center).lengthSquared();
        if (dSq < bestDistSq) {
            bestDistSq = dSq;
        }
    }

    distFromCenter = std::sqrt(bestDistSq);
    return distFromCenter <= (roadWidth / 2.0);
}

Vec2D TopViewTrack::getTangentAtPoint(const Vec2D &point) const {
    if (nodes.empty()) return Vec2D(0.0, -1.0);
    double bestDistSq = 1e12;
    Vec2D tangent = nodes[0].tangent;

    for (const auto &n : nodes) {
        double dSq = (point - n.center).lengthSquared();
        if (dSq < bestDistSq) {
            bestDistSq = dSq;
            tangent = n.tangent;
        }
    }
    return tangent;
}

Vec2D TopViewTrack::getTrackCenterAtPoint(const Vec2D &point) const {
    if (nodes.empty()) return Vec2D(0.0, 0.0);
    double bestDistSq = 1e12;
    Vec2D center = nodes[0].center;

    for (const auto &n : nodes) {
        double dSq = (point - n.center).lengthSquared();
        if (dSq < bestDistSq) {
            bestDistSq = dSq;
            center = n.center;
        }
    }
    return center;
}

bool TopViewTrack::checkBarrierCollision(const Vec2D &pos, double radius, Vec2D &pushOut) const {
    double distFromCenter = 0.0;
    bool onTrack = isPointOnTrack(pos, distFromCenter);

    if (!isLoopTrack) {
        double distAlong = getDistanceAlongTrack(pos);
        if (distAlong < 30.0) {
            Vec2D tangent = getTangentAtPoint(pos);
            pushOut = tangent * (30.0 - distAlong + radius);
            return true;
        }
        if (distAlong > totalTrackLength - 20.0) {
            Vec2D tangent = getTangentAtPoint(pos);
            pushOut = -tangent * (distAlong - (totalTrackLength - 20.0) + radius);
            return true;
        }
    }

    if (onTrack) {
        return false; // Safely on track!
    }

    // Outer barrier boundary limit
    double maxDist = (roadWidth / 2.0) + 14.0;
    if (distFromCenter > maxDist) {
        Vec2D center = getTrackCenterAtPoint(pos);
        Vec2D toTrack = (center - pos).normalized();
        double penetration = (distFromCenter - maxDist) + radius;
        pushOut = toTrack * penetration;
        return true;
    }
    return false;
}

bool TopViewTrack::checkBoostPad(const Vec2D &pos, double radius) {
    for (const auto &bp : boostPads) {
        if ((pos - bp.pos).length() <= (bp.height / 2.0 + radius + 10.0)) {
            return true;
        }
    }
    return false;
}

bool TopViewTrack::checkOilSlick(const Vec2D &pos, double radius) {
    for (const auto &os : oilSlicks) {
        if ((pos - os.pos).length() <= (os.radius + radius)) {
            return true;
        }
    }
    return false;
}

bool TopViewTrack::checkNitroPickup(const Vec2D &pos, double radius) {
    for (auto &np : nitroPickups) {
        if (np.active && (pos - np.pos).length() <= (22.0 + radius)) {
            np.active = false;
            np.respawnTimer = 6.0;
            return true;
        }
    }
    return false;
}

Vec2D TopViewTrack::getGridPosition(int gridIndex) const {
    if (nodes.empty()) return Vec2D(0.0, 0.0);

    // Staggered grid across the 4 massive lanes (widths 135px each):
    // gridIndex 0: Lane 2 (-68 px) - Player Pole Position
    // gridIndex 1: Lane 3 (+68 px) - Rival 1
    // gridIndex 2: Lane 1 (-195 px) - Rival 2
    // gridIndex 3: Lane 4 (+195 px) - Rival 3
    // gridIndex 4: Lane 2 (-68 px) - Rival 4
    double laneW = roadWidth / 4.0;
    double sideOffset = 0.0;
    if (gridIndex == 0) sideOffset = -laneW * 0.5;
    else if (gridIndex == 1) sideOffset = laneW * 0.5;
    else if (gridIndex == 2) sideOffset = -laneW * 1.45;
    else if (gridIndex == 3) sideOffset = laneW * 1.45;
    else sideOffset = ((gridIndex % 2 == 0) ? -laneW * 0.5 : laneW * 0.5);

    Vec2D pt;
    if (!isLoopTrack) {
        // Point-to-point sprint: start line is at S = 420 px
        double carDist = 420.0 - 55.0 - (gridIndex * 55.0);
        pt = getPointAtDistance(std::max(60.0, carDist));
    } else {
        // Closed loop circuit: start line is at S = 0
        double offsetDist = 55.0 + (gridIndex * 65.0);
        pt = getPointAtDistance(totalTrackLength - offsetDist);
    }

    Vec2D tangent = getTangentAtPoint(pt);
    Vec2D normal = Vec2D(-tangent.y, tangent.x);

    return pt + normal * sideOffset;
}

void TopViewTrack::getTrackBounds(double &outMinX, double &outMaxX, double &outMinY, double &outMaxY) const {
    if (nodes.empty()) {
        outMinX = 0; outMaxX = 2000; outMinY = 0; outMaxY = 2000;
        return;
    }
    outMinX = 1e9; outMaxX = -1e9;
    outMinY = 1e9; outMaxY = -1e9;

    for (const auto &n : nodes) {
        if (n.center.x < outMinX) outMinX = n.center.x;
        if (n.center.x > outMaxX) outMaxX = n.center.x;
        if (n.center.y < outMinY) outMinY = n.center.y;
        if (n.center.y > outMaxY) outMaxY = n.center.y;
    }

    outMinX -= 150.0;
    outMaxX += 150.0;
    outMinY -= 150.0;
    outMaxY += 150.0;
}

void TopViewTrack::update(double dt) {
    for (auto &bp : boostPads) {
        bp.pulseAnim += dt * 6.0;
    }

    for (auto &np : nitroPickups) {
        if (!np.active) {
            np.respawnTimer -= dt;
            if (np.respawnTimer <= 0.0) {
                np.active = true;
            }
        }
    }
}

void TopViewTrack::render(uint32_t *fb, int fbW, int fbH, const Mat2D &viewMatrix, const Vec2D &camPos, double viewRadius) {
    // 1. Base Terrain Grass / Ground Grid with World Scrolling
    int camTileY = static_cast<int>(camPos.y);
    int tileSize = 48;
    for (int y = 0; y < fbH; y++) {
        int wy = camTileY + (y - fbH / 2);
        int tileY = (wy >= 0) ? (wy / tileSize) : ((wy - tileSize + 1) / tileSize);
        uint32_t groundCol = (std::abs(tileY) % 2 == 0) ? theme.terrainBaseColor : theme.terrainPatternColor;
        fillScanline(fb, fbW, fbH, y, 0, fbW - 1, groundCol);
    }

    int totalNodes = static_cast<int>(nodes.size());
    if (totalNodes < 4) return;

    double maxDistSq = viewRadius * viewRadius;

    // 2. Render Track Segments
    int endNode = isLoopTrack ? totalNodes : (totalNodes - 1);
    for (int i = 0; i < endNode; i++) {
        int nextIdx = isLoopTrack ? ((i + 1) % totalNodes) : (i + 1);
        const TrackNode &n0 = nodes[i];
        const TrackNode &n1 = nodes[nextIdx];

        if ((n0.center - camPos).lengthSquared() > maxDistSq &&
            (n1.center - camPos).lengthSquared() > maxDistSq) {
            continue;
        }

        Vec2D sL0 = viewMatrix.transform(n0.leftEdge);
        Vec2D sR0 = viewMatrix.transform(n0.rightEdge);
        Vec2D sL1 = viewMatrix.transform(n1.leftEdge);
        Vec2D sR1 = viewMatrix.transform(n1.rightEdge);

        Vec2D sKL0 = viewMatrix.transform(n0.leftKerb);
        Vec2D sKR0 = viewMatrix.transform(n0.rightKerb);
        Vec2D sKL1 = viewMatrix.transform(n1.leftKerb);
        Vec2D sKR1 = viewMatrix.transform(n1.rightKerb);

        // A. Rumble Kerbs on corners, Paved Verge / Gravel Shoulder on straights
        if (n0.isCorner || n1.isCorner) {
            uint32_t kerbCol = ((i / 2) % 2 == 0) ? theme.kerbColor1 : theme.kerbColor2;

            std::vector<Vec2D> leftKerbPoly = { sKL0, sL0, sL1, sKL1 };
            fillConvexPolygon(fb, fbW, fbH, leftKerbPoly, kerbCol);

            std::vector<Vec2D> rightKerbPoly = { sR0, sKR0, sKR1, sR1 };
            fillConvexPolygon(fb, fbW, fbH, rightKerbPoly, kerbCol);

            // Ridge / bevel divider line
            drawBresenhamLine(fb, fbW, fbH, static_cast<int>(sKL0.x), static_cast<int>(sKL0.y),
                              static_cast<int>(sKL1.x), static_cast<int>(sKL1.y), rgba(15, 23, 42, 120), 1);
            drawBresenhamLine(fb, fbW, fbH, static_cast<int>(sKR0.x), static_cast<int>(sKR0.y),
                              static_cast<int>(sKR1.x), static_cast<int>(sKR1.y), rgba(15, 23, 42, 120), 1);
        } else {
            // High-grade paved shoulder verge
            uint32_t shoulderCol = scaleBrightness(theme.asphaltColor, 0.78);
            std::vector<Vec2D> leftShoulder = { sKL0, sL0, sL1, sKL1 };
            fillConvexPolygon(fb, fbW, fbH, leftShoulder, shoulderCol);

            std::vector<Vec2D> rightShoulder = { sR0, sKR0, sKR1, sR1 };
            fillConvexPolygon(fb, fbW, fbH, rightShoulder, shoulderCol);
        }

        // B. Wide Asphalt Surface Quad with subtle texture grading
        uint32_t segAsphalt = ((i / 4) % 2 == 0) ? theme.asphaltColor : scaleBrightness(theme.asphaltColor, 0.96);
        std::vector<Vec2D> roadPoly = { sL0, sR0, sR1, sL1 };
        fillConvexPolygon(fb, fbW, fbH, roadPoly, segAsphalt);

        // C. Continuous Solid Outer Border Lines
        drawBresenhamLine(fb, fbW, fbH,
                          static_cast<int>(sL0.x), static_cast<int>(sL0.y),
                          static_cast<int>(sL1.x), static_cast<int>(sL1.y),
                          theme.asphaltBorderColor, 3);

        drawBresenhamLine(fb, fbW, fbH,
                          static_cast<int>(sR0.x), static_cast<int>(sR0.y),
                          static_cast<int>(sR1.x), static_cast<int>(sR1.y),
                          theme.asphaltBorderColor, 3);

        // D. 3 Dashed Lane Lines (separating 4 dedicated massive lanes!)
        double laneW = roadWidth / 4.0;
        if ((i % 3) != 0) {
            // Lane 1 divider (offset -laneW)
            Vec2D l1_0 = viewMatrix.transform(n0.center - n0.normal * laneW);
            Vec2D l1_1 = viewMatrix.transform(n1.center - n1.normal * laneW);
            drawBresenhamLine(fb, fbW, fbH, static_cast<int>(l1_0.x), static_cast<int>(l1_0.y),
                              static_cast<int>(l1_1.x), static_cast<int>(l1_1.y), theme.laneLineColor, 2);

            // Centerline divider (offset 0)
            Vec2D c0 = viewMatrix.transform(n0.center);
            Vec2D c1 = viewMatrix.transform(n1.center);
            drawBresenhamLine(fb, fbW, fbH, static_cast<int>(c0.x), static_cast<int>(c0.y),
                              static_cast<int>(c1.x), static_cast<int>(c1.y), theme.centerLineColor, 3);

            // Lane 3 divider (offset +laneW)
            Vec2D l2_0 = viewMatrix.transform(n0.center + n0.normal * laneW);
            Vec2D l2_1 = viewMatrix.transform(n1.center + n1.normal * laneW);
            drawBresenhamLine(fb, fbW, fbH, static_cast<int>(l2_0.x), static_cast<int>(l2_0.y),
                              static_cast<int>(l2_1.x), static_cast<int>(l2_1.y), theme.laneLineColor, 2);
        } else {
            // Reflective Cat's-Eye Road Studs in the dashed line gaps
            Vec2D studL = viewMatrix.transform(n0.center - n0.normal * laneW);
            Vec2D studC = viewMatrix.transform(n0.center);
            Vec2D studR = viewMatrix.transform(n0.center + n0.normal * laneW);

            fillMidpointCircle(fb, fbW, fbH, static_cast<int>(studL.x), static_cast<int>(studL.y), 2, rgba(254, 240, 138));
            fillMidpointCircle(fb, fbW, fbH, static_cast<int>(studC.x), static_cast<int>(studC.y), 3, rgba(250, 204, 21));
            fillMidpointCircle(fb, fbW, fbH, static_cast<int>(studR.x), static_cast<int>(studR.y), 2, rgba(254, 240, 138));
        }
    }

    // 3. Start & Finish Lines
    if (isLoopTrack) {
        const TrackNode &startNode = nodes[0];
        Vec2D sL = viewMatrix.transform(startNode.leftEdge);
        Vec2D sR = viewMatrix.transform(startNode.rightEdge);

        int numCheckers = 28;
        for (int c = 0; c < numCheckers; c++) {
            double t0 = static_cast<double>(c) / numCheckers;
            double t1 = static_cast<double>(c + 1) / numCheckers;

            Vec2D p0 = sL * (1.0 - t0) + sR * t0;
            Vec2D p1 = sL * (1.0 - t1) + sR * t1;

            Vec2D fwd = viewMatrix.transform(startNode.center + startNode.tangent * 24.0) - viewMatrix.transform(startNode.center);

            std::vector<Vec2D> checkQuad = {
                p0, p1, p1 + fwd, p0 + fwd
            };
            uint32_t col = (c % 2 == 0) ? rgba(255, 255, 255) : rgba(15, 23, 42);
            fillConvexPolygon(fb, fbW, fbH, checkQuad, col);
        }

        Vec2D sCenter = viewMatrix.transform(startNode.center);
        drawArcadeText(fb, fbW, fbH, static_cast<int>(sCenter.x - 30), static_cast<int>(sCenter.y - 24), "START / FINISH", rgba(250, 204, 21), 1, true);
    } else {
        // A. Start Line across road at S = 420 px
        int startNodeIdx = 0;
        for (size_t i = 0; i < nodes.size(); i++) {
            if (nodes[i].distFromStart >= 420.0) {
                startNodeIdx = static_cast<int>(i);
                break;
            }
        }
        if (startNodeIdx < totalNodes) {
            const TrackNode &sNode = nodes[startNodeIdx];
            Vec2D sL = viewMatrix.transform(sNode.leftEdge);
            Vec2D sR = viewMatrix.transform(sNode.rightEdge);
            Vec2D fwd = viewMatrix.transform(sNode.center + sNode.tangent * 24.0) - viewMatrix.transform(sNode.center);

            int numCheckers = 28;
            for (int c = 0; c < numCheckers; c++) {
                double t0 = static_cast<double>(c) / numCheckers;
                double t1 = static_cast<double>(c + 1) / numCheckers;
                Vec2D p0 = sL * (1.0 - t0) + sR * t0;
                Vec2D p1 = sL * (1.0 - t1) + sR * t1;
                std::vector<Vec2D> checkQuad = { p0, p1, p1 + fwd, p0 + fwd };
                uint32_t col = (c % 2 == 0) ? rgba(34, 197, 94) : rgba(255, 255, 255);
                fillConvexPolygon(fb, fbW, fbH, checkQuad, col);
            }
            Vec2D sCenter = viewMatrix.transform(sNode.center);
            drawArcadeText(fb, fbW, fbH, static_cast<int>(sCenter.x - 24), static_cast<int>(sCenter.y - 24), "START LINE", rgba(34, 197, 94), 1, true);
        }

        // B. Checkered Stage Finish Line near end of the sprint
        int finishNodeIdx = std::max(0, totalNodes - 3);
        const TrackNode &fNode = nodes[finishNodeIdx];
        Vec2D fL = viewMatrix.transform(fNode.leftEdge);
        Vec2D fR = viewMatrix.transform(fNode.rightEdge);
        Vec2D fwd = viewMatrix.transform(fNode.center + fNode.tangent * 28.0) - viewMatrix.transform(fNode.center);

        int numCheckers = 28;
        for (int c = 0; c < numCheckers; c++) {
            double t0 = static_cast<double>(c) / numCheckers;
            double t1 = static_cast<double>(c + 1) / numCheckers;
            Vec2D p0 = fL * (1.0 - t0) + fR * t0;
            Vec2D p1 = fL * (1.0 - t1) + fR * t1;
            std::vector<Vec2D> checkQuad = { p0, p1, p1 + fwd, p0 + fwd };
            uint32_t col = (c % 2 == 0) ? rgba(250, 204, 21) : rgba(15, 23, 42);
            fillConvexPolygon(fb, fbW, fbH, checkQuad, col);
        }
        Vec2D fCenter = viewMatrix.transform(fNode.center);
        drawArcadeText(fb, fbW, fbH, static_cast<int>(fCenter.x - 30), static_cast<int>(fCenter.y - 24), "STAGE FINISH", rgba(250, 204, 21), 1, true);
    }

    // 4. Interactive Speed Boost Pads
    for (const auto &bp : boostPads) {
        if ((bp.pos - camPos).lengthSquared() > maxDistSq) continue;

        Mat2D padMat = Mat2D::multiply(Mat2D::translation(bp.pos.x, bp.pos.y), Mat2D::rotation(bp.angle));
        Mat2D padToScreen = Mat2D::multiply(viewMatrix, padMat);

        double hw = bp.width / 2.0;
        double hh = bp.height / 2.0;

        std::vector<Vec2D> padBox = {
            padToScreen.transform(-hw,  hh),
            padToScreen.transform( hw,  hh),
            padToScreen.transform( hw, -hh),
            padToScreen.transform(-hw, -hh)
        };

        double pulse = 0.5 + 0.5 * std::sin(bp.pulseAnim);
        uint32_t padBg = lerpRgba(rgba(14, 165, 233, 210), rgba(234, 179, 8, 230), pulse);
        fillConvexPolygonBlend(fb, fbW, fbH, padBox, padBg);
        drawPolygonOutline(fb, fbW, fbH, padBox, rgba(255, 255, 255), 2);

        for (double v = -12.0; v <= 12.0; v += 12.0) {
            Vec2D tip = padToScreen.transform(0.0, v + 8.0);
            Vec2D lArm = padToScreen.transform(-hw * 0.6, v - 5.0);
            Vec2D rArm = padToScreen.transform( hw * 0.6, v - 5.0);

            drawBresenhamLine(fb, fbW, fbH, static_cast<int>(tip.x), static_cast<int>(tip.y), static_cast<int>(lArm.x), static_cast<int>(lArm.y), rgba(255, 255, 255), 3);
            drawBresenhamLine(fb, fbW, fbH, static_cast<int>(tip.x), static_cast<int>(tip.y), static_cast<int>(rArm.x), static_cast<int>(rArm.y), rgba(255, 255, 255), 3);
        }
    }

    // 5. Oil Slicks
    for (const auto &os : oilSlicks) {
        if ((os.pos - camPos).lengthSquared() > maxDistSq) continue;
        Vec2D sPos = viewMatrix.transform(os.pos);
        fillMidpointCircle(fb, fbW, fbH, static_cast<int>(sPos.x), static_cast<int>(sPos.y), static_cast<int>(os.radius), rgba(15, 23, 42, 220));
        drawMidpointCircle(fb, fbW, fbH, static_cast<int>(sPos.x - 3), static_cast<int>(sPos.y - 3), static_cast<int>(os.radius * 0.6), rgba(99, 102, 241, 160), 2);
    }

    // 6. Nitro Pickup Canisters
    for (const auto &np : nitroPickups) {
        if (!np.active || (np.pos - camPos).lengthSquared() > maxDistSq) continue;
        Vec2D sPos = viewMatrix.transform(np.pos);
        int sx = static_cast<int>(sPos.x);
        int sy = static_cast<int>(sPos.y);

        fillMidpointCircle(fb, fbW, fbH, sx, sy, 16, rgba(6, 182, 212, 100));
        fillMidpointCircle(fb, fbW, fbH, sx, sy, 9, rgba(6, 182, 212, 220));
        fillMidpointCircle(fb, fbW, fbH, sx, sy, 4, rgba(255, 255, 255, 255));
        drawArcadeText(fb, fbW, fbH, sx - 4, sy - 4, "N", rgba(255, 255, 255), 1, false);
    }

    // 7. Render Rich Roadside Nature & Scenery Props (Trees, Palms, Pines, Bushes, Rocks, Lamps)
    renderRoadsideProps(fb, fbW, fbH, viewMatrix, camPos, viewRadius);
}

void TopViewTrack::renderRoadsideProps(uint32_t *fb, int fbW, int fbH, const Mat2D &viewMatrix, const Vec2D &camPos, double viewRadius) {
    double maxDistSq = (viewRadius + 220.0) * (viewRadius + 220.0);

    for (const auto &p : roadsideProps) {
        if ((p.pos - camPos).lengthSquared() > maxDistSq) continue;

        Vec2D sp = viewMatrix.transform(p.pos);
        int sx = static_cast<int>(sp.x);
        int sy = static_cast<int>(sp.y);

        // Offscreen culling margin
        if (sx < -75 || sx > fbW + 75 || sy < -75 || sy > fbH + 75) continue;

        double s = p.scale;

        switch (p.type) {
            case PROP_OAK_TREE: {
                int r = static_cast<int>(32.0 * s);
                // Drop shadow
                fillMidpointCircle(fb, fbW, fbH, sx + static_cast<int>(6 * s), sy + static_cast<int>(7 * s), r, rgba(15, 23, 42, 85));

                // Outer foliage canopy
                fillMidpointCircle(fb, fbW, fbH, sx, sy, r, p.color1);

                // Leafy perimeter lobes
                int lobeR = static_cast<int>(r * 0.55);
                fillMidpointCircle(fb, fbW, fbH, sx - static_cast<int>(12 * s), sy - static_cast<int>(8 * s), lobeR, p.color2);
                fillMidpointCircle(fb, fbW, fbH, sx + static_cast<int>(12 * s), sy - static_cast<int>(7 * s), lobeR, p.color2);
                fillMidpointCircle(fb, fbW, fbH, sx - static_cast<int>(8 * s), sy + static_cast<int>(11 * s), lobeR, p.color2);
                fillMidpointCircle(fb, fbW, fbH, sx + static_cast<int>(9 * s), sy + static_cast<int>(10 * s), lobeR, p.color2);

                // Sunlit canopy crown (top-left highlight)
                uint32_t lightGreen = scaleBrightness(p.color2, 1.18);
                fillMidpointCircle(fb, fbW, fbH, sx - static_cast<int>(5 * s), sy - static_cast<int>(6 * s), static_cast<int>(r * 0.42), lightGreen);

                // Trunk center
                fillMidpointCircle(fb, fbW, fbH, sx, sy, static_cast<int>(3.5 * s), rgba(69, 39, 21));
                break;
            }

            case PROP_PINE_TREE: {
                int r = static_cast<int>(26.0 * s);
                // Drop shadow
                fillMidpointCircle(fb, fbW, fbH, sx + static_cast<int>(5 * s), sy + static_cast<int>(6 * s), r, rgba(15, 23, 42, 80));

                // Tier 1: Wide dark evergreen base
                fillMidpointCircle(fb, fbW, fbH, sx, sy, r, p.color1);

                // Radial spiky needles
                for (int a = 0; a < 8; a++) {
                    double ang = p.rotation + a * (3.14159 / 4.0);
                    int nx = sx + static_cast<int>(std::cos(ang) * (r + 4 * s));
                    int ny = sy + static_cast<int>(std::sin(ang) * (r + 4 * s));
                    drawBresenhamLine(fb, fbW, fbH, sx, sy, nx, ny, p.color1, 2);
                }

                // Tier 2: Mid-tier emerald layer
                fillMidpointCircle(fb, fbW, fbH, sx, sy, static_cast<int>(r * 0.68), p.color2);

                // Tier 3: Bright conical apex top
                uint32_t tipCol = scaleBrightness(p.color2, 1.25);
                fillMidpointCircle(fb, fbW, fbH, sx - static_cast<int>(2 * s), sy - static_cast<int>(2 * s), static_cast<int>(r * 0.35), tipCol);
                fillMidpointCircle(fb, fbW, fbH, sx, sy, static_cast<int>(2.5 * s), rgba(254, 240, 138));
                break;
            }

            case PROP_PALM_TREE: {
                int r = static_cast<int>(30.0 * s);
                // Drop shadow
                fillMidpointCircle(fb, fbW, fbH, sx + static_cast<int>(5 * s), sy + static_cast<int>(5 * s), static_cast<int>(r * 0.8), rgba(15, 23, 42, 75));

                // 6 spreading palm fronds
                int numFronds = 6;
                for (int f = 0; f < numFronds; f++) {
                    double ang = p.rotation + f * (6.28318 / numFronds);
                    int tipX = sx + static_cast<int>(std::cos(ang) * r);
                    int tipY = sy + static_cast<int>(std::sin(ang) * r);

                    // Frond mid rib
                    drawBresenhamLine(fb, fbW, fbH, sx, sy, tipX, tipY, p.color1, 3);
                    // Highlight on spine
                    drawBresenhamLine(fb, fbW, fbH, sx, sy, tipX, tipY, p.color2, 1);

                    // Fan leaves on sides of frond
                    double sideAng1 = ang + 0.35;
                    double sideAng2 = ang - 0.35;
                    int l1X = sx + static_cast<int>(std::cos(sideAng1) * (r * 0.7));
                    int l1Y = sy + static_cast<int>(std::sin(sideAng1) * (r * 0.7));
                    int l2X = sx + static_cast<int>(std::cos(sideAng2) * (r * 0.7));
                    int l2Y = sy + static_cast<int>(std::sin(sideAng2) * (r * 0.7));
                    drawBresenhamLine(fb, fbW, fbH, sx, sy, l1X, l1Y, p.color1, 2);
                    drawBresenhamLine(fb, fbW, fbH, sx, sy, l2X, l2Y, p.color1, 2);
                }

                // Palm tree core trunk crown
                fillMidpointCircle(fb, fbW, fbH, sx, sy, static_cast<int>(6.0 * s), rgba(120, 53, 15));
                fillMidpointCircle(fb, fbW, fbH, sx, sy, static_cast<int>(3.0 * s), rgba(180, 83, 9));
                break;
            }

            case PROP_BUSH_CLUSTER: {
                int r = static_cast<int>(18.0 * s);
                // Drop shadow
                fillMidpointCircle(fb, fbW, fbH, sx + static_cast<int>(4 * s), sy + static_cast<int>(4 * s), r, rgba(15, 23, 42, 70));

                // 3 interlocking bush mounds
                fillMidpointCircle(fb, fbW, fbH, sx - static_cast<int>(6 * s), sy - static_cast<int>(3 * s), static_cast<int>(10 * s), p.color1);
                fillMidpointCircle(fb, fbW, fbH, sx + static_cast<int>(6 * s), sy - static_cast<int>(2 * s), static_cast<int>(10 * s), p.color1);
                fillMidpointCircle(fb, fbW, fbH, sx, sy + static_cast<int>(5 * s), static_cast<int>(11 * s), p.color2);

                // Leaf highlights
                fillMidpointCircle(fb, fbW, fbH, sx - static_cast<int>(3 * s), sy - static_cast<int>(3 * s), static_cast<int>(5 * s), scaleBrightness(p.color2, 1.2));
                // Flower berries
                fillMidpointCircle(fb, fbW, fbH, sx - static_cast<int>(4 * s), sy + static_cast<int>(4 * s), 2, rgba(244, 63, 94));
                fillMidpointCircle(fb, fbW, fbH, sx + static_cast<int>(5 * s), sy + static_cast<int>(3 * s), 2, rgba(250, 204, 21));
                break;
            }

            case PROP_FLOWER_PATCH: {
                int rx = static_cast<int>(20.0 * s);
                int ry = static_cast<int>(14.0 * s);
                // Grass turf mound
                fillMidpointEllipse(fb, fbW, fbH, sx, sy, rx, ry, rgba(34, 197, 94, 210));

                // Scattered colorful blossom dots
                uint32_t flowerCols[4] = { p.color1, p.color2, rgba(255, 255, 255), rgba(244, 63, 94) };
                int offsets[6][2] = {
                    { -10, -4 }, { -3, -6 }, { 8, -5 },
                    { -7, 4 }, { 3, 3 }, { 10, 4 }
                };
                for (int fi = 0; fi < 6; fi++) {
                    int fx = sx + static_cast<int>(offsets[fi][0] * s);
                    int fy = sy + static_cast<int>(offsets[fi][1] * s);
                    fillMidpointCircle(fb, fbW, fbH, fx, fy, 2, flowerCols[fi % 4]);
                    putPixelSafe(fb, fbW, fbH, fx, fy, rgba(254, 240, 138));
                }
                break;
            }

            case PROP_BOULDER: {
                int r = static_cast<int>(18.0 * s);
                // Drop shadow
                fillMidpointCircle(fb, fbW, fbH, sx + static_cast<int>(4 * s), sy + static_cast<int>(5 * s), r, rgba(15, 23, 42, 85));

                // Irregular boulder polygon
                std::vector<Vec2D> rockPts;
                for (int a = 0; a < 6; a++) {
                    double ang = p.rotation + a * (6.28318 / 6.0);
                    double radMult = (a % 2 == 0) ? 1.05 : 0.85;
                    rockPts.push_back(Vec2D(sx + std::cos(ang) * r * radMult,
                                            sy + std::sin(ang) * r * radMult));
                }
                fillConvexPolygon(fb, fbW, fbH, rockPts, p.color1);

                // Highlighted top crest
                std::vector<Vec2D> crestPts = {
                    rockPts[4], rockPts[5], rockPts[0], Vec2D(sx, sy)
                };
                fillConvexPolygon(fb, fbW, fbH, crestPts, p.color2);

                // Crack lines
                drawBresenhamLine(fb, fbW, fbH, sx, sy, static_cast<int>(rockPts[2].x), static_cast<int>(rockPts[2].y), rgba(30, 41, 59), 1);
                break;
            }

            case PROP_TIRE_STACK: {
                // Safety tire barrier cluster (3 interlocking tires)
                int tireR = static_cast<int>(8.0 * s);
                int offsets[3][2] = {
                    { -static_cast<int>(7 * s), 0 },
                    { static_cast<int>(7 * s), 0 },
                    { 0, -static_cast<int>(8 * s) }
                };
                for (int t = 0; t < 3; t++) {
                    int tx = sx + offsets[t][0];
                    int ty = sy + offsets[t][1];
                    uint32_t tCol = (t % 2 == 0) ? p.color1 : p.color2;

                    // Outer tire
                    fillMidpointCircle(fb, fbW, fbH, tx, ty, tireR, tCol);
                    drawMidpointCircle(fb, fbW, fbH, tx, ty, tireR, rgba(15, 23, 42), 1);
                    // Inner rim hole
                    fillMidpointCircle(fb, fbW, fbH, tx, ty, static_cast<int>(3.5 * s), rgba(15, 23, 42));
                }
                break;
            }

            case PROP_LAMP_POST: {
                int poleR = static_cast<int>(4.0 * s);
                // Ambient glow circle on ground
                fillMidpointCircle(fb, fbW, fbH, sx, sy, static_cast<int>(36.0 * s), rgba(254, 240, 138, 40));

                // Metallic base
                fillMidpointCircle(fb, fbW, fbH, sx, sy, poleR + 2, rgba(30, 41, 59));
                fillMidpointCircle(fb, fbW, fbH, sx, sy, poleR, p.color1);

                // Luminaire lamp head
                fillMidpointCircle(fb, fbW, fbH, sx - 1, sy - 1, poleR - 1, p.color2);
                fillMidpointCircle(fb, fbW, fbH, sx, sy, 2, rgba(255, 255, 255));
                break;
            }
        }
    }
}

