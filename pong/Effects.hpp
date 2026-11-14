#pragma once

#include <SFML/Graphics.hpp>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <random>
#include <string>
#include <vector>
#include "globals.hpp"

// ── Color helper ─────────────────────────────────────────────────────────────
inline sf::Color withAlpha(sf::Color col, std::uint8_t a)
{
    return sf::Color(col.r, col.g, col.b, a);
}

inline sf::Color lerpColor(sf::Color a, sf::Color b, float t)
{
    t = std::clamp(t, 0.f, 1.f);
    auto r = static_cast<std::uint8_t>(a.r + (b.r - a.r) * t);
    auto g = static_cast<std::uint8_t>(a.g + (b.g - a.g) * t);
    auto bl = static_cast<std::uint8_t>(a.b + (b.b - a.b) * t);
    auto al = static_cast<std::uint8_t>(a.a + (b.a - a.a) * t);
    return sf::Color(r, g, bl, al);
}

class Effects {
public:
    bool enabled = true;

    // Palette
    const sf::Color leftColor  {80, 160, 255};
    const sf::Color rightColor {255, 150, 60};
    const sf::Color wallColor  {180, 180, 180};
    const sf::Color fastRed    {255, 70, 70};

    struct Particle {
        sf::Vector2f pos;
        sf::Vector2f vel;
        float        initialRadius;
        float        radius;
        float        lifetime;
        float        maxLifetime;
        sf::Color    color;
    };

    Effects()
        : rng(std::random_device{}())
    {
        initGrid();
    }

    void update(float dt)
    {
        totalTime += dt;

        if (shakeTimer > 0.f)
            shakeTimer = std::max(0.f, shakeTimer - dt);

        if (paddleFlashTimer[0] > 0.f)
            paddleFlashTimer[0] = std::max(0.f, paddleFlashTimer[0] - dt);
        if (paddleFlashTimer[1] > 0.f)
            paddleFlashTimer[1] = std::max(0.f, paddleFlashTimer[1] - dt);

        if (scorePopTimer[0] > 0.f)
            scorePopTimer[0] = std::max(0.f, scorePopTimer[0] - dt);
        if (scorePopTimer[1] > 0.f)
            scorePopTimer[1] = std::max(0.f, scorePopTimer[1] - dt);

        if (countdownTimer > 0.f)
            countdownTimer = std::max(0.f, countdownTimer - dt);

        // Tint lerp
        if (tintLerpTimer > 0.f) {
            tintLerpTimer = std::max(0.f, tintLerpTimer - dt);
            float t = 1.0f - tintLerpTimer / 0.15f;
            currentTint = lerpColor(startTint, targetTint, t);
        } else {
            currentTint = targetTint;
        }

        // Speed-based effective ball color
        float speedFactor = std::clamp((currentBallSpeed - 300.f) / (650.f - 300.f), 0.f, 1.f);
        effectiveBallColor = lerpColor(currentTint, fastRed, speedFactor);

        // Particles update
        for (auto it = particles.begin(); it != particles.end();) {
            it->lifetime -= dt;
            if (it->lifetime <= 0.f) {
                it = particles.erase(it);
                continue;
            }
            it->vel *= std::max(0.f, 1.f - 3.f * dt);
            it->pos += it->vel * dt;
            float f = std::clamp(it->lifetime / it->maxLifetime, 0.f, 1.f);
            it->radius = it->initialRadius * (0.3f + 0.7f * f);
            ++it;
        }
    }

    void spawnHit(sf::Vector2f pos, sf::Color col, int count)
    {
        if (!enabled) return;
        while (particles.size() + count > 200 && !particles.empty()) {
            particles.pop_front();
        }
        std::uniform_real_distribution<float> distAngle(0.f, 2.f * 3.14159265f);
        std::uniform_real_distribution<float> distSpeed(80.f, 260.f);
        std::uniform_real_distribution<float> distLife(0.25f, 0.4f);
        std::uniform_real_distribution<float> distRad(2.f, 4.f);

        for (int i = 0; i < count; ++i) {
            float a = distAngle(rng);
            float s = distSpeed(rng);
            float life = distLife(rng);
            float r = distRad(rng);
            particles.push_back({
                pos,
                {s * std::cos(a), s * std::sin(a)},
                r,
                r,
                life,
                life,
                col
            });
        }
    }

