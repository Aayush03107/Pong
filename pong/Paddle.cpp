#include "Paddle.hpp"
#include "globals.hpp"
void Paddle::draw(sf::RenderWindow &window){
    window.draw(rectangle);
}
Paddle::Paddle(float x, float y,float speed){
    rectangle.setSize(sf::Vector2f(Pwidth,Pheight));
    rectangle.setFillColor(sf::Color({255,255,255}));
    rectangle.setPosition({x,y});
    initialX = x;
    initialY = y;
    this->speed = speed;
};

void Paddle::moveUp(float deltaTime){
    float deltay = speed*deltaTime;
    auto pos= rectangle.getPosition();
    if(pos.y-deltay >= 0){
        rectangle.move({0,-deltay});
    }else{
        rectangle.move({0,-pos.y});
    }
    
};

void Paddle::moveDown(float deltaTime){
    float deltay = speed*deltaTime;
    auto pos= rectangle.getPosition();
    if(pos.y+deltay+Pheight <= WindowY){
        rectangle.move({0,deltay});
    }else{
        rectangle.move({0,WindowY-pos.y-Pheight});
    }
};
sf::FloatRect Paddle::getBounds(){
    return rectangle.getGlobalBounds();
};
void Paddle::Reset(){
    rectangle.setPosition({initialX,initialY});
}
void Paddle::setSpeed(float newSpeed){
    speed = newSpeed;
}
void Paddle::setPositionY(float y){
    rectangle.setPosition({initialX, y});
}