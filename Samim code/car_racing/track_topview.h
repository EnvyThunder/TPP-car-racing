#ifndef TRACK_TOPVIEW_H
#define TRACK_TOPVIEW_H

#include "cg_math.h"
#include <vector>
#include <string>

enum TrackId {
    TRACK_LEVEL_1_STRAIGHT = 1,
    TRACK_LEVEL_2_CURVED   = 2,
    TRACK_LEVEL_3_WINDING  = 3,
    TRACK_LEVEL_4_LOOP     = 4,
    TRACK_LEVEL_5_LOOP_GP  = 5
};

struct TrackTheme {
    std::string name;
    std::string location;
    uint32_t terrainBaseColor;
    uint32_t terrainPatternColor;
    uint32_t asphaltColor;
    uint32_t asphaltBorderColor;
    uint32_t kerbColor1;
    uint32_t kerbColor2;
    uint32_t centerLineColor;
    uint32_t laneLineColor;
    uint32_t wallColor;
    uint32_t decorColor;
};

struct BoostPad {
    Vec2D pos;
    double angle;
    double width;
    double height;
    double pulseAnim;
};

struct NitroPickup {
    Vec2D pos;
    bool active;
    double respawnTimer;
};

struct OilSlick {
    Vec2D pos;
    double radius;
};

struct TrackNode {
    Vec2D center;
    Vec2D tangent;
    Vec2D normal;
    Vec2D leftEdge;
    Vec2D rightEdge;
    Vec2D leftKerb;
    Vec2D rightKerb;
    double distFromStart;
    bool isCorner;
};

enum PropType {
    PROP_OAK_TREE = 0,
    PROP_PINE_TREE,
    PROP_PALM_TREE,
    PROP_BUSH_CLUSTER,
    PROP_FLOWER_PATCH,
    PROP_BOULDER,
    PROP_TIRE_STACK,
    PROP_LAMP_POST
};

struct RoadsideProp {
    Vec2D pos;
    double scale;
    double rotation;
    PropType type;
    uint32_t color1;
    uint32_t color2;
};

class TopViewTrack {
public:
    TopViewTrack();
    ~TopViewTrack() = default;

    void loadTrack(int trackId);
    int getCurrentTrackId() const { return currentTrackId; }
    const TrackTheme& getTheme() const { return theme; }

    double getTrackLength() const { return totalTrackLength; }
    double getRoadWidth() const { return roadWidth; }
    bool isLoop() const { return isLoopTrack; }
    int getTotalLaps() const { return isLoopTrack ? 3 : 1; }

    // Query track geometry
    Vec2D getPointAtDistance(double dist) const;
    double getDistanceAlongTrack(const Vec2D &point) const;
    bool isPointOnTrack(const Vec2D &point, double &distFromCenter) const;
    bool checkBarrierCollision(const Vec2D &pos, double radius, Vec2D &pushOut) const;
    Vec2D getTangentAtPoint(const Vec2D &point) const;
    Vec2D getTrackCenterAtPoint(const Vec2D &point) const;

    // Interactive element queries
    bool checkBoostPad(const Vec2D &pos, double radius);
    bool checkOilSlick(const Vec2D &pos, double radius);
    bool checkNitroPickup(const Vec2D &pos, double radius);

    // Update animations
    void update(double dt);

    // Rendering in Top View
    void render(uint32_t *fb, int fbW, int fbH, const Mat2D &viewMatrix, const Vec2D &camPos, double viewRadius);

    // Starting positions for grid
    Vec2D getGridPosition(int gridIndex) const;
    double getStartHeading() const { return startHeading; }

    // Spline nodes for AI pathfinding
    const std::vector<TrackNode>& getNodes() const { return nodes; }
    int getNodeCount() const { return static_cast<int>(nodes.size()); }

    // Bounding Box for Minimap
    void getTrackBounds(double &outMinX, double &outMaxX, double &outMinY, double &outMaxY) const;

private:
    int currentTrackId;
    TrackTheme theme;
    double roadWidth;
    double totalTrackLength;
    double startHeading;
    bool isLoopTrack;

    std::vector<Vec2D> controlPoints;
    std::vector<TrackNode> nodes;

    std::vector<BoostPad> boostPads;
    std::vector<NitroPickup> nitroPickups;
    std::vector<OilSlick> oilSlicks;
    std::vector<RoadsideProp> roadsideProps;

    void buildTrackGeometry();
    void generateDecorations();
    void renderRoadsideProps(uint32_t *fb, int fbW, int fbH, const Mat2D &viewMatrix, const Vec2D &camPos, double viewRadius);
};

#endif // TRACK_TOPVIEW_H
