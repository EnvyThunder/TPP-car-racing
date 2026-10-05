# 🏎️ Apex Grand Prix: Top-View Supercar Racing Simulation

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![Qt](https://img.shields.io/badge/Qt-6.x%20%7C%205.15-41CD52.svg?logo=qt)](https://www.qt.io/)
[![Frame Rate](https://img.shields.io/badge/Frame%20Rate-60%20FPS%20Locked-brightgreen.svg)]()
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)]()

> An arcade top-down (bird's-eye view) supercar racing simulation engineered in **modern C++ (C++17)** with **Qt 6**, powered by a custom **CPU Software Rasterizer** running at a locked **60 FPS**.

---

## 📸 Overview & Visual Highlights

Unlike traditional third-person racers where vehicles are viewed from behind with heavy foreshortening, **Apex Grand Prix** utilizes an overhead bird's-eye perspective so that every vehicle, aerodynamic curve, and track element is rendered in crisp geometric detail:

- 🏎️ **Full Aerodynamic Chassis Silhouettes**: Contoured hoods, front radiator meshes, tinted windshields with specular glints, cockpit roof plates with racing numbers (**#1** through **#5**), side mirrors, rear engine cooling louvers, and dual-tier GT rear wings with endplates.
- 🛞 **Steerable Front Wheels**: When steering left or right, front wheels visibly rotate in real time relative to the car body using local transformation matrices.
- 💡 **Dynamic Headlight Projection Beams**: Twin translucent xenon light cones illuminate asphalt, lane dividers, and corners ahead via software alpha blending.
- 🏁 **Real-Time Rubber Tire Skid Marks**: Dual black tire tracks are deposited on the track surface during high-speed drifts, heavy braking, and burnouts, persisting seamlessly.
- 🔥 **Chrome Exhausts with Nitro Thruster Flames**: Multi-layered glowing fire plumes (white core, electric cyan/amber outer flame) with floating combustion particles ignite when accelerating or boosting.
- 🔴 **Dynamic Brake & Reverse Lights**: Taillights illuminate into bright halogen red beacons when braking, and switch to crisp white lamps when reversing.

---

## 🛣️ Circuit Design & Racing Mechanics

- **Massive 4-Lane Circuits (380px Road Width)**: Over $3.5\times$ wider than typical arcade tracks, accommodating up to 12 supercars side-by-side with 4 dedicated lanes (Inside, Mid-Left, Mid-Right, Outside).
- **Dynamic AI Competitors**: AI rivals select racing lanes, overtake intelligently, and draft behind other vehicles.
- **Fluid Barrier Deflection**: Outer barriers deflect vehicles smoothly forward along the track curve—preventing jarring dead-stops.
- **Controlled Crawl Reverse**: Reversing (`[DOWN]` / `[S]`) operates at a gentle, controlled crawl (~35 km/h) with bright white reverse lamps for effortless maneuvering.
- **Mutual Crash Physics**: Collisions between vehicles trigger realistic momentum exchange, visual spark bursts, smoke puffs, and broadcast HUD alerts.

---

## 🚗 Selectable Supercars & Hypercars

Switch vehicles instantly at any time using **Keys `[1]` to `[5]`** or **`[TAB]`**:

| Key | Model | Livery & Styling | Top Speed | Handling | Accel | Trait |
|:---:|:---|:---|:---:|:---:|:---:|:---|
| **`[1]`** | **Apex Falcon** | Scuderia Corsa Red & White Stripe | 320 km/h | 1.20 rad/s | High | Balanced Italian GT racer |
| **`[2]`** | **Venom Stryker** | Electric Lime Green & Carbon Black | 335 km/h | 1.35 rad/s | Ultra | Sharp cornering & high grip |
| **`[3]`** | **Chiron Mirage** | Royal Cobalt Blue & Electric Cyan | 365 km/h | 1.10 rad/s | High | Maximum top-speed king |
| **`[4]`** | **Sunfire Hyper** | McLaren Papaya Orange & Dark Charcoal | 325 km/h | 1.40 rad/s | Ultra | Precision cornering specialist |
| **`[5]`** | **Ghost Specter** | Stealth Gunmetal & Cyber Violet | 345 km/h | 1.25 rad/s | Very High | Nitro boost specialist |

---

## 🏆 5-Stage Circuit Progression

| Stage | Track | Circuit Type | Lap Count | AI Speed Scale | Description |
|:---:|:---|:---:|:---:|:---:|:---|
| **1** | **Desert Highway Sprint** | Point-to-Point Straight | 1 Lap | 42% (~165 px/s) | Ultra-wide drag highway heading straight North through desert dunes. |
| **2** | **Coastal Highway** | Point-to-Point Sweeper | 1 Lap | 54% (~215 px/s) | Gentle, flowing seaside curves with azure water and palm borders. |
| **3** | **Alpine Canyon Pass** | Point-to-Point Technical | 1 Lap | 65% (~260 px/s) | S-curves through mountain gorges and rock cliff faces. |
| **4** | **Meadow GP Circuit** | Closed Loop Circuit | 3 Laps | 75% (~300 px/s) | Full championship closed circuit with 13 control points and sweeping chicanes. |
| **5** | **Neo Tokyo Expressway** | Closed Loop Championship | 3 Laps | 85% (~340 px/s) | Midnight wet asphalt loop with glowing cyberpunk neon kerbs. |

---

## 🎮 Controls

| Action | Primary Key | Secondary Key | Description |
|:---|:---:|:---:|:---|
| **Drive / Accelerate** | `Up Arrow` | `W` | Progressive forward throttle |
| **Brake / Reverse** | `Down Arrow` | `S` | Gentle brake; stationary crawl reverse |
| **Steer Left** | `Left Arrow` | `A` | Steers front wheels left |
| **Steer Right** | `Right Arrow` | `D` | Steers front wheels right |
| **Handbrake / Drift** | `Shift` | — | Initiates rubber-burning drift |
| **Nitro Boost** | `Spacebar` | — | Ignites dual thruster exhaust flames |
| **Switch Car** | `1` - `5` | `Tab` | Instant vehicle swap |
| **Next Track / Stage** | `L` | `Enter` (on finish) | Advance circuit |
| **Toggle Camera** | `C` | — | Switch between World-Follow and Track-Up |
| **Restart Circuit** | `R` | `Enter` (on game over) | Reset position & lives |
| **Pause / Resume** | `P` | — | Pause menu |
| **Diagnostics Telemetry**| `F1` | — | Toggle Computer Graphics metrics overlay |

---

## 📐 Computer Graphics (CG) Algorithms Implemented

This project is built from scratch without external graphics engines (no OpenGL, DirectX, or Vulkan). Every pixel is drawn via custom software rasterization algorithms in C++:

### 1. Bresenham's Line Algorithm with Thickness
- Used for track boundary lines, dashed lane dividers, finish line checkerboards, headlight projection rays, and HUD elements.
- Implemented with integer arithmetic error terms ($d = 2\Delta y - \Delta x$) and perpendicular span expansion for arbitrary line widths.

### 2. Midpoint Circle & Midpoint Ellipse Algorithms
- Used for wheel alloy rims, tire hubs, circular tachometers/speedometers, radar blips, and particle sprites.
- Utilizes decision variables:
  $$d_1 = r_y^2 - r_x^2 r_y + \frac{1}{4} r_x^2$$
  to evaluate eight-way and four-way symmetry without floating-point trigonometric evaluations.

### 3. 2D Affine Homogeneous Coordinate Transformations (3x3 Matrices)
- Full transformation pipeline mapping local car geometry to screen space:
  $$M_{\text{final}} = M_{\text{Screen}} \times M_{\text{View}} \times M_{\text{World}} \times M_{\text{Local}}$$
- Coordinates are translated, rotated, and scaled through $3 \times 3$ affine matrix multiplications for car chassis, steerable wheels, spoilers, and light cones.

### 4. Scanline Convex Polygon Rasterization & Alpha Blending
- Edge-table sorting and horizontal span interpolation rasterize complex car body panels, windshield glass, and road quads.
- Alpha blending equation:
  $$C_{\text{out}} = \frac{C_{\text{src}} \times \alpha + C_{\text{dst}} \times (255 - \alpha)}{255}$$

### 5. Catmull-Rom Spline Interpolation
- Circuit centerlines are smoothly interpolated from sparse control points:
  $$P(t) = 0.5 \cdot \begin{bmatrix} 1 & t & t^2 & t^3 \end{bmatrix} \begin{bmatrix} 0 & 2 & 0 & 0 \\ -1 & 0 & 1 & 0 \\ 2 & -5 & 4 & -1 \\ -1 & 3 & -3 & 1 \end{bmatrix} \begin{bmatrix} P_0 \\ P_1 \\ P_2 \\ P_3 \end{bmatrix}$$
- Continuous first derivatives yield normal vectors for dynamic track width extrusion.

### 6. Oriented Bounding Box (OBB) & Circle Collision Dynamics
- Accurate vehicle-to-barrier and vehicle-to-vehicle collision detection with impulse response, angular damping, and friction loss.

---

## 📂 Project Structure

```text
cg_car_racing_project/
├── cg_car_racing_project.pro   # Qt QMake Project configuration
├── project_3.pro               # Compatibility project configuration
├── CMakeLists.txt              # Modern CMake build configuration
├── .gitignore                  # Git ignore rules for Qt/C++ builds
├── LICENSE                     # MIT License
├── README.md                   # Complete documentation & guide
├── build.bat                   # 1-click Windows build script
├── run.bat                     # 1-click Windows run script
│
├── Headers/
│   ├── car_topview.h           # Car specifications, liveries & rendering
│   ├── cg_math.h               # CG rasterization algorithms & 3x3 matrices
│   ├── engine_topview.h        # Game engine, physics & state loop
│   ├── mainwindow.h            # Qt MainWindow container
│   ├── raster_canvas.h         # QWidget CPU framebuffer canvas
│   ├── rival_topview.h         # AI rival logic, steering & difficulty
│   └── track_topview.h         # Catmull-Rom spline circuits & barriers
│
├── Sources/
│   ├── car_topview.cpp         # Procedural car drawing & steerable wheels
│   ├── cg_math.cpp             # Bresenham, Midpoint, Scanline implementations
│   ├── engine_topview.cpp      # Race loop, collisions, HUD & telemetry
│   ├── main.cpp                # Application entry point
│   ├── mainwindow.cpp          # Window initialization
│   ├── raster_canvas.cpp       # Framebuffer blitting & input events
│   ├── rival_topview.cpp       # AI behavior & overtaking
│   └── track_topview.cpp       # Track extrusion & surface drawing
│
└── docs/
    ├── Apex_Grand_Prix_Design_Report.pdf    # Full engineering & design report
    └── Apex_Grand_Prix_Code_Flowcharts.pdf   # Complete code flowcharts & architecture
```

---

## 🚀 How to Build & Run

### Method 1: Qt Creator (Recommended)

1. Open **Qt Creator**.
2. Click **File > Open File or Project...** (or `Ctrl + O`).
3. Select `cg_car_racing_project.pro` (or `CMakeLists.txt`).
4. Select your installed Qt Kit (e.g., **Desktop Qt 6.x MinGW 64-bit**).
5. Click **Configure Project**.
6. Press `Ctrl + R` (or the green **Run** arrow in the bottom-left corner).

---

### Method 2: Command Line (Windows Batch Script)

If Qt 6 and MinGW are installed, run:

```powershell
.\build.bat
.\run.bat
```

---

### Method 3: CMake Build

```powershell
mkdir build
cd build
cmake .. -G "Ninja" -DCMAKE_PREFIX_PATH="C:/Qt/6.11.1/mingw_64"
cmake --build .
.\TopViewRacing.exe
```

---

## 📤 Pushing to GitHub

To push this project to your GitHub repository:

```powershell
# Navigate into the project folder
cd cg_car_racing_project

# Initialize git repository
git init

# Add all project files
git add .

# Create the initial commit
git commit -m "Initial commit: Apex Grand Prix top-view racing simulation with Qt 6 CPU rasterizer"

# Rename branch to main
git branch -M main

# Link your remote repository (replace with your repo URL)
git remote add origin https://github.com/<your-username>/<your-repo-name>.git

# Push to GitHub
git push -u origin main
```

---

## 📜 License

This project is licensed under the [MIT License](LICENSE) - feel free to use, modify, and distribute for academic and personal projects.
