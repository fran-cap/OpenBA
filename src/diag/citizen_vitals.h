// src/diag/citizen_vitals.h — see citizen_vitals.cpp.
// An [EntityCensus] and a [Vitals] line at frames 1, 2, 5 .. 1200, then every 1800 frames:
// entity count, citizen count, age/hunger/thirst spread. BANISHED_VITALS=1 enables (off by default).
#pragma once

class GameUpdateLoop;
void CitizenVitals_Tick(GameUpdateLoop* gameWorld);
