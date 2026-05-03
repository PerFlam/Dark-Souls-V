#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int kWindowWidth = 960;
constexpr int kWindowHeight = 640;
constexpr int kTimerId = 1;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

Vec2 operator+(const Vec2& a, const Vec2& b) {
    return {a.x + b.x, a.y + b.y};
}

Vec2 operator-(const Vec2& a, const Vec2& b) {
    return {a.x - b.x, a.y - b.y};
}

Vec2 operator*(const Vec2& a, float scale) {
    return {a.x * scale, a.y * scale};
}

Vec2& operator+=(Vec2& a, const Vec2& b) {
    a.x += b.x;
    a.y += b.y;
    return a;
}

float LengthSquared(const Vec2& v) {
    return v.x * v.x + v.y * v.y;
}

float Length(const Vec2& v) {
    return std::sqrt(LengthSquared(v));
}

Vec2 Normalize(const Vec2& v) {
    float len = Length(v);
    if (len < 0.0001f) {
        return {0.0f, 0.0f};
    }
    return {v.x / len, v.y / len};
}

float ClampFloat(float value, float low, float high) {
    return std::max(low, std::min(value, high));
}

bool CircleHit(const Vec2& a, float ar, const Vec2& b, float br) {
    float rr = ar + br;
    return LengthSquared(a - b) <= rr * rr;
}

struct Player {
    Vec2 pos;
    float radius = 18.0f;
    float speed = 300.0f;
    int hp = 3;
    float energy = 100.0f;
    float pulseCooldown = 0.0f;
    float invulnerable = 0.0f;
};

struct Meteor {
    Vec2 pos;
    Vec2 vel;
    float radius = 16.0f;
    float wobble = 0.0f;
};

struct EnergyShard {
    Vec2 pos;
    Vec2 vel;
    float radius = 7.0f;
    float life = 7.0f;
};

struct PulseRing {
    Vec2 pos;
    float life = 0.0f;
    float maxLife = 0.35f;
    float maxRadius = 170.0f;
};

struct Star {
    Vec2 pos;
    float size = 1.0f;
    float phase = 0.0f;
};

enum class Scene {
    Menu,
    Playing,
    Paused,
    GameOver,
};

enum class DifficultyMode {
    Normal,
    Hard,
};

struct ModeConfig {
    const char* label;
    int hp;
    float initialSpawnTimer;
    float baseSpawnInterval;
    float minSpawnInterval;
    float difficultyRampSeconds;
    float meteorRadiusBonus;
    float meteorSpeedBonus;
    float energyRegenPerSecond;
    float pulseCost;
    float pulseCooldown;
    float pulseRadius;
    float shardDropChance;
    float focusMultiplier;
    float scoreMultiplier;
};

const ModeConfig& GetModeConfig(DifficultyMode mode) {
    static const ModeConfig kNormal{
        "NORMAL",
        3,
        0.45f,
        0.85f,
        0.22f,
        18.0f,
        0.0f,
        0.0f,
        14.0f,
        35.0f,
        0.75f,
        165.0f,
        0.38f,
        0.62f,
        1.0f,
    };
    static const ModeConfig kHard{
        "HARD",
        2,
        0.30f,
        0.74f,
        0.14f,
        13.0f,
        5.0f,
        32.0f,
        10.0f,
        42.0f,
        0.95f,
        150.0f,
        0.26f,
        0.56f,
        1.55f,
    };
    return mode == DifficultyMode::Hard ? kHard : kNormal;
}

class Game {
public:
    Game() : rng_(std::random_device{}()) {
        GenerateStars();
        Reset();
        lastTick_ = std::chrono::steady_clock::now();
    }

    void Reset() {
        const ModeConfig& config = GetModeConfig(activeMode_);
        player_ = {};
        player_.pos = {kWindowWidth * 0.5f, kWindowHeight * 0.72f};
        player_.hp = config.hp;
        meteors_.clear();
        shards_.clear();
        pulses_.clear();
        elapsed_ = 0.0f;
        ambientTime_ = 0.0f;
        score_ = 0.0f;
        killScore_ = 0;
        shardScore_ = 0;
        spawnTimer_ = config.initialSpawnTimer;
        difficulty_ = 1.0f;
        keys_.assign(256, false);
    }

