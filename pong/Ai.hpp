#pragma once
#include "Paddle.hpp"
#include "Ball.hpp"
#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>
class AI{
   private:
     sf::Clock reactionClock;
     Paddle& paddle;
     float reactionSpeed;
     float errorMargin;
     int direction = 0;
   public:
     AI(Paddle& paddle, float reactionSpeed, float errorMargin);
     void update(Ball& ball, float dt);
     void changeReactionSpeed(float s);
     void changeError(float e);
     void Reset();
 };