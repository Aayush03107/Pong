#include "Ai.hpp"
#include "Paddle.hpp"
#include "globals.hpp"
#include <random>
#include <algorithm>

std::random_device rd;
std::mt19937 gen(rd());


AI::AI(Paddle& paddle, float reactionSpeed, float errorMargin): paddle(paddle){
    this->reactionSpeed = reactionSpeed;
    this->errorMargin = errorMargin;
}
void AI::changeReactionSpeed(float s){
    reactionSpeed = s;
};
void AI::changeError(float e){
    errorMargin = e;
};

void AI::Reset(){
    direction = 0;
    reactionClock.restart();
}

void AI::update( Ball&b, float dt){
    auto velocity = b.GetVelocity();
    if(velocity.x > 0){
        sf::Vector2f pos = b.GetPosition();
        std::uniform_real_distribution<float> distribution(-errorMargin, errorMargin);
        

        if (reactionClock.getElapsedTime().asSeconds() >= reactionSpeed) {
            
            auto paddleBounds = paddle.getBounds();
            float t = ((paddleBounds.position.x - 2.f * b.radius)-pos.x)/velocity.x;
            float L = WindowY - 2.f * b.radius;
            
            float targetY = pos.y + t*velocity.y;
            float paddleY = paddleBounds.position.y + paddleBounds.size.y / 2.f;
            float q = targetY;
            float m = std::fmod(q, 2.f * L);
            if (m < 0.f) {
                m += 2.f * L;
            }
            if (m > L) {
                m = 2.f * L - m;
            }
            
            float landingY = b.radius + m +distribution(gen);
            landingY = std::clamp(
                    landingY,
                    Pheight / 2.f,
                    WindowY - Pheight / 2.f
                );
            float difference = landingY  - paddleY;
            if (difference < -10.f) {
                direction = -1;
            }
            else if (difference > 10.f) {
                direction = 1;
            }else{
                direction =0;
            }
            reactionClock.restart();
        }
        if (direction == -1) {
            paddle.moveUp(dt);
        }
        else if (direction == 1) {
            paddle.moveDown(dt);
        }
    } else {
        // Ball moving away — drift back to centre.
        auto  paddleBounds = paddle.getBounds();
        float paddleY      = paddleBounds.position.y + paddleBounds.size.y / 2.f;
        float difference   = (WindowY / 2.f) - paddleY;
        if      (difference < -10.f) paddle.moveUp(dt);
        else if (difference >  10.f) paddle.moveDown(dt);
    }
    
}