    void onPaddleHit(int side /*0=left, 1=right*/, sf::Vector2f ballPos)
    {
        if (side < 0 || side > 1) return;
        paddleFlashTimer[side] = 0.12f;
        lastHitter = side;

        // Ball tint update
        startTint = currentTint;
        targetTint = (side == 0) ? leftColor : rightColor;
        tintLerpTimer = 0.15f;

        if (!enabled) return;

        // Particles
        while (particles.size() + 14 > 200 && !particles.empty()) {
            particles.pop_front();
        }

        // Normal: side 0 points right (0 rad), side 1 points left (PI rad)
        float baseAngle = (side == 0) ? 0.f : 3.14159265f;
        constexpr float maxSpread = 70.f * (3.14159265f / 180.f);
        std::uniform_real_distribution<float> distAngle(baseAngle - maxSpread, baseAngle + maxSpread);
        std::uniform_real_distribution<float> distSpeed(80.f, 260.f);
        std::uniform_real_distribution<float> distLife(0.25f, 0.4f);
        std::uniform_real_distribution<float> distRad(2.f, 4.f);
        sf::Color pCol = (side == 0) ? leftColor : rightColor;

        for (int i = 0; i < 14; ++i) {
            float a = distAngle(rng);
            float s = distSpeed(rng);
            float life = distLife(rng);
            float r = distRad(rng);
            particles.push_back({
                ballPos,
                {s * std::cos(a), s * std::sin(a)},
                r,
                r,
                life,
                life,
                pCol
            });
        }
    }

    void onWallHit(sf::Vector2f ballPos)
    {
        if (!enabled) return;
        while (particles.size() + 8 > 200 && !particles.empty()) {
            particles.pop_front();
        }

        float baseAngle = (ballPos.y < WindowY / 2.f) ? (3.14159265f / 2.f) : (-3.14159265f / 2.f);
        constexpr float maxSpread = 70.f * (3.14159265f / 180.f);
        std::uniform_real_distribution<float> distAngle(baseAngle - maxSpread, baseAngle + maxSpread);
        std::uniform_real_distribution<float> distSpeed(80.f, 260.f);
        std::uniform_real_distribution<float> distLife(0.25f, 0.4f);
        std::uniform_real_distribution<float> distRad(2.f, 4.f);

        for (int i = 0; i < 8; ++i) {
            float a = distAngle(rng);
            float s = distSpeed(rng);
            float life = distLife(rng);
            float r = distRad(rng);
            particles.push_back({
                ballPos,
                {s * std::cos(a), s * std::sin(a)},
                r,
                r,
                life,
                life,
                wallColor
            });
        }
    }

    void onScore(int scorerSide /*0=left, 1=right*/)
    {
        if (scorerSide >= 0 && scorerSide <= 1) {
            scorePopTimer[scorerSide] = 0.3f;
        }
        shakeTimer = 0.2f;

        // Reset ball tint
        startTint = sf::Color::White;
        targetTint = sf::Color::White;
        currentTint = sf::Color::White;
        effectiveBallColor = sf::Color::White;
        tintLerpTimer = 0.f;
        lastHitter = -1;
        clearTrail();

        if (!enabled) return;

        // 24 particles at edge where ball exited
        while (particles.size() + 24 > 200 && !particles.empty()) {
            particles.pop_front();
        }

        // Left scorer scored on right edge (x=WindowX), right scorer scored on left edge (x=0)
        float edgeX = (scorerSide == 0) ? static_cast<float>(WindowX) : 0.f;
        float baseAngle = (scorerSide == 0) ? 3.14159265f : 0.f;
        constexpr float maxSpread = 70.f * (3.14159265f / 180.f);
        std::uniform_real_distribution<float> distAngle(baseAngle - maxSpread, baseAngle + maxSpread);
        std::uniform_real_distribution<float> distSpeed(80.f, 260.f);
        std::uniform_real_distribution<float> distLife(0.25f, 0.4f);
        std::uniform_real_distribution<float> distRad(2.f, 4.f);
        std::uniform_real_distribution<float> distY(100.f, WindowY - 100.f);
        sf::Color col = (scorerSide == 0) ? leftColor : rightColor;

        for (int i = 0; i < 24; ++i) {
            float a = distAngle(rng);
            float s = distSpeed(rng);
            float life = distLife(rng);
            float r = distRad(rng);
            particles.push_back({
                {edgeX, distY(rng)},
                {s * std::cos(a), s * std::sin(a)},
                r,
                r,
                life,
                life,
                col
            });
        }
    }

