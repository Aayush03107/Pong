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


// Defined here; declared extern in globals.hpp.
const int WindowX = 800;
const int WindowY = 600;
const int Pwidth  = 15;
const int Pheight = 100;

// ── Game states ───────────────────────────────────────────────────────────────
enum class GameState { ModeSelect, DifficultySelect, Waiting, NoOpponent, Ready, Playing, Gameover };

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

// ─────────────────────────────────────────────────────────────────────────────
int main()
{
    auto serverIp = sf::IpAddress::resolve("16.179.132.221");
    
    if (!serverIp) {
        std::cerr << "Could not resolve server IP\n";
        return 1;
    }
    
    Network network(*serverIp, 5050);
    sf::RenderWindow window(
        sf::VideoMode({static_cast<unsigned>(WindowX),
                       static_cast<unsigned>(WindowY)}),
        "Pong", sf::Style::Close);
    window.setFramerateLimit(60);

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
    bool pressedup = false;
    bool pressedDown = false;
    int  lastSentDir = 0;  // -1 = up, 0 = stop, 1 = down
    float prevBallX = 388.f, prevBallY = 288.f;
    float prevBallDX = 0.f,  prevBallDY = 0.f;
    int   myPlayerId  = 0;         // 1 or 2, assigned by server ASSIGN message
    int   resendTimer = 0;         // counts frames since last input was sent
    float targetBallX = 388.f, targetBallY = 288.f; // server-authoritative targets
    float targetP1Y   = 250.f, targetP2Y   = 250.f;
    float renderBallX = 388.f, renderBallY = 288.f; // interpolated render positions
    float renderP1Y   = 250.f, renderP2Y   = 250.f;
    sf::Clock lastStateReceived;   // for disconnect detection
    sf::Clock waitingClock;        // tracks how long we've been in Waiting state
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
                // ESC quits from the Game Over screen.
                if (key->code == sf::Keyboard::Key::Escape &&
                    state == GameState::Gameover)
                    window.close();

                // Any key on the No-Opponent screen returns to mode select.
                if (state == GameState::NoOpponent) {
                    network.sendMessage(R"({"Typeo":"QUIT"})");
                    resetMatch();
                    state = GameState::ModeSelect;
                }

                // Mode selection — choose AI or Multiplayer.
                if (state == GameState::ModeSelect)
                {
                    if (key->code == sf::Keyboard::Key::Num1) {
                        isMultiplayer = false;
                        state = GameState::DifficultySelect;
                    }
                    else if (key->code == sf::Keyboard::Key::Num2) {
                        isMultiplayer = true;
                        myPlayerId    = 0;
                        lastSentDir   = 0;
                        resendTimer   = 0;
                        network.sendMessage(R"({"Typeo":"JOIN"})");
                        waitingClock.restart();
                        state = GameState::Waiting;
                    }
                }

                // Difficulty selection — AI mode only.
                else if (state == GameState::DifficultySelect)
                {
                    float aiSpd = 0.f, react = 0.f, err = 0.f, bSpd = 0.f;
                    if (key->code == sf::Keyboard::Key::Num1)
                        { aiSpd = 200.f; react = 0.35f; err = 80.f; bSpd = 400.f; }
                    else if (key->code == sf::Keyboard::Key::Num2)
                        { aiSpd = 300.f; react = 0.15f; err = 30.f; bSpd = 500.f; }
                    else if (key->code == sf::Keyboard::Key::Num3)
                        { aiSpd = 450.f; react = 0.05f; err =  5.f; bSpd = 800.f; }

                    if (bSpd > 0.f) {
                        p2.setSpeed(aiSpd);
                        opponent.changeReactionSpeed(react);
                        opponent.changeError(err);
                        ball.setSpeed(bSpd);    // rescales velocity direction
                        state = GameState::Ready;
                    }
                }
                else if (key->code == sf::Keyboard::Key::Space)
                {
                    if (state == GameState::Ready) {
                        state = GameState::Playing;
                        frameClock.restart();   // avoid a large dt on first frame
                    }
                    else if (state == GameState::Gameover) {
                        resetMatch();
                        state = GameState::ModeSelect;
                    }
                }
            }
        }

        // ── Update ────────────────────────────────────────────────────────────
        const float dt = frameClock.restart().asSeconds();

        // Waiting: drain packets; handle ASSIGN then transition on first STATE.
        if (state == GameState::Waiting)
        {
            // 40-second timeout — no opponent found.
            if (waitingClock.getElapsedTime().asSeconds() > 40.f) {
                network.sendMessage(R"({"Typeo":"QUIT"})");
                state = GameState::NoOpponent;
            }

            while (auto raw = network.receiveMessage()) {
                int pid;
                if (std::sscanf(raw->c_str(),
                    R"({"type":"ASSIGN","pid":%d})", &pid) == 1)
                { myPlayerId = pid; continue; }

                float bx, by, p1y, p2y; int s1, s2;
                if (std::sscanf(raw->c_str(),
                    R"({"type":"STATE","bx":%f,"by":%f,"p1y":%f,"p2y":%f,"s1":%d,"s2":%d})",
                    &bx, &by, &p1y, &p2y, &s1, &s2) == 6)
                {
                    // Snap render positions so there is no lerp artefact on entry.
                    targetBallX = renderBallX = bx;
                    targetBallY = renderBallY = by;
                    targetP1Y   = renderP1Y   = p1y;
                    targetP2Y   = renderP2Y   = p2y;
                    lastStateReceived.restart();
                    state = GameState::Playing;
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

                if (waitAfterScore)
                {
                    if (pauseClock.getElapsedTime().asMilliseconds() >= 1000)
                        waitAfterScore = false;
                }
                else
                {
                    if (ball.update(dt))
                        sndWall.play();

                    opponent.update(ball, dt);

                    if (ball.velocity.x < 0 &&
                        ball.getBounds().findIntersection(p1.getBounds()))
                    {
                        ball.handlePaddleCollision(p1.getBounds());
                        sndPaddle.play();
                    }
                    if (ball.velocity.x > 0 &&
                        ball.getBounds().findIntersection(p2.getBounds()))
                    {
                        ball.handlePaddleCollision(p2.getBounds());
                        sndPaddle.play();
                    }

                    const auto bb = ball.getBounds();
                    if (bb.position.x + bb.size.x < 0) {
                        ++scoreP2;
                        txtP2Score.setString(std::to_string(scoreP2));
                        ball.Reset();
                        sndScore.play();
                        waitAfterScore = true;
                        pauseClock.restart();
                    } else if (bb.position.x > WindowX) {
                        ++scoreP1;
                        txtP1Score.setString(std::to_string(scoreP1));
                        ball.Reset();
                        sndScore.play();
                        waitAfterScore = true;
                        pauseClock.restart();
                    }

                    if (scoreP1 >= WIN_SCORE || scoreP2 >= WIN_SCORE) {
                        playerWon = scoreP1 >= WIN_SCORE;
                        state     = GameState::Gameover;
                        if (playerWon) sndVictory.play();
                        else           sndDefeat.play();
                    }
                }
            }
            else
            {
                // ── Multiplayer: server-authoritative ─────────────────────────
                // Send direction on change OR every 10 frames as keep-alive
                // so a single dropped packet doesn't freeze the paddle.
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

                // Drain all queued packets; keep only the latest STATE values.
                bool  gotState  = false;
                float latestBx  = targetBallX, latestBy  = targetBallY;
                float latestP1y = targetP1Y,   latestP2y = targetP2Y;
                int   latestS1  = scoreP1,     latestS2  = scoreP2;

                while (auto raw = network.receiveMessage()) {
                    int pid;
                    if (std::sscanf(raw->c_str(),
                        R"({"type":"ASSIGN","pid":%d})", &pid) == 1)
                    { myPlayerId = pid; continue; }

                    int winner;
                    if (std::sscanf(raw->c_str(),
                        R"({"type":"GAME_OVER","winner":%d})", &winner) == 1)
                    {
                        playerWon = (myPlayerId == winner);
                        state     = GameState::Gameover;
                        if (playerWon) sndVictory.play();
                        else           sndDefeat.play();
                        break;
                    }

                    float bx, by, p1y, p2y; int s1, s2;
                    if (std::sscanf(raw->c_str(),
                        R"({"type":"STATE","bx":%f,"by":%f,"p1y":%f,"p2y":%f,"s1":%d,"s2":%d})",
                        &bx, &by, &p1y, &p2y, &s1, &s2) == 6)
                    {
                        gotState  = true;
                        latestBx  = bx;  latestBy  = by;
                        latestP1y = p1y; latestP2y = p2y;
                        latestS1  = s1;  latestS2  = s2;
                    }
                }

                if (gotState && state == GameState::Playing) {
                    lastStateReceived.restart();
                    targetBallX = latestBx;  targetBallY = latestBy;
                    targetP1Y   = latestP1y; targetP2Y   = latestP2y;

                    bool  scoreChanged = (latestS1 != scoreP1 || latestS2 != scoreP2);
                    float dX = latestBx - prevBallX;
                    float dY = latestBy - prevBallY;
                    if (!scoreChanged) {
                        if (prevBallDX * dX < 0.f) sndPaddle.play();
                        if (prevBallDY * dY < 0.f) sndWall.play();
                    }
                    prevBallX  = latestBx;  prevBallY  = latestBy;
                    prevBallDX = dX;        prevBallDY = dY;

                    if (scoreChanged) {
                        scoreP1 = latestS1; scoreP2 = latestS2;
                        txtP1Score.setString(std::to_string(scoreP1));
                        txtP2Score.setString(std::to_string(scoreP2));
                        sndScore.play();
                    }
                }

                // Disconnect detection: no STATE for 5 s means the server or
                // opponent is gone — go back to menu.
                if (state == GameState::Playing &&
                    lastStateReceived.getElapsedTime().asSeconds() > 5.f)
                {
                    resetMatch();
                    state = GameState::ModeSelect;
                }

                // Lerp render positions toward server targets every frame.
                const float LERP = 0.3f;
                renderBallX += (targetBallX - renderBallX) * LERP;
                renderBallY += (targetBallY - renderBallY) * LERP;
                renderP1Y   += (targetP1Y   - renderP1Y)   * LERP;
                renderP2Y   += (targetP2Y   - renderP2Y)   * LERP;

                ball.ball.setPosition({renderBallX, renderBallY});
                p1.setPositionY(renderP1Y);
                p2.setPositionY(renderP2Y);
            }
        }

        // ── Draw ──────────────────────────────────────────────────────────────
        window.clear(sf::Color::Black);

        if (state == GameState::ModeSelect)
        {
            auto title = makeText(84);
            title.setString("PONG");
            centerX(title, WindowX, 55.f);
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
            // Title
            auto title = makeText(84);
            title.setString("PONG");
            centerX(title, WindowX, 55.f);
            window.draw(title);

            // Subtitle
            auto sub = makeText(26, sf::Color(160, 160, 160));
            sub.setString("Choose Difficulty");
            centerX(sub, WindowX, 168.f);
            window.draw(sub);

            // Options
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

            // Show countdown while waiting.
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
        else if (state == GameState::Ready)
        {
            auto t1 = makeText(62);
            t1.setString("Ready?");
            centerX(t1, WindowX, 185.f);
            window.draw(t1);

            auto t2 = makeText(28, sf::Color(170, 170, 170));
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
            drawDivider();
            ball.draw(window);
            p1.draw(window);
            p2.draw(window);
            window.draw(txtP1Score);
            window.draw(txtP2Score);
        }
        else if (state == GameState::Gameover)
        {
            // Winner headline
            auto headline = makeText(90);
            headline.setFillColor(playerWon ? sf::Color(80, 220, 120)
                                            : sf::Color(220, 80, 80));
            headline.setString(playerWon ? "YOU WIN!" : (isMultiplayer ? "YOU LOSE!" : "AI WINS!"));
            centerX(headline, WindowX, 70.f);
            window.draw(headline);

            // Final score
            auto scoreLine = makeText(52);
            scoreLine.setString(std::to_string(scoreP1) +
                                "  -  " +
                                std::to_string(scoreP2));
            centerX(scoreLine, WindowX, 225.f);
            window.draw(scoreLine);

            // Player / AI labels under the score
            auto lbl = makeText(22, sf::Color(130, 130, 130));
            lbl.setString("PLAYER");
            lbl.setPosition({(float)WindowX / 2.f - 120.f, 290.f});
            window.draw(lbl);
            lbl.setString(isMultiplayer ? "OPPONENT" : "AI");
            lbl.setPosition({(float)WindowX / 2.f + 68.f, 290.f});
            window.draw(lbl);

            // Instructions
            auto instr = makeText(28, sf::Color(170, 170, 170));
            instr.setString("SPACE  —  play again");
            centerX(instr, WindowX, 380.f);
            window.draw(instr);

            instr.setString("ESC  —  quit");
            centerX(instr, WindowX, 430.f);
            window.draw(instr);
        }

        window.display();
    }

    return 0;
}