    void StartRun() {
        Reset();
        scene_ = Scene::Playing;
        lastTick_ = std::chrono::steady_clock::now();
    }

    void TogglePause() {
        if (scene_ == Scene::Playing) {
            scene_ = Scene::Paused;
        } else if (scene_ == Scene::Paused) {
            scene_ = Scene::Playing;
            lastTick_ = std::chrono::steady_clock::now();
        }
    }

    void OnKeyDown(WPARAM key, bool firstPress) {
        if (key < keys_.size()) {
            keys_[key] = true;
        }

        if (!firstPress) {
            return;
        }

        if (scene_ == Scene::Menu) {
            if (key == VK_LEFT || key == 'A') {
                selectedMode_ = DifficultyMode::Normal;
            } else if (key == VK_RIGHT || key == 'D') {
                selectedMode_ = DifficultyMode::Hard;
            } else if (key == '1') {
                selectedMode_ = DifficultyMode::Normal;
            } else if (key == '2') {
                selectedMode_ = DifficultyMode::Hard;
            }
            if (key == VK_RETURN) {
                activeMode_ = selectedMode_;
                StartRun();
            }
            return;
        }

        if (scene_ == Scene::GameOver) {
            if (key == 'R' || key == VK_RETURN) {
                StartRun();
            } else if (key == 'M') {
                scene_ = Scene::Menu;
            }
            return;
        }

        if (key == VK_ESCAPE || key == 'P') {
            TogglePause();
            return;
        }

        if (scene_ != Scene::Playing) {
            return;
        }

        if (key == VK_SPACE) {
            ActivatePulse();
        }
    }

    void OnKeyUp(WPARAM key) {
        if (key < keys_.size()) {
            keys_[key] = false;
        }
    }

    void Tick() {
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - lastTick_).count();
        lastTick_ = now;
        dt = ClampFloat(dt, 0.0f, 0.05f);

        ambientTime_ += dt;
        if (scene_ != Scene::Playing) {
            UpdatePulseEffects(dt);
            return;
        }

        elapsed_ += dt;
        const ModeConfig& config = GetModeConfig(activeMode_);
        difficulty_ = 1.0f + elapsed_ / config.difficultyRampSeconds;

        UpdatePlayer(dt);
        UpdateSpawning(dt);
        UpdateMeteors(dt);
        UpdateShards(dt);
        UpdatePulseEffects(dt);
        ResolveCollisions();

        score_ = (elapsed_ * 12.0f + static_cast<float>(killScore_ + shardScore_)) * config.scoreMultiplier;
    }

    void Render(HDC hdc, const RECT& clientRect) {
        DrawBackground(hdc, clientRect);
        DrawGrid(hdc, clientRect);

        if (scene_ == Scene::Menu) {
            DrawMenu(hdc, clientRect);
            return;
        }

        DrawShards(hdc);
        DrawMeteors(hdc);
        DrawPulseEffects(hdc);
        DrawPlayer(hdc);
        DrawHud(hdc);

        if (scene_ == Scene::Paused) {
            DrawPauseOverlay(hdc, clientRect);
        } else if (scene_ == Scene::GameOver) {
            DrawGameOver(hdc, clientRect);
        }
    }

