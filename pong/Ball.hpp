#pragma once
#include <SFML/Graphics.hpp>

class Ball {
public:
    sf::CircleShape ball;
    sf::Vector2f    velocity;
    float           radius;
    float           speed;

    Ball(float x, float y, float radius, float speed);

    // Returns true if the ball bounced off a wall this frame.
    bool          update(float dt);
    void          draw(sf::RenderWindow& window);
    void          handlePaddleCollision(const sf::FloatRect& paddleBounds);
    void          setSpeed(float s);
    void          Reset();

    sf::Vector2f  GetPosition() const;
    sf::Vector2f  GetVelocity() const;
    sf::FloatRect getBounds()   const;

private:
    float initialX;
    float initialY;
};
