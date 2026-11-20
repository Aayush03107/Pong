#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <array>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include "Ball.hpp"
#include "Paddle.hpp"
#include "Ai.hpp"
#include "globals.hpp"
#include "Network.hpp"
#include "ResourcePath.hpp"
#include "Effects.hpp"
#include "Difficulty.hpp"


// Defined here; declared extern in globals.hpp.
const int WindowX = 800;
const int WindowY = 600;
const int Pwidth  = 15;
const int Pheight = 100;

// ── Game states ───────────────────────────────────────────────────────────────
enum class GameState { ModeSelect, DifficultySelect, Waiting, NoOpponent, OpponentLeft, Ready, Playing, Gameover };

// ── Procedural audio ──────────────────────────────────────────────────────────
static sf::SoundBuffer makeBeep(float freq, float duration)
{
    constexpr unsigned int SR = 44100;
    const auto n = static_cast<std::uint64_t>(SR * duration);
    std::vector<std::int16_t> s(n);
    for (std::uint64_t i = 0; i < n; ++i) {
        float t   = static_cast<float>(i) / SR;
        float env = t > duration * 0.7f ? (duration - t) / (duration * 0.3f) : 1.f;
        s[i] = static_cast<std::int16_t>(
                   32767.f * env * std::sin(2.f * 3.14159265f * freq * t));
    }
    return sf::SoundBuffer(s.data(), n, 1u, SR, {sf::SoundChannel::Mono});
}

static sf::SoundBuffer makeArpeggio(std::initializer_list<float> freqs,
                                     float noteDuration)
{
    constexpr unsigned int SR = 44100;
    const int ns  = static_cast<int>(SR * noteDuration);
    const int tot = ns * static_cast<int>(freqs.size());
    std::vector<std::int16_t> s(tot);
    int idx = 0;
    for (float f : freqs)
        for (int i = 0; i < ns; ++i, ++idx) {
            float t   = static_cast<float>(i) / SR;
            float env = t > noteDuration * 0.6f
                        ? (noteDuration - t) / (noteDuration * 0.4f) : 1.f;
            s[idx] = static_cast<std::int16_t>(
                         32767.f * env * std::sin(2.f * 3.14159265f * f * t));
        }
    return sf::SoundBuffer(s.data(), static_cast<std::uint64_t>(tot),
                           1u, SR, {sf::SoundChannel::Mono});
}

// ── Layout helpers ────────────────────────────────────────────────────────────
// Center a text object horizontally within [0, width] at the given y position.
static void centerX(sf::Text& t, float width, float y)
{
    auto b = t.getLocalBounds();
    t.setPosition({(width - b.size.x) / 2.f - b.position.x, y});
}

// ── Dead-reckoning helper ─────────────────────────────────────────────────────
struct BallState { float x, y, vx, vy; };
static void stepBall(BallState& b, float dt)
{
    b.x += b.vx * dt;
    b.y += b.vy * dt;
    const float maxY = 600.f - 24.f;   // WindowY - 2*BallRadius
    if      (b.y < 0.f)   { b.y = -b.y;             b.vy = -b.vy; }
    else if (b.y > maxY)  { b.y = 2.f*maxY - b.y;   b.vy = -b.vy; }
}