    void pushTrail(sf::Vector2f ballCenter, float speed)
    {
        currentBallSpeed = speed;
        if (!enabled) {
            trail.clear();
            return;
        }

        float factor = std::clamp((speed - 300.f) / (650.f - 300.f), 0.f, 1.f);
        auto maxPts = static_cast<std::size_t>(6.f + factor * 8.f);

        trail.push_back(ballCenter);
        while (trail.size() > maxPts) {
            trail.pop_front();
        }
    }

    void clearTrail()
    {
        trail.clear();
    }

    void drawBackground(sf::RenderWindow& window)
    {
        if (!enabled) {
            window.clear(sf::Color::Black);
            return;
        }

        // Vertical gradient quad
        sf::VertexArray bg(sf::PrimitiveType::TriangleStrip, 4);
        bg[0] = sf::Vertex{{0.f, 0.f}, sf::Color(10, 10, 22)};
        bg[1] = sf::Vertex{{static_cast<float>(WindowX), 0.f}, sf::Color(10, 10, 22)};
        bg[2] = sf::Vertex{{0.f, static_cast<float>(WindowY)}, sf::Color(4, 4, 10)};
        bg[3] = sf::Vertex{{static_cast<float>(WindowX), static_cast<float>(WindowY)}, sf::Color(4, 4, 10)};
        window.draw(bg);

        // Faint grid
        window.draw(gridVertices);
    }

    void drawDivider(sf::RenderWindow& window)
    {
        sf::RectangleShape seg({5.f, 14.f});
        if (!enabled) {
            seg.setFillColor(sf::Color(80, 80, 80));
            for (float y = 4.f; y < WindowY; y += 19.f) {
                seg.setPosition({397.5f, y});
                window.draw(seg);
            }
            return;
        }

        float alpha = 70.f + 20.f * std::sin(totalTime * (2.f * 3.14159265f / 3.0f));
        seg.setFillColor(sf::Color(60, 60, 70, static_cast<std::uint8_t>(std::clamp(alpha, 50.f, 90.f))));
        for (float y = 4.f; y < WindowY; y += 19.f) {
            seg.setPosition({397.5f, y});
            window.draw(seg);
        }
    }

    void drawTrail(sf::RenderWindow& window)
    {
        if (!enabled || trail.empty()) return;

        sf::CircleShape circle;
        std::size_t n = trail.size();
        for (std::size_t i = 0; i < n; ++i) {
            float t = (n <= 1) ? 1.0f : static_cast<float>(i) / static_cast<float>(n - 1);
            float r = 12.f * (0.3f + 0.6f * t);
            auto a = static_cast<std::uint8_t>(20.f + 120.f * t);

            circle.setRadius(r);
            circle.setOrigin({r, r});
            circle.setPosition(trail[i]);
            circle.setFillColor(withAlpha(effectiveBallColor, a));
            window.draw(circle);
        }
    }

    void drawParticles(sf::RenderWindow& window)
    {
        if (!enabled || particles.empty()) return;

        sf::CircleShape pShape;
        for (const auto& p : particles) {
            float f = std::clamp(p.lifetime / p.maxLifetime, 0.f, 1.f);
            auto a = static_cast<std::uint8_t>(255.f * f);
            pShape.setRadius(p.radius);
            pShape.setOrigin({p.radius, p.radius});
            pShape.setPosition(p.pos);
            pShape.setFillColor(withAlpha(p.color, a));
            window.draw(pShape);
        }
    }