private:
    void GenerateStars() {
        std::uniform_real_distribution<float> xDist(0.0f, static_cast<float>(kWindowWidth));
        std::uniform_real_distribution<float> yDist(0.0f, static_cast<float>(kWindowHeight));
        std::uniform_real_distribution<float> sizeDist(1.0f, 3.0f);
        std::uniform_real_distribution<float> phaseDist(0.0f, 6.28f);

        stars_.clear();
        for (int i = 0; i < 80; ++i) {
            stars_.push_back({{xDist(rng_), yDist(rng_)}, sizeDist(rng_), phaseDist(rng_)});
        }
    }

    float RandFloat(float low, float high) {
        std::uniform_real_distribution<float> dist(low, high);
        return dist(rng_);
    }

    int RandInt(int low, int high) {
        std::uniform_int_distribution<int> dist(low, high);
        return dist(rng_);
    }

    void UpdatePlayer(float dt) {
        const ModeConfig& config = GetModeConfig(activeMode_);
        Vec2 input;
        if (IsPressed('A') || IsPressed(VK_LEFT)) {
            input.x -= 1.0f;
        }
        if (IsPressed('D') || IsPressed(VK_RIGHT)) {
            input.x += 1.0f;
        }
        if (IsPressed('W') || IsPressed(VK_UP)) {
            input.y -= 1.0f;
        }
        if (IsPressed('S') || IsPressed(VK_DOWN)) {
            input.y += 1.0f;
        }

        input = Normalize(input);
        float focus = IsPressed(VK_SHIFT) ? config.focusMultiplier : 1.0f;
        player_.pos += input * (player_.speed * focus * dt);
        player_.pos.x = ClampFloat(player_.pos.x, 30.0f, kWindowWidth - 30.0f);
        player_.pos.y = ClampFloat(player_.pos.y, 30.0f, kWindowHeight - 30.0f);

        player_.energy = ClampFloat(player_.energy + config.energyRegenPerSecond * dt, 0.0f, 100.0f);
        player_.pulseCooldown = std::max(0.0f, player_.pulseCooldown - dt);
        player_.invulnerable = std::max(0.0f, player_.invulnerable - dt);
    }

    void UpdateSpawning(float dt) {
        const ModeConfig& config = GetModeConfig(activeMode_);
        spawnTimer_ -= dt;
        float interval = ClampFloat(config.baseSpawnInterval - difficulty_ * 0.05f,
                                    config.minSpawnInterval,
                                    config.baseSpawnInterval);
        if (spawnTimer_ <= 0.0f) {
            SpawnMeteor();
            spawnTimer_ += interval;
        }
    }

    void SpawnMeteor() {
        const ModeConfig& config = GetModeConfig(activeMode_);
        Meteor meteor;
        int edge = RandInt(0, 3);
        if (edge == 0) {
            meteor.pos = {-25.0f, RandFloat(0.0f, static_cast<float>(kWindowHeight))};
        } else if (edge == 1) {
            meteor.pos = {kWindowWidth + 25.0f, RandFloat(0.0f, static_cast<float>(kWindowHeight))};
        } else if (edge == 2) {
            meteor.pos = {RandFloat(0.0f, static_cast<float>(kWindowWidth)), -25.0f};
        } else {
            meteor.pos = {RandFloat(0.0f, static_cast<float>(kWindowWidth)), kWindowHeight + 25.0f};
        }

        meteor.radius = RandFloat(12.0f + config.meteorRadiusBonus * 0.3f,
                                  ClampFloat(22.0f + difficulty_ * 2.5f + config.meteorRadiusBonus, 22.0f, 42.0f));
        Vec2 towardPlayer = Normalize(player_.pos - meteor.pos);
        Vec2 offset = {RandFloat(-0.35f, 0.35f), RandFloat(-0.35f, 0.35f)};
        Vec2 heading = Normalize(towardPlayer + offset);
        float speed = RandFloat(100.0f + difficulty_ * 16.0f + config.meteorSpeedBonus,
                                165.0f + difficulty_ * 25.0f + config.meteorSpeedBonus);
        meteor.vel = heading * speed;
        meteor.wobble = RandFloat(0.0f, 6.28f);
        meteors_.push_back(meteor);
    }

    void UpdateMeteors(float dt) {
        const ModeConfig& config = GetModeConfig(activeMode_);
        for (auto& meteor : meteors_) {
            Vec2 steer = Normalize(player_.pos - meteor.pos) * (18.0f * dt);
            meteor.vel += steer;
            float speed = ClampFloat(Length(meteor.vel), 80.0f, 260.0f + difficulty_ * 18.0f + config.meteorSpeedBonus);
            meteor.vel = Normalize(meteor.vel) * speed;

            meteor.wobble += dt * 2.0f;
            Vec2 sway = {-meteor.vel.y, meteor.vel.x};
            sway = Normalize(sway) * std::sin(meteor.wobble) * 16.0f * dt;

            meteor.pos += meteor.vel * dt;
            meteor.pos += sway;
        }

        meteors_.erase(
            std::remove_if(
                meteors_.begin(),
                meteors_.end(),
                [](const Meteor& meteor) {
                    return meteor.pos.x < -120.0f || meteor.pos.x > kWindowWidth + 120.0f ||
                           meteor.pos.y < -120.0f || meteor.pos.y > kWindowHeight + 120.0f;
                }),
            meteors_.end());
    }

    void UpdateShards(float dt) {
        for (auto& shard : shards_) {
            shard.life -= dt;
            shard.pos += shard.vel * dt;
            shard.vel = shard.vel * (1.0f - dt * 0.25f);
        }

        shards_.erase(
            std::remove_if(
                shards_.begin(),
                shards_.end(),
                [](const EnergyShard& shard) { return shard.life <= 0.0f; }),
            shards_.end());
    }

    void UpdatePulseEffects(float dt) {
        for (auto& pulse : pulses_) {
            pulse.life -= dt;
        }

        pulses_.erase(
            std::remove_if(
                pulses_.begin(),
                pulses_.end(),
                [](const PulseRing& pulse) { return pulse.life <= 0.0f; }),
            pulses_.end());
    }

    void ResolveCollisions() {
        for (size_t i = 0; i < shards_.size();) {
            if (CircleHit(player_.pos, player_.radius + 3.0f, shards_[i].pos, shards_[i].radius)) {
                player_.energy = ClampFloat(player_.energy + 16.0f, 0.0f, 100.0f);
                shardScore_ += 8;
                shards_.erase(shards_.begin() + static_cast<long long>(i));
                continue;
            }
            ++i;
        }

        if (player_.invulnerable > 0.0f) {
            return;
        }

        for (size_t i = 0; i < meteors_.size(); ++i) {
            if (!CircleHit(player_.pos, player_.radius, meteors_[i].pos, meteors_[i].radius)) {
                continue;
            }

            player_.hp -= 1;
            player_.invulnerable = 1.15f;
            pulses_.push_back({player_.pos, 0.18f, 0.18f, 90.0f});
            meteors_.erase(meteors_.begin() + static_cast<long long>(i));

            if (player_.hp <= 0) {
                GetBestScoreRef(activeMode_) = std::max(GetBestScoreRef(activeMode_), static_cast<int>(score_));
                scene_ = Scene::GameOver;
            }
            break;
        }
    }

    void ActivatePulse() {
        const ModeConfig& config = GetModeConfig(activeMode_);
        if (player_.energy < config.pulseCost || player_.pulseCooldown > 0.0f) {
            return;
        }

        player_.energy -= config.pulseCost;
        player_.pulseCooldown = config.pulseCooldown;
        pulses_.push_back({player_.pos, 0.35f, 0.35f, config.pulseRadius + 10.0f});

        int destroyed = 0;
        for (size_t i = 0; i < meteors_.size();) {
            float reach = config.pulseRadius + meteors_[i].radius;
            if (LengthSquared(meteors_[i].pos - player_.pos) <= reach * reach) {
                MaybeSpawnShard(meteors_[i].pos);
                meteors_.erase(meteors_.begin() + static_cast<long long>(i));
                destroyed += 1;
                continue;
            }
            ++i;
        }
        killScore_ += destroyed * 15;
    }

    void MaybeSpawnShard(const Vec2& pos) {
        const ModeConfig& config = GetModeConfig(activeMode_);
        if (RandFloat(0.0f, 1.0f) > config.shardDropChance) {
            return;
        }

        EnergyShard shard;
        shard.pos = pos;
        shard.vel = {RandFloat(-40.0f, 40.0f), RandFloat(-25.0f, 25.0f)};
        shards_.push_back(shard);
    }

    bool IsPressed(WPARAM key) const {
        return key < keys_.size() && keys_[key];
    }

    int& GetBestScoreRef(DifficultyMode mode) {
        return mode == DifficultyMode::Hard ? hardBestScore_ : normalBestScore_;
    }

    void DrawBackground(HDC hdc, const RECT& clientRect) {
        HBRUSH backBrush = CreateSolidBrush(RGB(8, 11, 26));
        FillRect(hdc, &clientRect, backBrush);
        DeleteObject(backBrush);

        for (const auto& star : stars_) {
            float glow = 0.55f + 0.45f * std::sin(ambientTime_ * 1.8f + star.phase);
            int alpha = static_cast<int>(120 + glow * 100);
            COLORREF color = RGB(alpha, alpha, std::min(255, alpha + 30));
            HBRUSH starBrush = CreateSolidBrush(color);
            RECT r{
                static_cast<LONG>(star.pos.x),
                static_cast<LONG>(star.pos.y),
                static_cast<LONG>(star.pos.x + star.size),
                static_cast<LONG>(star.pos.y + star.size),
            };
            FillRect(hdc, &r, starBrush);
            DeleteObject(starBrush);
        }
    }

    void DrawGrid(HDC hdc, const RECT& clientRect) {
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(20, 36, 64));
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));

        int verticalOffset = static_cast<int>(std::fmod(ambientTime_ * 30.0f, 40.0f));
        for (int x = 0; x <= clientRect.right; x += 60) {
            MoveToEx(hdc, x, 0, nullptr);
            LineTo(hdc, x, clientRect.bottom);
        }
        for (int y = -40 + verticalOffset; y <= clientRect.bottom; y += 40) {
            MoveToEx(hdc, 0, y, nullptr);
            LineTo(hdc, clientRect.right, y);
        }

        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }

    void DrawPlayer(HDC hdc) {
        bool blinking = player_.invulnerable > 0.0f &&
                        static_cast<int>(player_.invulnerable * 12.0f) % 2 == 0;
        COLORREF color = blinking ? RGB(255, 240, 170) : RGB(115, 235, 255);
        DrawFilledCircle(hdc, player_.pos, player_.radius, color);
        DrawOutlineCircle(hdc, player_.pos, player_.radius + 6.0f, RGB(70, 165, 210), 2);

        Vec2 nose{player_.pos.x, player_.pos.y - player_.radius - 10.0f};
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(170, 240, 255));
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
        MoveToEx(hdc, static_cast<int>(player_.pos.x), static_cast<int>(player_.pos.y), nullptr);
        LineTo(hdc, static_cast<int>(nose.x), static_cast<int>(nose.y));
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }

    void DrawMeteors(HDC hdc) {
        for (const auto& meteor : meteors_) {
            COLORREF outer = RGB(255, 130, 90);
            COLORREF inner = RGB(255, 204, 120);
            DrawFilledCircle(hdc, meteor.pos, meteor.radius, outer);
            DrawFilledCircle(hdc, meteor.pos, meteor.radius * 0.48f, inner);
            DrawOutlineCircle(hdc, meteor.pos, meteor.radius + 2.0f, RGB(255, 235, 180), 1);
        }
    }

    void DrawShards(HDC hdc) {
        for (const auto& shard : shards_) {
            DrawFilledCircle(hdc, shard.pos, shard.radius, RGB(90, 255, 170));
            DrawOutlineCircle(hdc, shard.pos, shard.radius + 2.0f, RGB(180, 255, 220), 1);
        }
    }

    void DrawPulseEffects(HDC hdc) {
        for (const auto& pulse : pulses_) {
            float t = 1.0f - pulse.life / pulse.maxLife;
            float radius = 28.0f + pulse.maxRadius * t;
            int intensity = static_cast<int>(255 - 120 * t);
            DrawOutlineCircle(hdc, pulse.pos, radius, RGB(120, intensity, 255), 3);
        }
    }

    void DrawHud(HDC hdc) {
        const ModeConfig& config = GetModeConfig(activeMode_);
        SetBkMode(hdc, TRANSPARENT);

        HFONT titleFont = CreateFontA(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, VARIABLE_PITCH, "Segoe UI");
        HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, titleFont));
        SetTextColor(hdc, RGB(235, 245, 255));

        std::ostringstream scoreText;
        scoreText << "Score " << static_cast<int>(score_);
        TextOutA(hdc, 28, 20, scoreText.str().c_str(), static_cast<int>(scoreText.str().size()));

        std::ostringstream timeText;
        timeText << "Time " << std::fixed << std::setprecision(1) << elapsed_;
        TextOutA(hdc, 28, 50, timeText.str().c_str(), static_cast<int>(timeText.str().size()));

        std::ostringstream diffText;
        diffText << "Threat " << std::fixed << std::setprecision(1) << difficulty_;
        TextOutA(hdc, 28, 80, diffText.str().c_str(), static_cast<int>(diffText.str().size()));

        std::ostringstream modeText;
        modeText << "Mode " << config.label;
        TextOutA(hdc, 28, 110, modeText.str().c_str(), static_cast<int>(modeText.str().size()));

        SelectObject(hdc, oldFont);
        DeleteObject(titleFont);

        DrawMeter(hdc, 730, 22, 180, 18, player_.energy / 100.0f, RGB(80, 255, 180), "Energy");

        float cooldownRatio = player_.pulseCooldown <= 0.0f ? 1.0f : 1.0f - player_.pulseCooldown / config.pulseCooldown;
        DrawMeter(hdc, 730, 54, 180, 14, cooldownRatio, RGB(120, 180, 255), "Pulse");

        HFONT infoFont = CreateFontA(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, VARIABLE_PITCH, "Segoe UI");
        oldFont = static_cast<HFONT>(SelectObject(hdc, infoFont));
        SetTextColor(hdc, RGB(255, 245, 230));

        std::ostringstream hpText;
        hpText << "HP " << player_.hp << "/3";
        const std::string hpLabel = hpText.str();
        TextOutA(hdc, 730, 78, hpLabel.c_str(), static_cast<int>(hpLabel.size()));

        const char* controls = "Move WASD / Arrows | Space Pulse | Shift Focus | Esc Pause";
        TextOutA(hdc, 240, 602, controls, lstrlenA(controls));

        SelectObject(hdc, oldFont);
        DeleteObject(infoFont);
    }

    void DrawMenu(HDC hdc, const RECT& clientRect) {
        const bool normalSelected = selectedMode_ == DifficultyMode::Normal;
        const bool hardSelected = selectedMode_ == DifficultyMode::Hard;
        DrawCenterBlock(hdc, clientRect, 180);
        DrawCenteredText(hdc, clientRect, 168, 44, FW_BOLD, RGB(245, 248, 255), "PULSE HARBOR");
        DrawCenteredText(hdc, clientRect, 228, 24, FW_NORMAL, RGB(180, 220, 255),
                         "Survive the meteor field and release pulse waves.");
        DrawCenteredText(hdc, clientRect, 266, 22, FW_NORMAL, RGB(170, 205, 235),
                         "Collect green shards to recharge. Focus with Shift when the field gets crowded.");
        DrawCenteredText(hdc, clientRect, 306, 20, FW_NORMAL, RGB(165, 196, 226),
                         "Select mode with Left / Right or 1 / 2");
        DrawCenteredText(hdc, clientRect, 338, 28, FW_BOLD, normalSelected ? RGB(150, 255, 200) : RGB(130, 150, 175),
                         "1. NORMAL");
        DrawCenteredText(hdc, clientRect, 372, 28, FW_BOLD, hardSelected ? RGB(255, 150, 120) : RGB(130, 150, 175),
                         "2. HARD");
        DrawCenteredText(hdc, clientRect, 412, 18, FW_NORMAL,
                         normalSelected ? RGB(170, 225, 255) : RGB(255, 195, 180),
                         normalSelected ? "Balanced survival mode with 3 HP and standard pulse recovery."
                                        : "2 HP, faster meteors, weaker recovery, higher score multiplier.");
        DrawCenteredText(hdc, clientRect, 452, 28, FW_BOLD, RGB(255, 234, 170),
                         "Press Enter to Start");
        DrawCenteredText(hdc, clientRect, 490, 20, FW_NORMAL, RGB(170, 195, 220),
                         "Controls: WASD / Arrows move, Space pulse, Esc pause");
        std::ostringstream normalBest;
        normalBest << "Normal best " << normalBestScore_;
        DrawCenteredText(hdc, clientRect, 530, 18, FW_NORMAL, RGB(150, 255, 200), normalBest.str().c_str());

        std::ostringstream hardBest;
        hardBest << "Hard best " << hardBestScore_;
        DrawCenteredText(hdc, clientRect, 556, 18, FW_NORMAL, RGB(255, 182, 160), hardBest.str().c_str());
    }

    void DrawPauseOverlay(HDC hdc, const RECT& clientRect) {
        DrawCenterBlock(hdc, clientRect, 160);
        DrawCenteredText(hdc, clientRect, 238, 40, FW_BOLD, RGB(245, 248, 255), "PAUSED");
        DrawCenteredText(hdc, clientRect, 294, 24, FW_NORMAL, RGB(180, 220, 255),
                         "Press Esc or P to continue.");
    }

    void DrawGameOver(HDC hdc, const RECT& clientRect) {
        const ModeConfig& config = GetModeConfig(activeMode_);
        DrawCenterBlock(hdc, clientRect, 180);
        DrawCenteredText(hdc, clientRect, 188, 42, FW_BOLD, RGB(255, 222, 198), "MISSION OVER");

        std::ostringstream result;
        result << "Final score " << static_cast<int>(score_);
        DrawCenteredText(hdc, clientRect, 246, 26, FW_BOLD, RGB(255, 245, 220), result.str().c_str());

        std::ostringstream survived;
        survived << "Survived " << std::fixed << std::setprecision(1) << elapsed_ << " seconds";
        DrawCenteredText(hdc, clientRect, 286, 22, FW_NORMAL, RGB(190, 220, 235), survived.str().c_str());

        std::ostringstream best;
        best << config.label << " best " << GetBestScoreRef(activeMode_);
        DrawCenteredText(hdc, clientRect, 320, 22, FW_NORMAL, RGB(160, 255, 200), best.str().c_str());

        DrawCenteredText(hdc, clientRect, 370, 28, FW_BOLD, RGB(255, 234, 170),
                         "Press R or Enter to Restart");
        DrawCenteredText(hdc, clientRect, 408, 20, FW_NORMAL, RGB(180, 210, 230),
                         "Press M to return to menu");
    }

    void DrawCenterBlock(HDC hdc, const RECT& clientRect, int top) {
        RECT panel{
            clientRect.left + 140,
            top,
            clientRect.right - 140,
            clientRect.bottom - 140,
        };
        HBRUSH panelBrush = CreateSolidBrush(RGB(12, 20, 38));
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(72, 120, 170));
        HGDIOBJ oldBrush = SelectObject(hdc, panelBrush);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        RoundRect(hdc, panel.left, panel.top, panel.right, panel.bottom, 24, 24);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(panelBrush);
        DeleteObject(pen);
    }

    void DrawCenteredText(HDC hdc, const RECT& clientRect, int y, int height, int weight,
                          COLORREF color, const char* text) {
        RECT area{clientRect.left, y, clientRect.right, y + height};
        HFONT font = CreateFontA(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, VARIABLE_PITCH, "Segoe UI");
        HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, font));
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, color);
        DrawTextA(hdc, text, -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont);
        DeleteObject(font);
    }

    void DrawMeter(HDC hdc, int x, int y, int width, int height, float ratio, COLORREF fill, const char* label) {
        ratio = ClampFloat(ratio, 0.0f, 1.0f);
        RECT outer{x, y, x + width, y + height};
        HBRUSH borderBrush = CreateSolidBrush(RGB(26, 34, 52));
        FillRect(hdc, &outer, borderBrush);
        DeleteObject(borderBrush);

        RECT inner{x + 2, y + 2, x + 2 + static_cast<int>((width - 4) * ratio), y + height - 2};
        HBRUSH fillBrush = CreateSolidBrush(fill);
        FillRect(hdc, &inner, fillBrush);
        DeleteObject(fillBrush);

        HFONT font = CreateFontA(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, VARIABLE_PITCH, "Segoe UI");
        HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, font));
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(240, 245, 255));
        TextOutA(hdc, x - 56, y - 1, label, lstrlenA(label));
        SelectObject(hdc, oldFont);
        DeleteObject(font);
    }

    void DrawFilledCircle(HDC hdc, const Vec2& center, float radius, COLORREF fill) {
        HBRUSH brush = CreateSolidBrush(fill);
        HGDIOBJ oldBrush = SelectObject(hdc, brush);
        HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
        Ellipse(hdc,
                static_cast<int>(center.x - radius),
                static_cast<int>(center.y - radius),
                static_cast<int>(center.x + radius),
                static_cast<int>(center.y + radius));
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(brush);
    }

    void DrawOutlineCircle(HDC hdc, const Vec2& center, float radius, COLORREF color, int penWidth) {
        HPEN pen = CreatePen(PS_SOLID, penWidth, color);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
        Ellipse(hdc,
                static_cast<int>(center.x - radius),
                static_cast<int>(center.y - radius),
                static_cast<int>(center.x + radius),
                static_cast<int>(center.y + radius));
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(pen);
    }