// ─────────────────────────────────────────────────────────────────────────────
int main()
{
    auto serverIp = sf::IpAddress::resolve("129.225.99.54");
    
    if (!serverIp) {
        std::cerr << "Could not resolve server IP\n";
        return 1;
    }
    
    Network network(*serverIp, 5050);
    sf::RenderWindow window(
        sf::VideoMode({static_cast<unsigned>(WindowX),
                       static_cast<unsigned>(WindowY)}),
        "Pong", sf::Style::Close);
    window.setVerticalSyncEnabled(true);

    // Font — expected relative to the executable (CMake copies assets/ there).
    sf::Font font;
    auto fontPath = getResourceDirectory() / "assets/fonts/game.ttf";
    if (!font.openFromFile(fontPath)) {
        return -1; 
    }
    // ── Sounds (all procedurally generated, no files needed) ─────────────────
    const sf::SoundBuffer bufPaddle  = makeBeep(480.f,  0.08f);
    const sf::SoundBuffer bufWall    = makeBeep(320.f,  0.05f);
    const sf::SoundBuffer bufScore   = makeBeep(220.f,  0.45f);
    const sf::SoundBuffer bufVictory = makeArpeggio({523.f, 659.f, 784.f}, 0.18f);
    const sf::SoundBuffer bufDefeat  = makeBeep(180.f,  0.65f);

    sf::Sound sndPaddle (bufPaddle);
    sf::Sound sndWall   (bufWall);
    sf::Sound sndScore  (bufScore);
    sf::Sound sndVictory(bufVictory);
    sf::Sound sndDefeat (bufDefeat);

    // ── Game objects ──────────────────────────────────────────────────────────
    Paddle p1(30.f,  250.f, 300.f);   // Human player (left)
    Paddle p2(730.f, 250.f, 300.f);   // AI opponent (right)
    Ball   ball(388.f, 288.f, 12.f, 500.f);
    AI     opponent(p2, 0.15f, 30.f);

    // ── Score state ───────────────────────────────────────────────────────────
    int scoreP1 = 0, scoreP2 = 0;
    constexpr int WIN_SCORE = 5;

    // Factory for Text objects with a shared font.
    auto makeText = [&](unsigned size,
                        sf::Color col = sf::Color::White) -> sf::Text
    {
        sf::Text t(font);
        t.setCharacterSize(size);
        t.setFillColor(col);
        return t;
    };

    sf::Text txtP1Score = makeText(52);
    sf::Text txtP2Score = makeText(52);
    txtP1Score.setPosition({280.f, 12.f});
    txtP2Score.setPosition({468.f, 12.f});
    txtP1Score.setString("0");
    txtP2Score.setString("0");

    // ── Match state ───────────────────────────────────────────────────────────
    GameState state          = GameState::ModeSelect;
    bool      isMultiplayer  = false;
    bool      playerWon      = false;
    bool      waitAfterScore = false;
    sf::Clock pauseClock;
    sf::Clock frameClock;

    Effects   effects;
    bool      showFPS           = false;
    bool      showSpeedInfo     = false;
    int       currentDiffIndex  = -1;
    float     fpsTimer          = 0.f;
    int       fpsFrames         = 0;
    int       currentFPS        = 60;
    sf::Text  txtFPS            = makeText(16, sf::Color(180, 180, 180));
    sf::Text  txtSpeed          = makeText(16, sf::Color(180, 180, 180));
    float     totalRunTime      = 0.f;
    float     transitionTimer   = 0.f;
    sf::Clock modeSelectClock;
    float     oppConnectedTimer = 0.f;

    auto setState = [&](GameState newState) {
        if (state != newState) {
            state = newState;
            transitionTimer = 0.2f;
            if (newState == GameState::ModeSelect) {
                modeSelectClock.restart();
            }
        }
    };

    txtFPS.setPosition({15.f, static_cast<float>(WindowY) - 28.f});

    bool pressedup = false;
    bool pressedDown = false;
    int  lastSentDir = 0;  // -1 = up, 0 = stop, 1 = down
    float prevBallX = 388.f, prevBallY = 288.f;
    float prevBallDX = 0.f,  prevBallDY = 0.f;
    int   myPlayerId  = 0;         // 1 or 2, assigned by server ASSIGN message
    int   resendTimer = 0;         // counts frames since last input was sent
    float targetP1Y   = 250.f, targetP2Y   = 250.f;
    float renderP1Y   = 250.f, renderP2Y   = 250.f;
    float prevVelX    = 0.f,   prevVelY    = 0.f;   // previous server velocity for sound detection
    // ── Multiplayer-specific state ────────────────────────────────────────────
    BallState renderBall   = {388.f, 288.f, 0.f, 0.f}; // dead-reckoned render ball
    uint32_t  lastTick     = 0;          // out-of-order UDP rejection
    float     oneWayLatency = 0.05f;     // estimated half-RTT in seconds
    constexpr float MP_PADDLE_SPEED = 600.f;
    float     myPaddleY    = 250.f;      // client-side predicted Y for my paddle
    sf::Clock lastMoveClock;             // time since my paddle last moved
    sf::Clock pingClock;                 // time since last PING sent
    sf::Clock pingRttClock;             // RTT measurement of outstanding PING
    bool      pingPending  = false;      // a PING is in-flight
    sf::Clock lastStateReceived;         // for disconnect detection
    sf::Clock waitingClock;             // tracks how long we've been in Waiting state
    // Resets everything for a fresh match (called when returning to menu).
    auto resetMatch = [&]() {
        scoreP1 = scoreP2 = 0;
        txtP1Score.setString("0");
        txtP2Score.setString("0");
        waitAfterScore = false;
        ball.Reset();
        p1.Reset();
        p2.Reset();
        opponent.Reset();
        // Reset multiplayer-specific state.
        lastTick    = 0;
        myPlayerId  = 0;
        myPaddleY   = 250.f;
        pingPending = false;
        renderBall  = {388.f, 288.f, 0.f, 0.f};
        prevVelX = prevVelY = 0.f;
        targetP1Y = renderP1Y = 250.f;
        targetP2Y = renderP2Y = 250.f;
        effects.reset();
        oppConnectedTimer = 0.f;
        currentDiffIndex = -1;
    };

    // Draws the dashed centre divider used during gameplay.
    auto drawDivider = [&]() {
        sf::RectangleShape seg({5.f, 14.f});
        seg.setFillColor(sf::Color(80, 80, 80));
        for (float y = 4.f; y < WindowY; y += 19.f) {
            seg.setPosition({397.5f, y});
            window.draw(seg);
        }
    };
    // ── Main loop ─────────────────────────────────────────────────────────────
    while (window.isOpen())
    {
        // ── Events ────────────────────────────────────────────────────────────
        while (const std::optional ev = window.pollEvent())
        {
            if (ev->is<sf::Event::Closed>()) window.close();
            if(const auto* key = ev->getIf<sf::Event::KeyReleased>())
            {
            };
            if (const auto* key = ev->getIf<sf::Event::KeyPressed>())
            {
                if (key->code == sf::Keyboard::Key::F1) {
                    effects.enabled = !effects.enabled;
                }
                else if (key->code == sf::Keyboard::Key::F2) {
                    showFPS = !showFPS;
                }
                else if (key->code == sf::Keyboard::Key::F3) {
                    showSpeedInfo = !showSpeedInfo;
                }

                // ESC quits from the Game Over screen.
                else if (key->code == sf::Keyboard::Key::Escape &&
                    state == GameState::Gameover)
                    window.close();

                // Any key on the No-Opponent screen returns to mode select.
                else if (state == GameState::NoOpponent) {
                    network.sendMessage(R"({"Typeo":"QUIT"})");
                    resetMatch();
                    setState(GameState::ModeSelect);
                }

                // Any key on Opponent-Left screen returns to mode select.
                else if (state == GameState::OpponentLeft) {
                    network.sendMessage(R"({"Typeo":"QUIT"})");
                    resetMatch();
                    setState(GameState::ModeSelect);
                }

                // Mode selection — choose AI or Multiplayer.
                else if (state == GameState::ModeSelect)
                {
                    if (key->code == sf::Keyboard::Key::Num1) {
                        isMultiplayer = false;
                        setState(GameState::DifficultySelect);
                    }
                    else if (key->code == sf::Keyboard::Key::Num2) {
                        isMultiplayer = true;
                        myPlayerId    = 0;
                        lastSentDir   = 0;
                        resendTimer   = 0;
                        network.sendMessage(R"({"Typeo":"JOIN"})");
                        waitingClock.restart();
                        setState(GameState::Waiting);
                    }
                }

                // Difficulty selection — AI mode only.
                else if (state == GameState::DifficultySelect)
                {
                    int chosenDiff = -1;
                    if (key->code == sf::Keyboard::Key::Num1)
                        chosenDiff = 0;
                    else if (key->code == sf::Keyboard::Key::Num2)
                        chosenDiff = 1;
                    else if (key->code == sf::Keyboard::Key::Num3)
                        chosenDiff = 2;

                    if (chosenDiff != -1) {
                        currentDiffIndex = chosenDiff;
                        const auto& d = DIFFS[chosenDiff];
                        p2.setSpeed(d.aiPaddleSpeed);
                        opponent.changeReactionSpeed(d.aiReaction);
                        opponent.changeError(d.aiError);
                        ball.setDifficulty(d);
                        setState(GameState::Ready);
                    }
                }
                else if (key->code == sf::Keyboard::Key::Space)
                {
                    if (state == GameState::Ready) {
                        setState(GameState::Playing);
                        frameClock.restart();   // avoid a large dt on first frame
                        if (!isMultiplayer) {
                            waitAfterScore = true;
                            effects.startCountdown(false);
                            pauseClock.restart();
                        }
                    }
                    else if (state == GameState::Gameover) {
                        resetMatch();
                        setState(GameState::ModeSelect);
                    }
                }
            }
        }

        // ── Update ────────────────────────────────────────────────────────────
        const float dt = frameClock.restart().asSeconds();
        effects.update(dt);
        totalRunTime += dt;
        transitionTimer = std::max(0.f, transitionTimer - dt);
        if (oppConnectedTimer > 0.f)
            oppConnectedTimer = std::max(0.f, oppConnectedTimer - dt);

        fpsTimer += dt;
        fpsFrames++;
        if (fpsTimer >= 0.5f) {
            currentFPS = static_cast<int>(std::round(fpsFrames / fpsTimer));
            txtFPS.setString("FPS: " + std::to_string(currentFPS));
            fpsFrames = 0;
            fpsTimer = 0.f;
        }

        // Waiting: drain packets; handle ASSIGN then transition on first STATE.
        if (state == GameState::Waiting)
        {
            // 40-second timeout — no opponent found.
            if (waitingClock.getElapsedTime().asSeconds() > 40.f) {
                network.sendMessage(R"({"Typeo":"QUIT"})");
                setState(GameState::NoOpponent);
            }

            while (auto raw = network.receiveMessage()) {
                // Handle PONG (latency reply) — skip without mis-parsing.
                if (*raw == "PONG") {
                    if (pingPending) {
                        float rtt = pingRttClock.getElapsedTime().asSeconds();
                        oneWayLatency = oneWayLatency * 0.8f + (rtt * 0.5f) * 0.2f;
                        oneWayLatency = std::max(0.01f, std::min(oneWayLatency, 0.2f));
                        pingPending = false;
                    }
                    continue;
                }

                int pid;
                if (std::sscanf(raw->c_str(),
                    R"({"type":"ASSIGN","pid":%d})", &pid) == 1)
                { myPlayerId = pid; continue; }

                uint32_t t; float bx, by, vx, vy, p1y, p2y; int s1, s2;
                if (std::sscanf(raw->c_str(),
                    R"({"type":"STATE","t":%u,"bx":%f,"by":%f,"vx":%f,"vy":%f,"p1y":%f,"p2y":%f,"s1":%d,"s2":%d})",
                    &t, &bx, &by, &vx, &vy, &p1y, &p2y, &s1, &s2) == 9)
                {
                    // Snap all positions on initial entry; initialise renderBall.
                    lastTick    = t;
                    renderBall  = {bx, by, vx, vy};
                    myPaddleY   = (myPlayerId == 2) ? p2y : p1y;
                    targetP1Y   = renderP1Y = p1y;
                    targetP2Y   = renderP2Y = p2y;
                    lastStateReceived.restart();
                    setState(GameState::Playing);
                    effects.startCountdown(true);
                    oppConnectedTimer = 2.0f;
                    frameClock.restart();
                    break;
                }
            }
        }

        if (state == GameState::Playing)
        {
            if (!isMultiplayer)
            {
                // ── AI mode: local simulation ─────────────────────────────────
                if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Up))
                    p1.moveUp(dt);
                if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Down))
                    p1.moveDown(dt);

                bool countdownActive = effects.enabled ? effects.isCountdownActive() : waitAfterScore;
                if (!effects.enabled && waitAfterScore) {
                    if (pauseClock.getElapsedTime().asMilliseconds() >= 1000)
                        waitAfterScore = false;
                } else if (effects.enabled && waitAfterScore) {
                    if (!effects.isCountdownActive())
                        waitAfterScore = false;
                }

                if (!countdownActive)
                {
                    ball.applyRamp(dt);

                    if (ball.update(dt)) {
                        sndWall.play();
                        effects.onWallHit(ball.ball.getPosition() + sf::Vector2f(12.f, 12.f));
                    }

                    opponent.update(ball, dt);

                    if (ball.velocity.x < 0 &&
                        ball.getBounds().findIntersection(p1.getBounds()))
                    {
                        ball.handlePaddleCollision(p1.getBounds());
                        sndPaddle.play();
                        effects.onPaddleHit(0, ball.ball.getPosition() + sf::Vector2f(12.f, 12.f));
                    }
                    if (ball.velocity.x > 0 &&
                        ball.getBounds().findIntersection(p2.getBounds()))
                    {
                        ball.handlePaddleCollision(p2.getBounds());
                        sndPaddle.play();
                        effects.onPaddleHit(1, ball.ball.getPosition() + sf::Vector2f(12.f, 12.f));
                    }

                    const auto bb = ball.getBounds();
                    if (bb.position.x + bb.size.x < 0) {
                        ++scoreP2;
                        txtP2Score.setString(std::to_string(scoreP2));
                        ball.Reset();
                        sndScore.play();
                        effects.onScore(1);
                        effects.startCountdown(false);
                        waitAfterScore = true;
                        pauseClock.restart();
                    } else if (bb.position.x > WindowX) {
                        ++scoreP1;
                        txtP1Score.setString(std::to_string(scoreP1));
                        ball.Reset();
                        sndScore.play();
                        effects.onScore(0);
                        effects.startCountdown(false);
                        waitAfterScore = true;
                        pauseClock.restart();
                    }

                    if (scoreP1 >= WIN_SCORE || scoreP2 >= WIN_SCORE) {
                        playerWon = scoreP1 >= WIN_SCORE;
                        setState(GameState::Gameover);
                        if (playerWon) sndVictory.play();
                        else           sndDefeat.play();
                    }
                }

                float spd = countdownActive ? 0.f : std::sqrt(ball.velocity.x * ball.velocity.x + ball.velocity.y * ball.velocity.y);
                effects.pushTrail(ball.ball.getPosition() + sf::Vector2f(12.f, 12.f), spd);
            }
            else
            {
                // ── Multiplayer: server-authoritative ─────────────────────────

                // ── PING for latency measurement (every 1 s) ──────────────────
                if (!pingPending && pingClock.getElapsedTime().asSeconds() >= 1.f) {
                    network.sendMessage(R"({"Typeo":"PING"})");
                    pingRttClock.restart();
                    pingPending = true;
                    pingClock.restart();
                }

                // ── Input: send on change OR every 10 frames (keep-alive) ─────
                int wantDir = 0;
                if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Up) &&
                    !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Down))
                    wantDir = -1;
                else if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Down) &&
                         !sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Up))
                    wantDir = 1;

                ++resendTimer;
                if (wantDir != lastSentDir || resendTimer >= 10) {
                    if      (wantDir == -1) network.sendMessage(R"({"Typeo":"INPUT","Action":"MOVE_UP"})");
                    else if (wantDir ==  1) network.sendMessage(R"({"Typeo":"INPUT","Action":"MOVE_DOWN"})");
                    else                    network.sendMessage(R"({"Typeo":"INPUT","Action":"STOP"})");
                    lastSentDir = wantDir;
                    resendTimer = 0;
                }

                // ── Drain all queued packets; keep highest-tick STATE ─────────
                bool     gotState  = false;
                uint32_t latestT   = lastTick;
                float    latestBx  = renderBall.x, latestBy  = renderBall.y;
                float    latestVx  = renderBall.vx, latestVy  = renderBall.vy;
                float    latestP1y = targetP1Y,   latestP2y = targetP2Y;
                int      latestS1  = scoreP1,     latestS2  = scoreP2;

                while (auto raw = network.receiveMessage()) {
                    // PONG reply.
                    if (*raw == "PONG") {
                        if (pingPending) {
                            float rtt = pingRttClock.getElapsedTime().asSeconds();
                            oneWayLatency = oneWayLatency * 0.8f + (rtt * 0.5f) * 0.2f;
                            oneWayLatency = std::max(0.01f, std::min(oneWayLatency, 0.2f));
                            pingPending = false;
                        }
                        continue;
                    }

                    int pid;
                    if (std::sscanf(raw->c_str(),
                        R"({"type":"ASSIGN","pid":%d})", &pid) == 1)
                    { myPlayerId = pid; continue; }

                    int winner;
                    if (std::sscanf(raw->c_str(),
                        R"({"type":"GAME_OVER","winner":%d})", &winner) == 1)
                    {
                        playerWon = (myPlayerId == winner);
                        setState(GameState::Gameover);
                        if (playerWon) sndVictory.play();
                        else           sndDefeat.play();
                        break;
                    }

                    uint32_t t; float bx, by, vx, vy, p1y, p2y; int s1, s2;
                    if (std::sscanf(raw->c_str(),
                        R"({"type":"STATE","t":%u,"bx":%f,"by":%f,"vx":%f,"vy":%f,"p1y":%f,"p2y":%f,"s1":%d,"s2":%d})",
                        &t, &bx, &by, &vx, &vy, &p1y, &p2y, &s1, &s2) == 9)
                    {
                        // Discard out-of-order (stale) packets.
                        if (t <= lastTick) continue;
                        gotState  = true;
                        latestT   = t;
                        latestBx  = bx;  latestBy  = by;
                        latestVx  = vx;  latestVy  = vy;
                        latestP1y = p1y; latestP2y = p2y;
                        latestS1  = s1;  latestS2  = s2;
                    }
                }

                if (gotState && state == GameState::Playing) {
                    lastTick = latestT;
                    lastStateReceived.restart();
                    targetP1Y = latestP1y; targetP2Y = latestP2y;

                    bool scoreChanged = (latestS1 != scoreP1 || latestS2 != scoreP2);

                    // Sound: velocity sign change on the received packets (not render vel).
                    if (!scoreChanged) {
                        if (prevVelX != 0.f && latestVx != 0.f && prevVelX * latestVx < 0.f) {
                            sndPaddle.play();
                            int side = (latestVx > 0.f) ? 0 : 1;
                            effects.onPaddleHit(side, {renderBall.x + 12.f, renderBall.y + 12.f});
                        }
                        if (prevVelY != 0.f && latestVy != 0.f && prevVelY * latestVy < 0.f) {
                            sndWall.play();
                            effects.onWallHit({renderBall.x + 12.f, renderBall.y + 12.f});
                        }
                    }
                    prevVelX = latestVx; prevVelY = latestVy;

                    if (scoreChanged) {
                        int scorerSide = (latestS1 != scoreP1) ? 0 : 1;
                        scoreP1 = latestS1; scoreP2 = latestS2;
                        txtP1Score.setString(std::to_string(scoreP1));
                        txtP2Score.setString(std::to_string(scoreP2));
                        sndScore.play();
                        effects.onScore(scorerSide);
                        effects.startCountdown(true);
                        // Snap ball immediately on score reset.
                        renderBall = {latestBx, latestBy, latestVx, latestVy};
                    } else {
                        // Build authoritative ball state, compensated by one-way latency.
                        BallState auth{latestBx, latestBy, latestVx, latestVy};
                        stepBall(auth, oneWayLatency);

                        // Always adopt server velocity.
                        renderBall.vx = auth.vx;
                        renderBall.vy = auth.vy;

                        float ex = auth.x - renderBall.x;
                        float ey = auth.y - renderBall.y;
                        if (ex * ex + ey * ey > 100.f * 100.f) {
                            // Large error: snap.
                            renderBall.x = auth.x;
                            renderBall.y = auth.y;
                        } else {
                            // Small error: single blend step.
                            renderBall.x += ex * 0.3f;
                            renderBall.y += ey * 0.3f;
                        }
                    }
                }

                // Disconnect detection: no STATE for 5 s — show Opponent Left.
                if (state == GameState::Playing &&
                    lastStateReceived.getElapsedTime().asSeconds() > 5.f)
                {
                    resetMatch();
                    setState(GameState::OpponentLeft);
                }

                // ── Dead reckoning: advance ball every frame ──────────────────
                stepBall(renderBall, dt);

                float mpSpd = std::sqrt(renderBall.vx * renderBall.vx + renderBall.vy * renderBall.vy);
                effects.pushTrail({renderBall.x + 12.f, renderBall.y + 12.f}, mpSpd);

                // ── Client-side paddle prediction ─────────────────────────────
                if (myPlayerId != 0) {
                    float& myRenderY  = (myPlayerId == 1) ? renderP1Y : renderP2Y;
                    float& myTargetY  = (myPlayerId == 1) ? targetP1Y : targetP2Y;

                    if (wantDir != 0) {
                        myPaddleY += wantDir * MP_PADDLE_SPEED * dt;
                        myPaddleY  = std::max(0.f, std::min(myPaddleY, 500.f));
                        lastMoveClock.restart();
                    }

                    float serverErr = myTargetY - myPaddleY;
                    if (serverErr * serverErr > 60.f * 60.f) {
                        // Large divergence: snap to server.
                        myPaddleY = myTargetY;
                    } else if (wantDir == 0 &&
                               lastMoveClock.getElapsedTime().asSeconds() > 0.2f) {
                        // Not moving and settled: ease toward server.
                        const float ease = 1.f - std::exp(-10.f * dt);
                        myPaddleY += serverErr * ease;
                    }
                    // While moving: trust client prediction, don't correct.

                    myRenderY = myPaddleY;
                }

                // Opponent paddle: lerp toward server value.
                {
                    float& oppRenderY = (myPlayerId == 1) ? renderP2Y : renderP1Y;
                    float& oppTargetY = (myPlayerId == 1) ? targetP2Y : targetP1Y;
                    const float lerpT = 1.f - std::exp(-20.f * dt);
                    oppRenderY += (oppTargetY - oppRenderY) * lerpT;
                    // If ASSIGN not received yet, lerp both paddles.
                    if (myPlayerId == 0) {
                        const float l = 1.f - std::exp(-20.f * dt);
                        renderP1Y += (targetP1Y - renderP1Y) * l;
                        renderP2Y += (targetP2Y - renderP2Y) * l;
                    }
                }

                ball.ball.setPosition({renderBall.x, renderBall.y});
                p1.setPositionY(renderP1Y);
                p2.setPositionY(renderP2Y);
            }
        }

        // ── Draw ──────────────────────────────────────────────────────────────
        window.clear(sf::Color::Black);
        effects.drawBackground(window);

        if (state == GameState::ModeSelect)
        {
            float msTime = modeSelectClock.getElapsedTime().asSeconds();
            float titleAlpha = effects.enabled ? std::min(255.f, (msTime / 0.6f) * 255.f) : 255.f;
            float titleY = effects.enabled ? (55.f + 4.f * std::sin(msTime * (2.f * 3.14159265f / 4.0f))) : 55.f;

            auto title = makeText(84, withAlpha(sf::Color::White, static_cast<std::uint8_t>(titleAlpha)));
            title.setString("PONG");
            centerX(title, WindowX, titleY);
            window.draw(title);

            auto m1 = makeText(42, sf::Color(100, 210, 100));
            m1.setString("1   AI Mode");
            centerX(m1, WindowX, 240.f);
            window.draw(m1);

            auto m2 = makeText(42, sf::Color(100, 150, 210));
            m2.setString("2   Multiplayer");
            centerX(m2, WindowX, 330.f);
            window.draw(m2);
        }
        else if (state == GameState::DifficultySelect)
        {
            auto title = makeText(84);
            title.setString("PONG");
            centerX(title, WindowX, 55.f);
            window.draw(title);

            auto sub = makeText(26, sf::Color(160, 160, 160));
            sub.setString("Choose Difficulty");
            centerX(sub, WindowX, 168.f);
            window.draw(sub);

            struct Option { const char* label; sf::Color color; };
            const std::array<Option, 3> opts {{
                {"1   Easy",   sf::Color(100, 210, 100)},
                {"2   Medium", sf::Color(210, 210,  70)},
                {"3   Hard",   sf::Color(210,  70,  70)},
            }};
            float y = 250.f;
            for (const auto& o : opts) {
                auto opt = makeText(42, o.color);
                opt.setString(o.label);
                centerX(opt, WindowX, y);
                window.draw(opt);
                y += 90.f;
            }
        }
        else if (state == GameState::Waiting)
        {
            auto t = makeText(36, sf::Color(170, 170, 170));
            t.setString("Waiting for opponent...");
            centerX(t, WindowX, 260.f);
            window.draw(t);

            int remaining = 40 - static_cast<int>(waitingClock.getElapsedTime().asSeconds());
            if (remaining < 0) remaining = 0;
            auto timer = makeText(22, sf::Color(110, 110, 110));
            timer.setString("Timing out in " + std::to_string(remaining) + "s...");
            centerX(timer, WindowX, 320.f);
            window.draw(timer);
        }
        else if (state == GameState::NoOpponent)
        {
            auto headline = makeText(52, sf::Color(220, 120, 60));
            headline.setString("No Opponent Found");
            centerX(headline, WindowX, 180.f);
            window.draw(headline);

            auto sub = makeText(28, sf::Color(170, 170, 170));
            sub.setString("Could not find a match in time.");
            centerX(sub, WindowX, 275.f);
            window.draw(sub);

            auto instr = makeText(26, sf::Color(130, 130, 130));
            instr.setString("Press any key to go back");
            centerX(instr, WindowX, 360.f);
            window.draw(instr);
        }
        else if (state == GameState::OpponentLeft)
        {
            auto headline = makeText(52, sf::Color(220, 80, 80));
            headline.setString("OPPONENT LEFT");
            centerX(headline, WindowX, 180.f);
            window.draw(headline);

            auto sub = makeText(28, sf::Color(170, 170, 170));
            sub.setString("Connection lost.");
            centerX(sub, WindowX, 275.f);
            window.draw(sub);

            auto instr = makeText(26, sf::Color(130, 130, 130));
            instr.setString("Press any key to return to menu");
            centerX(instr, WindowX, 360.f);
            window.draw(instr);
        }
        else if (state == GameState::Ready)
        {
            auto t1 = makeText(62);
            t1.setString("Ready?");
            centerX(t1, WindowX, 185.f);
            window.draw(t1);

            float pulse = effects.enabled
                ? (182.5f + 72.5f * std::sin(totalRunTime * (2.f * 3.14159265f / 1.2f)))
                : 170.f;
            auto t2 = makeText(28, withAlpha(sf::Color(170, 170, 170), static_cast<std::uint8_t>(pulse)));
            t2.setString("Press SPACE to serve");
            centerX(t2, WindowX, 295.f);
            window.draw(t2);

            auto t3 = makeText(22, sf::Color(110, 110, 110));
            t3.setString("UP / DOWN arrows to move");
            centerX(t3, WindowX, 358.f);
            window.draw(t3);
        }
        else if (state == GameState::Playing)
        {
            effects.applyShake(window);
            effects.drawDivider(window);
            effects.drawTrail(window);
            effects.drawParticles(window);

            sf::Vector2f ballCenter = isMultiplayer
                ? sf::Vector2f(renderBall.x + 12.f, renderBall.y + 12.f)
                : (ball.ball.getPosition() + sf::Vector2f(12.f, 12.f));
            effects.drawBall(window, ballCenter, 12.f);

            float leftY = isMultiplayer ? renderP1Y : p1.getBounds().position.y;
            float rightY = isMultiplayer ? renderP2Y : p2.getBounds().position.y;
            effects.drawPaddle(window, 0, leftY);
            effects.drawPaddle(window, 1, rightY);

            // In multiplayer, "YOU" label above player's paddle
            if (isMultiplayer && myPlayerId != 0 && effects.enabled) {
                float paddleX = (myPlayerId == 1) ? 30.f : 730.f;
                float paddleY = (myPlayerId == 1) ? renderP1Y : renderP2Y;
                sf::Color youCol = (myPlayerId == 1) ? effects.leftColor : effects.rightColor;
                sf::Text txtYou(font);
                txtYou.setCharacterSize(16);
                txtYou.setString("YOU");
                txtYou.setFillColor(withAlpha(youCol, 200));
                auto b = txtYou.getLocalBounds();
                txtYou.setOrigin({b.position.x + b.size.x / 2.f, b.position.y + b.size.y / 2.f});
                txtYou.setPosition({paddleX + static_cast<float>(Pwidth) / 2.f, paddleY - 18.f});
                window.draw(txtYou);
            }

            // Score pop
            float s1Scale = effects.getScoreScale(0);
            auto b1 = txtP1Score.getLocalBounds();
            txtP1Score.setOrigin({b1.position.x + b1.size.x / 2.f, b1.position.y + b1.size.y / 2.f});
            txtP1Score.setPosition({280.f + b1.size.x / 2.f, 12.f + b1.size.y / 2.f});
            txtP1Score.setScale({s1Scale, s1Scale});
            window.draw(txtP1Score);

            float s2Scale = effects.getScoreScale(1);
            auto b2 = txtP2Score.getLocalBounds();
            txtP2Score.setOrigin({b2.position.x + b2.size.x / 2.f, b2.position.y + b2.size.y / 2.f});
            txtP2Score.setPosition({468.f + b2.size.x / 2.f, 12.f + b2.size.y / 2.f});
            txtP2Score.setScale({s2Scale, s2Scale});
            window.draw(txtP2Score);

            effects.drawCountdown(window, font);

            if (isMultiplayer && oppConnectedTimer > 0.f && effects.enabled) {
                float a = (oppConnectedTimer < 0.5f) ? (oppConnectedTimer / 0.5f) * 255.f : 255.f;
                sf::Text txtOpp(font);
                txtOpp.setCharacterSize(22);
                txtOpp.setString("OPPONENT CONNECTED");
                txtOpp.setFillColor(sf::Color(100, 220, 120, static_cast<std::uint8_t>(a)));
                centerX(txtOpp, WindowX, 75.f);
                window.draw(txtOpp);
            }

            effects.resetView(window);

            // Connection indicator (multiplayer)
            if (isMultiplayer) {
                int rttMs = static_cast<int>(std::round(oneWayLatency * 2000.f));
                sf::Color pingCol = (rttMs < 80)   ? sf::Color(80, 220, 100) :
                                    (rttMs <= 150) ? sf::Color(230, 220, 70) :
                                                     sf::Color(230, 70, 70);
                sf::Text txtPing(font);
                txtPing.setCharacterSize(16);
                txtPing.setString("PING " + std::to_string(rttMs) + " ms");
                txtPing.setFillColor(pingCol);
                auto pb = txtPing.getLocalBounds();
                txtPing.setPosition({static_cast<float>(WindowX) - pb.size.x - 20.f, 15.f});
                window.draw(txtPing);

                float elapsedSinceState = lastStateReceived.getElapsedTime().asSeconds();
                if (elapsedSinceState > 1.0f && elapsedSinceState <= 5.0f && effects.enabled) {
                    sf::RectangleShape reconOverlay({static_cast<float>(WindowX), static_cast<float>(WindowY)});
                    reconOverlay.setFillColor(sf::Color(0, 0, 0, 140));
                    window.draw(reconOverlay);

                    float pAlpha = 187.5f + 67.5f * std::sin(totalRunTime * (2.f * 3.14159265f / 1.0f));
                    sf::Text txtRecon(font);
                    txtRecon.setCharacterSize(32);
                    txtRecon.setString("Reconnecting...");
                    txtRecon.setFillColor(sf::Color(255, 255, 255, static_cast<std::uint8_t>(pAlpha)));
                    centerX(txtRecon, WindowX, WindowY / 2.f - 20.f);
                    window.draw(txtRecon);
                }
            }
        }
        else if (state == GameState::Gameover)
        {
            auto headline = makeText(90);
            headline.setFillColor(playerWon ? sf::Color(80, 220, 120)
                                            : sf::Color(220, 80, 80));
            headline.setString(playerWon ? "YOU WIN!" : (isMultiplayer ? "YOU LOSE!" : "AI WINS!"));
            centerX(headline, WindowX, 70.f);
            window.draw(headline);

            auto scoreLine = makeText(52);
            scoreLine.setString(std::to_string(scoreP1) +
                                "  -  " +
                                std::to_string(scoreP2));
            centerX(scoreLine, WindowX, 225.f);
            window.draw(scoreLine);

            auto lbl = makeText(22, sf::Color(130, 130, 130));
            lbl.setString("PLAYER");
            lbl.setPosition({(float)WindowX / 2.f - 120.f, 290.f});
            window.draw(lbl);
            lbl.setString(isMultiplayer ? "OPPONENT" : "AI");
            lbl.setPosition({(float)WindowX / 2.f + 68.f, 290.f});
            window.draw(lbl);

            float pulse = effects.enabled
                ? (182.5f + 72.5f * std::sin(totalRunTime * (2.f * 3.14159265f / 1.2f)))
                : 170.f;
            auto instr = makeText(28, withAlpha(sf::Color(170, 170, 170), static_cast<std::uint8_t>(pulse)));
            instr.setString("SPACE  —  play again");
            centerX(instr, WindowX, 380.f);
            window.draw(instr);

            auto instrEsc = makeText(28, sf::Color(170, 170, 170));
            instrEsc.setString("ESC  —  quit");
            centerX(instrEsc, WindowX, 430.f);
            window.draw(instrEsc);
        }

        // Transition fade overlay
        if (effects.enabled && transitionTimer > 0.f) {
            float a = std::clamp(transitionTimer / 0.2f, 0.f, 1.f) * 255.f;
            sf::RectangleShape fadeRect({static_cast<float>(WindowX), static_cast<float>(WindowY)});
            fadeRect.setFillColor(sf::Color(0, 0, 0, static_cast<std::uint8_t>(a)));
            window.draw(fadeRect);
        }

        // FPS counter
        if (showFPS) {
            window.draw(txtFPS);
        }

        // Speed & difficulty debug readout
        if (showSpeedInfo) {
            if (isMultiplayer) {
                float mpSpd = std::sqrt(renderBall.vx * renderBall.vx + renderBall.vy * renderBall.vy);
                txtSpeed.setString("Speed: " + std::to_string(static_cast<int>(std::round(mpSpd))));
            } else {
                std::string diffName = (currentDiffIndex >= 0 && currentDiffIndex < 3)
                                           ? DIFFS[currentDiffIndex].name
                                           : "None";
                txtSpeed.setString("Speed: " + std::to_string(static_cast<int>(std::round(ball.speed))) +
                                   " (" + diffName + ")");
            }
            auto b = txtSpeed.getLocalBounds();
            txtSpeed.setPosition({static_cast<float>(WindowX) - b.size.x - 15.f,
                                  static_cast<float>(WindowY) - 28.f});
            window.draw(txtSpeed);
        }

        window.display();
    }

    return 0;
}
