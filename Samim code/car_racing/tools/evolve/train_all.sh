#!/bin/sh
# Trains the four brains one after the other and packs them into shared/brains.inc.
# Run from the project folder:  sh tools/evolve/train_all.sh [generations] [population]
# (about 2-3 s a generation on a 12-thread PC; each species can carry on later from its .pop)
set -e
GENS=${1:-150}
POP=${2:-96}
mkdir -p build/evolve
g++ -O2 -std=c++17 -Ishared tools/evolve/evolve.cpp shared/gamesim.cpp shared/track.cpp shared/circuits.cpp \
    shared/playermodel.cpp shared/aidriver.cpp shared/director.cpp shared/roster.cpp shared/brain.cpp \
    -o build/evolve/evolve.exe -pthread
cd build/evolve
for k in 0 1 2 3; do
    ./evolve.exe train $k "$GENS" "$POP" | tail -n 3
    ./evolve.exe baseline $k 4
done
./evolve.exe pack ../../shared/brains.inc