    void drawPaddle(sf::RenderWindow& window, int side /*0=left, 1=right*/, float renderY)
    {
        float baseX = (side == 0) ? 30.f : 730.f;
        sf::Color baseCol = (side == 0) ? leftColor : rightColor;

        if (!enabled) {
            sf::RectangleShape rect({static_cast<float>(Pwidth), static_cast<float>(Pheight)});
            rect.setFillColor(sf::Color::White);
            rect.setPosition({baseX, renderY});
            window.draw(rect);
            return;
        }

        // Squash & flash calculation
        float f = 0.f;
        if (paddleFlashTimer[side] > 0.f) {
            float p = 1.0f - paddleFlashTimer[side] / 0.12f;
            f = 1.0f - std::abs(2.0f * p - 1.0f);
        }

        sf::Color fillCol = lerpColor(baseCol, sf::Color::White, f);
        float scaleX = 1.0f + 0.25f * f;
        float w = static_cast<float>(Pwidth) * scaleX;
        float h = static_cast<float>(Pheight);

        // Anchored on outer edge:
        // Left paddle (side 0): outer edge is x=30.
        // Right paddle (side 1): outer edge is x=730+15=745.
        float posX = (side == 0) ? 30.f : (745.f - w);

        // Outer glow (+8 px each side, alpha 40)
        sf::RectangleShape glowOuter({w + 16.f, h + 16.f});
        glowOuter.setPosition({posX - 8.f, renderY - 8.f});
        glowOuter.setFillColor(withAlpha(baseCol, 40));
        window.draw(glowOuter);

        // Inner glow (+4 px each side, alpha 70)
        sf::RectangleShape glowInner({w + 8.f, h + 8.f});
        glowInner.setPosition({posX - 4.f, renderY - 4.f});
        glowInner.setFillColor(withAlpha(baseCol, 70));
        window.draw(glowInner);

        // Solid paddle
        sf::RectangleShape rect({w, h});
        rect.setPosition({posX, renderY});
        rect.setFillColor(fillCol);
        window.draw(rect);
    }

    void drawBall(sf::RenderWindow& window, sf::Vector2f center, float radius = 12.f)
    {
        if (!enabled) {
            sf::CircleShape b(radius);
            b.setOrigin({radius, radius});
            b.setPosition(center);
            b.setFillColor(sf::Color::White);
            window.draw(b);
            return;
        }

        // Outer glow (x1.8, alpha 40)
        float rOuter = radius * 1.8f;
        sf::CircleShape glowOuter(rOuter);
        glowOuter.setOrigin({rOuter, rOuter});
        glowOuter.setPosition(center);
        glowOuter.setFillColor(withAlpha(effectiveBallColor, 40));
        window.draw(glowOuter);

        // Inner glow (x1.35, alpha 70)
        float rInner = radius * 1.35f;
        sf::CircleShape glowInner(rInner);
        glowInner.setOrigin({rInner, rInner});
        glowInner.setPosition(center);
        glowInner.setFillColor(withAlpha(effectiveBallColor, 70));
        window.draw(glowInner);

        // Solid ball
        sf::CircleShape b(radius);
        b.setOrigin({radius, radius});
        b.setPosition(center);
        b.setFillColor(effectiveBallColor);
        window.draw(b);
    }

    void applyShake(sf::RenderWindow& window)
    {
        if (!enabled || shakeTimer <= 0.f) {
            window.setView(window.getDefaultView());
            return;
        }

        float mag = 8.0f * (shakeTimer / 0.2f);
        std::uniform_real_distribution<float> dist(-mag, mag);
        float offsetX = dist(rng);
        float offsetY = dist(rng);

        sf::View v = window.getDefaultView();
        v.setCenter({WindowX / 2.f + offsetX, WindowY / 2.f + offsetY});
        window.setView(v);
    }

    void resetView(sf::RenderWindow& window)
    {
        window.setView(window.getDefaultView());
    }

    float getScoreScale(int side /*0=left, 1=right*/) const
    {
        if (!enabled || side < 0 || side > 1 || scorePopTimer[side] <= 0.f)
            return 1.0f;
        float p = 1.0f - scorePopTimer[side] / 0.3f;
        return 1.0f + 0.5f * (1.0f - std::abs(2.0f * p - 1.0f));
    }

