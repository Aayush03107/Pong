#pragma once

struct Difficulty {
    const char* name;
    float baseSpeed, hitBoost, centerBonus, ramp, maxSpeed;
    float aiPaddleSpeed, aiReaction, aiError;
};

inline constexpr Difficulty DIFFS[3] = {
    // name    base   boost  ctr   ramp  max     aiSpd  react  err
    {"Easy",   400.f, 10.f,  15.f, 0.f,  550.f,  200.f, 0.35f, 80.f},
    {"Medium", 500.f, 15.f,  30.f, 3.f,  750.f,  300.f, 0.15f, 30.f},
    {"Hard",   650.f, 25.f,  50.f, 6.f, 1000.f,  450.f, 0.05f,  5.f},
};
