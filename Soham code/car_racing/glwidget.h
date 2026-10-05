#ifndef GLWIDGET_H
#define GLWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QTimer>
#include <QKeyEvent>
#include <QSet>
#include <vector>
#include <random>

struct GLUquadric;

// ---------------- Game objects ----------------
struct Car {
    float x = 0.0f, z = 0.0f;     // world position (track runs towards -Z)
    float heading = 0.0f;         // radians, 0 = facing -Z, + = turning right
    float speed = 0.0f;           // units per tick
    float camHeading = 0.0f;      // smoothed heading for the chase camera
    float fov = 64.0f;            // dynamic field of view (widens with speed/nitro)
    float nitro = 1.0f;           // 0..1 nitro tank
    bool  nitroOn = false;
    float steerVis = 0.0f;        // visual front-wheel steer angle
    float wheelSpin = 0.0f;       // degrees
    float shake = 0.0f;           // camera shake after collisions
    float r = 1.0f, g = 0.0f, b = 0.0f;
    bool  finished = false;
};

struct Traffic {
    float lane, targetLane;       // lateral offset from road centre
    float z, speed;
    float r, g, b;
    bool  truck;
    bool  parked;
};

struct Obstacle {
    enum Type { Cone, Barrel, Barrier };
    Type  type;
    float x, y, z;
    float hx, hz;                 // half extents (barriers only)
    float vx, vy, vz;             // velocity when knocked away
    float rot, spin;
    bool  knocked;
};

struct Prop {
    enum Type { House, Pine, RoundTree, Lamp, Torch };
    Type  type;
    float x, z, rot, scale;
    int   palette;
};

class GLWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
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

private slots:
    void updateGame();

private:
    enum State { Countdown, Racing, Finished };

    // ---- state ----
    Car p1;                       // RIGHT screen, arrow keys
    Car p2;                       // LEFT screen, WASD
    std::vector<Traffic>  traffic;
    std::vector<Obstacle> obstacles;
    std::vector<Prop>     props;
    State state = Countdown;
    int   tick = 0;
    int   raceTick = 0;
    int   winner = 0;
    QSet<int> keys;
    QTimer *timer = nullptr;
    std::mt19937 rng;

    GLUquadric *quad = nullptr;
    GLuint texGrass = 0, texAsphalt = 0, texStone = 0, texPlaster = 0, texRoof = 0;

    // ---- logic ----
    void  resetRace();
    float rnd(float a, float b);
    float roadCenter(float z) const;
    float roadAngle(float z) const;
    bool  isDown(int key) const { return keys.contains(key); }
    void  updateCar(Car &c, bool up, bool down, bool left, bool right, bool nitro);
    void  collideWorld(Car &c);
    void  collideCars(Car &a, Car &b);
    bool  knock(Obstacle &o, float x, float z, float fx, float fz, float speed);

    // ---- procedural raster textures ----
    void   buildTextures();
    GLuint makeTexture(int w, int h, const std::vector<unsigned char> &rgb);

    // ---- rendering ----
    void renderView(const Car &me, const Car &other, int playerNum, int vx, int vy, int vw, int vh);
    void begin2D(int w, int h);
    void drawSky(int vw, int vh);
    void drawSunMountainsClouds(float ex, float ez);
    void drawTemple(float ex, float ez);
    void drawGround(float ex, float ez);
    void drawRoad(float camZ);
    void drawArch(float z, bool finish);
    void drawProps(float camZ);
    void drawHouse(int palette);
    void drawPine();
    void drawRoundTree();
    void drawLamp();
    void drawTorch(int palette, float phase);
    void drawObstacles(float camZ);
    void drawCar(float x, float z, float heading, float steer, float spin,
                 float r, float g, float b, bool nitro);
    void drawTruck(float x, float z, float heading, float spin, float r, float g, float b);
    void drawWheel(float x, float y, float z, float steerDeg, float spin);
    void drawShadow(float x, float z, float heading, float hw, float hl);
    void drawHUD(const Car &me, const Car &other, int playerNum, int vw, int vh);
    void drawMinimap(int W, int H, float dpr);
    void drawText(const char *s, float x, float y, float ps, float r, float g, float b);
    float textWidth(const char *s, float ps) const;
};

#endif // GLWIDGET_H
