#pragma once
#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>
class Paddle{
    public:
        float initialX;
        float initialY;
        Paddle(float x, float y,float speed);
        void moveUp(float deltaTime);
        void moveDown(float deltaTime);
        void draw(sf::RenderWindow& window);
        void setSpeed(float newspeed);
        void setPositionY(float y);
        sf::FloatRect getBounds();
        void Reset();

    private:
        sf::RectangleShape rectangle;
        float speed;
};