    void startCountdown(bool isMP)
    {
        countdownTimer = 2.5f;
        countdownIsMP = isMP;
    }

    bool isCountdownActive() const
    {
        return countdownTimer > 0.f;
    }

    void drawCountdown(sf::RenderWindow& window, const sf::Font& font)
    {
        if (!enabled || countdownTimer <= 0.f) return;

        float elapsed = 2.5f - countdownTimer;
        std::string textStr;
        float slotElapsed = 0.f;
        float slotDuration = 0.7f;

        if (elapsed < 0.7f) {
            textStr = "3";
            slotElapsed = elapsed;
            slotDuration = 0.7f;
        } else if (elapsed < 1.4f) {
            textStr = "2";
            slotElapsed = elapsed - 0.7f;
            slotDuration = 0.7f;
        } else if (elapsed < 2.1f) {
            textStr = "1";
            slotElapsed = elapsed - 1.4f;
            slotDuration = 0.7f;
        } else if (elapsed < 2.5f) {
            textStr = "GO!";
            slotElapsed = elapsed - 2.1f;
            slotDuration = 0.4f;
        } else {
            return;
        }

        float u = std::clamp(slotElapsed / slotDuration, 0.f, 1.f);
        float scale = 1.4f - 0.4f * u;
        float maxAlpha = countdownIsMP ? 180.f : 255.f;
        float alphaFactor = std::min(1.0f, slotElapsed / 0.15f);
        auto a = static_cast<std::uint8_t>(maxAlpha * alphaFactor);

        sf::Text cdText(font);
        cdText.setCharacterSize(100);
        cdText.setString(textStr);
        cdText.setFillColor(sf::Color(255, 255, 255, a));

        auto b = cdText.getLocalBounds();
        cdText.setOrigin({b.position.x + b.size.x / 2.f, b.position.y + b.size.y / 2.f});
        cdText.setPosition({WindowX / 2.f, WindowY / 2.f});
        cdText.setScale({scale, scale});

        window.draw(cdText);
    }

    void reset()
    {
        shakeTimer = 0.f;
        paddleFlashTimer[0] = paddleFlashTimer[1] = 0.f;
        scorePopTimer[0] = scorePopTimer[1] = 0.f;
        countdownTimer = 0.f;
        tintLerpTimer = 0.f;
        startTint = targetTint = currentTint = effectiveBallColor = sf::Color::White;
        lastHitter = -1;
        particles.clear();
        trail.clear();
    }

    sf::Color getBallColor() const
    {
        return effectiveBallColor;
    }

private:
    float totalTime = 0.f;
    float shakeTimer = 0.f;
    float paddleFlashTimer[2] = {0.f, 0.f};
    float scorePopTimer[2] = {0.f, 0.f};
    float countdownTimer = 0.f;
    bool  countdownIsMP = false;

    float currentBallSpeed = 300.f;
    sf::Color startTint = sf::Color::White;
    sf::Color targetTint = sf::Color::White;
    sf::Color currentTint = sf::Color::White;
    sf::Color effectiveBallColor = sf::Color::White;
    float tintLerpTimer = 0.f;
    int lastHitter = -1;

    std::deque<sf::Vector2f> trail;
    std::deque<Particle>     particles;

    sf::VertexArray gridVertices{sf::PrimitiveType::Lines};
    std::mt19937    rng;

    void initGrid()
    {
        sf::Color gridColor(255, 255, 255, 6);
        // Vertical lines every 50 px
        for (float x = 50.f; x < WindowX; x += 50.f) {
            gridVertices.append(sf::Vertex{{x, 0.f}, gridColor});
            gridVertices.append(sf::Vertex{{x, static_cast<float>(WindowY)}, gridColor});
        }
        // Horizontal lines every 50 px
        for (float y = 50.f; y < WindowY; y += 50.f) {
            gridVertices.append(sf::Vertex{{0.f, y}, gridColor});
            gridVertices.append(sf::Vertex{{static_cast<float>(WindowX), y}, gridColor});
        }
    }
};
