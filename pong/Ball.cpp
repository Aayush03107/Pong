#include "Ball.hpp"
#include "globals.hpp"
#include <cmath>
#include <algorithm>

static constexpr float PI = 3.14159265f;

Ball::Ball(float x, float y, float r, float spd)
    : radius(r), speed(spd), initialX(x), initialY(y)
{
    ball.setRadius(radius);
    ball.setFillColor(sf::Color::White);
    ball.setPosition({x, y});

    // Launch at 45 degrees (PI/4 radians)
    float angle = PI / 4.f;
    velocity.x  = speed * std::cos(angle);
    velocity.y  = speed * std::sin(angle);
}

bool Ball::update(float dt)
{
    auto  pos = ball.getPosition();
    float nx  = pos.x + velocity.x * dt;
    float ny  = pos.y + velocity.y * dt;

    bool bounced = false;
    if (ny + 2.f * radius >= WindowY || ny <= 0.f) {
        velocity.y = -velocity.y;
        bounced    = true;
    }
    ny = std::clamp(ny, 0.f, WindowY - 2.f * radius);
    ball.setPosition({nx, ny});
    return bounced;
}

void Ball::draw(sf::RenderWindow& window)
{
    window.draw(ball);
}

sf::FloatRect Ball::getBounds() const
{
    return ball.getGlobalBounds();
}

void Ball::setSpeed(float s)
{
    // Rescale current velocity to the new speed, preserving direction.
    float len = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
    if (len > 0.f) {
        velocity.x = velocity.x / len * s;
        velocity.y = velocity.y / len * s;
    }
    speed = s;
}

void Ball::handlePaddleCollision(const sf::FloatRect& paddleBounds)
{
    auto  pos = ball.getPosition();

    // Compute bounce angle based on where the ball hit the paddle face.
    float ballCenterY   = pos.y + radius;
    float paddleCenterY = paddleBounds.position.y + paddleBounds.size.y / 2.f;
    float offset = (ballCenterY - paddleCenterY) / (paddleBounds.size.y / 2.f);
    offset = std::clamp(offset, -1.f, 1.f);
    float angle = offset * 60.f * (PI / 180.f);

    bool  movingRight = velocity.x > 0;
    float dir         = movingRight ? -1.f : 1.f;

    // Push the ball clear of the paddle to prevent repeated collisions.
    float paddleLeft  = paddleBounds.position.x;
    float paddleRight = paddleBounds.position.x + paddleBounds.size.x;
    if (movingRight)
        ball.setPosition({paddleLeft - 2.f * radius, pos.y});
    else
        ball.setPosition({paddleRight, pos.y});

    velocity.x = dir  * speed * std::cos(angle);
    velocity.y = speed * std::sin(angle);
}

sf::Vector2f Ball::GetPosition() const { return ball.getPosition(); }
sf::Vector2f Ball::GetVelocity() const { return velocity; }

void Ball::Reset()
{
    ball.setPosition({initialX, initialY});
    velocity.x = -velocity.x;   // Serve toward the player who just conceded.
}