private:
    Scene scene_ = Scene::Menu;
    Player player_;
    std::vector<Meteor> meteors_;
    std::vector<EnergyShard> shards_;
    std::vector<PulseRing> pulses_;
    std::vector<Star> stars_;
    std::vector<bool> keys_ = std::vector<bool>(256, false);
    std::mt19937 rng_;
    std::chrono::steady_clock::time_point lastTick_;
    float elapsed_ = 0.0f;
    float ambientTime_ = 0.0f;
    float score_ = 0.0f;
    int killScore_ = 0;
    int shardScore_ = 0;
    int normalBestScore_ = 0;
    int hardBestScore_ = 0;
    float spawnTimer_ = 0.0f;
    float difficulty_ = 1.0f;
    DifficultyMode selectedMode_ = DifficultyMode::Normal;
    DifficultyMode activeMode_ = DifficultyMode::Normal;
};

Game* g_game = nullptr;

void PaintScene(HWND hwnd, HDC hdc) {
    RECT clientRect{};
    GetClientRect(hwnd, &clientRect);

    HDC memoryDc = CreateCompatibleDC(hdc);
    HBITMAP bitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
    HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);

    if (g_game != nullptr) {
        g_game->Render(memoryDc, clientRect);
    }

    BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, memoryDc, 0, 0, SRCCOPY);

    SelectObject(memoryDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        SetTimer(hwnd, kTimerId, 16, nullptr);
        return 0;

    case WM_TIMER:
        if (wParam == kTimerId && g_game != nullptr) {
            g_game->Tick();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_KEYDOWN:
        if (g_game != nullptr) {
            bool firstPress = (lParam & 0x40000000) == 0;
            g_game->OnKeyDown(wParam, firstPress);
        }
        return 0;

    case WM_KEYUP:
        if (g_game != nullptr) {
            g_game->OnKeyUp(wParam);
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        PaintScene(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, kTimerId);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

}  // namespace

int APIENTRY WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand) {
    Game game;
    g_game = &game;

    const char kClassName[] = "PulseHarborWindowClass";

    WNDCLASSA wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));

    RegisterClassA(&wc);

    RECT desired{0, 0, kWindowWidth, kWindowHeight};
    AdjustWindowRect(&desired, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);

    HWND hwnd = CreateWindowExA(
        0,
        kClassName,
        "Pulse Harbor",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        desired.right - desired.left,
        desired.bottom - desired.top,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (hwnd == nullptr) {
        return 0;
    }

    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return static_cast<int>(msg.wParam);
}
