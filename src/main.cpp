#define NOMINMAX
#include <windows.h>
#include <gdiplus.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Gdiplus::Color;
using Gdiplus::Graphics;
using Gdiplus::ImageAttributes;
using Gdiplus::Image;
using Gdiplus::RectF;
using Gdiplus::SolidBrush;

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
    float infiniteEnergyTimer = 0.0f;
};

struct Meteor {
    Vec2 pos;
    Vec2 vel;
    float radius = 16.0f;
    float wobble = 0.0f;
    float shootCooldown = 0.0f;
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

struct RingPowerup {
    Vec2 pos;
    float radius = 24.0f;
    float bobPhase = 0.0f;
    float life = 0.0f;
    bool active = false;
};

struct EnemyBullet {
    Vec2 pos;
    Vec2 vel;
    float radius = 13.0f;
    float life = 0.0f;
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
        0.26f,
        0.68f,
        0.12f,
        11.5f,
        6.0f,
        40.0f,
        9.0f,
        45.0f,
        1.05f,
        145.0f,
        0.22f,
        0.54f,
        1.55f,
    };
    return mode == DifficultyMode::Hard ? kHard : kNormal;
}

ULONG_PTR g_gdiplusToken = 0;
std::unique_ptr<Image> g_gameOverImage;
std::unique_ptr<Image> g_ringPowerupImage;
std::unique_ptr<Image> g_enemySpriteImage;
std::unique_ptr<Image> g_poopBulletImage;
std::unique_ptr<Image> g_menuBackgroundImage;
std::unique_ptr<Image> g_swordPulseImage;
std::unique_ptr<Image> g_gameplayBackgroundImage;
std::unique_ptr<Image> g_playerSpriteImage;

std::wstring GetExecutableDirectory() {
    wchar_t buffer[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    std::wstring path(buffer);
    size_t slashPos = path.find_last_of(L"\\/");
    if (slashPos == std::wstring::npos) {
        return L".";
    }
    return path.substr(0, slashPos);
}

bool FileExists(const std::wstring& path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring GetGameOverImagePath() {
    const std::wstring exeDir = GetExecutableDirectory();
    const std::wstring candidateFromBuild = exeDir + L"\\..\\assets\\game_over.png";
    if (FileExists(candidateFromBuild)) {
        return candidateFromBuild;
    }

    const std::wstring candidateLocal = exeDir + L"\\assets\\game_over.png";
    if (FileExists(candidateLocal)) {
        return candidateLocal;
    }

    return candidateFromBuild;
}

std::wstring GetAssetPath(const wchar_t* assetName) {
    const std::wstring exeDir = GetExecutableDirectory();
    const std::wstring candidateFromBuild = exeDir + L"\\..\\assets\\" + assetName;
    if (FileExists(candidateFromBuild)) {
        return candidateFromBuild;
    }

    const std::wstring candidateLocal = exeDir + L"\\assets\\" + assetName;
    if (FileExists(candidateLocal)) {
        return candidateLocal;
    }

    return candidateFromBuild;
}

void EnsureGameOverImageLoaded() {
    if (g_gameOverImage != nullptr) {
        return;
    }

    const std::wstring imagePath = GetGameOverImagePath();
    auto image = std::make_unique<Image>(imagePath.c_str());
    if (image->GetLastStatus() == Gdiplus::Ok) {
        g_gameOverImage = std::move(image);
    }
}

void EnsureRingImageLoaded() {
    if (g_ringPowerupImage != nullptr) {
        return;
    }

    auto image = std::make_unique<Image>(GetAssetPath(L"ring_powerup.png").c_str());
    if (image->GetLastStatus() == Gdiplus::Ok) {
        g_ringPowerupImage = std::move(image);
    }
}

void EnsureEnemySpriteLoaded() {
    if (g_enemySpriteImage != nullptr) {
        return;
    }

    auto image = std::make_unique<Image>(GetAssetPath(L"enemy_sprite.png").c_str());
    if (image->GetLastStatus() == Gdiplus::Ok) {
        g_enemySpriteImage = std::move(image);
    }
}

void EnsurePoopBulletImageLoaded() {
    if (g_poopBulletImage != nullptr) {
        return;
    }

    auto image = std::make_unique<Image>(GetAssetPath(L"poop_bullet.png").c_str());
    if (image->GetLastStatus() == Gdiplus::Ok) {
        g_poopBulletImage = std::move(image);
    }
}

void EnsureMenuBackgroundLoaded() {
    if (g_menuBackgroundImage != nullptr) {
        return;
    }

    auto image = std::make_unique<Image>(GetAssetPath(L"menu_background.png").c_str());
    if (image->GetLastStatus() == Gdiplus::Ok) {
        g_menuBackgroundImage = std::move(image);
    }
}

void EnsureSwordPulseImageLoaded() {
    if (g_swordPulseImage != nullptr) {
        return;
    }

    auto image = std::make_unique<Image>(GetAssetPath(L"sword_pulse.png").c_str());
    if (image->GetLastStatus() == Gdiplus::Ok) {
        g_swordPulseImage = std::move(image);
    }
}

void EnsureGameplayBackgroundLoaded() {
    if (g_gameplayBackgroundImage != nullptr) {
        return;
    }

    auto image = std::make_unique<Image>(GetAssetPath(L"gameplay_background.png").c_str());
    if (image->GetLastStatus() == Gdiplus::Ok) {
        g_gameplayBackgroundImage = std::move(image);
    }
}

void EnsurePlayerSpriteLoaded() {
    if (g_playerSpriteImage != nullptr) {
        return;
    }

    auto image = std::make_unique<Image>(GetAssetPath(L"player_sprite.png").c_str());
    if (image->GetLastStatus() == Gdiplus::Ok) {
        g_playerSpriteImage = std::move(image);
    }
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
        enemyBullets_.clear();
        shards_.clear();
        pulses_.clear();
        ringPowerup_ = {};
        elapsed_ = 0.0f;
        ambientTime_ = 0.0f;
        score_ = 0.0f;
        killScore_ = 0;
        shardScore_ = 0;
        spawnTimer_ = config.initialSpawnTimer;
        ringSpawnTimer_ = RandFloat(9.0f, 16.0f);
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
        UpdateEnemyBullets(dt);
        UpdateShards(dt);
        UpdateRingPowerup(dt);
        UpdatePulseEffects(dt);
        ResolveCollisions();

        score_ = (elapsed_ * 12.0f + static_cast<float>(killScore_ + shardScore_)) * config.scoreMultiplier;
    }

    void Render(HDC hdc, const RECT& clientRect) {
        if (scene_ == Scene::GameOver) {
            DrawGameOver(hdc, clientRect);
            return;
        }

        if (scene_ == Scene::Menu) {
            DrawMenu(hdc, clientRect);
            return;
        }

        DrawBackground(hdc, clientRect);
        DrawGrid(hdc, clientRect);

        DrawShards(hdc);
        DrawMeteors(hdc);
        DrawEnemyBullets(hdc);
        DrawRingPowerup(hdc);
        DrawPulseEffects(hdc);
        DrawPlayer(hdc);
        DrawHud(hdc);

        if (scene_ == Scene::Paused) {
            DrawPauseOverlay(hdc, clientRect);
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
        player_.infiniteEnergyTimer = std::max(0.0f, player_.infiniteEnergyTimer - dt);
        if (player_.infiniteEnergyTimer > 0.0f) {
            player_.energy = 100.0f;
        }
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
        meteor.shootCooldown = RandFloat(1.2f, 3.8f);
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

            meteor.shootCooldown -= dt;
            if (meteor.shootCooldown <= 0.0f) {
                const float shootChance = activeMode_ == DifficultyMode::Hard ? 0.62f : 0.38f;
                if (RandFloat(0.0f, 1.0f) < shootChance) {
                    SpawnEnemyBullet(meteor);
                }
                meteor.shootCooldown = RandFloat(1.6f, activeMode_ == DifficultyMode::Hard ? 3.2f : 4.4f);
            }
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

    void SpawnEnemyBullet(const Meteor& meteor) {
        EnemyBullet bullet;
        bullet.pos = meteor.pos;
        Vec2 aim = Normalize(player_.pos - meteor.pos);
        Vec2 spread{RandFloat(-0.16f, 0.16f), RandFloat(-0.16f, 0.16f)};
        aim = Normalize(aim + spread);
        bullet.vel = aim * RandFloat(activeMode_ == DifficultyMode::Hard ? 230.0f : 195.0f,
                                     activeMode_ == DifficultyMode::Hard ? 320.0f : 260.0f);
        bullet.life = RandFloat(3.5f, 5.0f);
        bullet.radius = RandFloat(11.0f, 15.5f);
        enemyBullets_.push_back(bullet);
    }

    void UpdateEnemyBullets(float dt) {
        for (auto& bullet : enemyBullets_) {
            bullet.life -= dt;
            bullet.pos += bullet.vel * dt;
        }

        enemyBullets_.erase(
            std::remove_if(
                enemyBullets_.begin(),
                enemyBullets_.end(),
                [](const EnemyBullet& bullet) {
                    return bullet.life <= 0.0f ||
                           bullet.pos.x < -80.0f || bullet.pos.x > kWindowWidth + 80.0f ||
                           bullet.pos.y < -80.0f || bullet.pos.y > kWindowHeight + 80.0f;
                }),
            enemyBullets_.end());
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

    void UpdateRingPowerup(float dt) {
        if (ringPowerup_.active) {
            ringPowerup_.life -= dt;
            ringPowerup_.bobPhase += dt * 2.8f;
            if (ringPowerup_.life <= 0.0f) {
                ringPowerup_.active = false;
                ringSpawnTimer_ = RandFloat(8.0f, 14.0f);
            }
            return;
        }

        ringSpawnTimer_ -= dt;
        if (ringSpawnTimer_ > 0.0f) {
            return;
        }

        SpawnRingPowerup();
    }

    void SpawnRingPowerup() {
        ringPowerup_.active = true;
        ringPowerup_.life = 11.0f;
        ringPowerup_.radius = 24.0f;
        ringPowerup_.bobPhase = RandFloat(0.0f, 6.28f);

        for (int attempt = 0; attempt < 20; ++attempt) {
            Vec2 pos{
                RandFloat(90.0f, kWindowWidth - 90.0f),
                RandFloat(90.0f, kWindowHeight - 130.0f),
            };
            if (LengthSquared(pos - player_.pos) < 170.0f * 170.0f) {
                continue;
            }
            ringPowerup_.pos = pos;
            return;
        }

        ringPowerup_.pos = {kWindowWidth * 0.5f, 120.0f};
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

        if (ringPowerup_.active && CircleHit(player_.pos, player_.radius + 3.0f, ringPowerup_.pos, ringPowerup_.radius)) {
            player_.infiniteEnergyTimer = 5.0f;
            player_.energy = 100.0f;
            ringPowerup_.active = false;
            ringSpawnTimer_ = RandFloat(18.0f, 28.0f);
            shardScore_ += 25;
        }

        if (player_.invulnerable > 0.0f) {
            return;
        }

        for (size_t i = 0; i < enemyBullets_.size(); ++i) {
            if (!CircleHit(player_.pos, player_.radius, enemyBullets_[i].pos, enemyBullets_[i].radius)) {
                continue;
            }

            player_.hp -= 1;
            player_.invulnerable = 1.0f;
            pulses_.push_back({player_.pos, 0.16f, 0.16f, 82.0f});
            enemyBullets_.erase(enemyBullets_.begin() + static_cast<long long>(i));

            if (player_.hp <= 0) {
                GetBestScoreRef(activeMode_) = std::max(GetBestScoreRef(activeMode_), static_cast<int>(score_));
                scene_ = Scene::GameOver;
            }
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
        const bool hasInfiniteEnergy = player_.infiniteEnergyTimer > 0.0f;
        if ((!hasInfiniteEnergy && player_.energy < config.pulseCost) || player_.pulseCooldown > 0.0f) {
            return;
        }

        if (!hasInfiniteEnergy) {
            player_.energy -= config.pulseCost;
        }
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
        for (size_t i = 0; i < enemyBullets_.size();) {
            float reach = config.pulseRadius + enemyBullets_[i].radius;
            if (LengthSquared(enemyBullets_[i].pos - player_.pos) <= reach * reach) {
                enemyBullets_.erase(enemyBullets_.begin() + static_cast<long long>(i));
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
        EnsureGameplayBackgroundLoaded();
        if (g_gameplayBackgroundImage != nullptr) {
            Graphics graphics(hdc);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            graphics.DrawImage(g_gameplayBackgroundImage.get(),
                               RectF(0.0f,
                                     0.0f,
                                     static_cast<float>(clientRect.right),
                                     static_cast<float>(clientRect.bottom)));

            SolidBrush dimBrush(Color(58, 0, 0, 0));
            graphics.FillRectangle(&dimBrush,
                                   0.0f,
                                   0.0f,
                                   static_cast<float>(clientRect.right),
                                   static_cast<float>(clientRect.bottom));
            return;
        }

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
        EnsurePlayerSpriteLoaded();
        if (g_playerSpriteImage != nullptr) {
            Graphics graphics(hdc);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);

            const float spriteHeight = 76.0f;
            const float spriteWidth = spriteHeight *
                                      static_cast<float>(g_playerSpriteImage->GetWidth()) /
                                      static_cast<float>(g_playerSpriteImage->GetHeight());
            const float opacity = blinking ? 0.45f : 1.0f;
            Gdiplus::ColorMatrix playerMatrix = {
                1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 0.0f, opacity, 0.0f,
                0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
            };
            ImageAttributes attributes;
            attributes.SetColorMatrix(&playerMatrix,
                                      Gdiplus::ColorMatrixFlagsDefault,
                                      Gdiplus::ColorAdjustTypeBitmap);

            RectF rect(player_.pos.x - spriteWidth * 0.5f,
                       player_.pos.y - spriteHeight * 0.58f,
                       spriteWidth,
                       spriteHeight);
            graphics.DrawImage(g_playerSpriteImage.get(),
                               rect,
                               0.0f,
                               0.0f,
                               static_cast<float>(g_playerSpriteImage->GetWidth()),
                               static_cast<float>(g_playerSpriteImage->GetHeight()),
                               Gdiplus::UnitPixel,
                               &attributes);

            if (player_.invulnerable > 0.0f) {
                DrawOutlineCircle(hdc, player_.pos, player_.radius + 7.0f, RGB(255, 220, 110), 2);
            }
            return;
        }

        COLORREF color = blinking ? RGB(255, 240, 170) : RGB(115, 235, 255);
        DrawFilledCircle(hdc, player_.pos, player_.radius, color);
        DrawOutlineCircle(hdc, player_.pos, player_.radius + 6.0f, RGB(70, 165, 210), 2);
    }

    void DrawMeteors(HDC hdc) {
        EnsureEnemySpriteLoaded();
        for (const auto& meteor : meteors_) {
            if (g_enemySpriteImage != nullptr) {
                Graphics graphics(hdc);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
                const float size = meteor.radius * 3.2f;
                graphics.DrawImage(g_enemySpriteImage.get(),
                                   RectF(meteor.pos.x - size * 0.5f,
                                         meteor.pos.y - size * 0.5f,
                                         size,
                                         size));
            } else {
                COLORREF outer = RGB(255, 130, 90);
                COLORREF inner = RGB(255, 204, 120);
                DrawFilledCircle(hdc, meteor.pos, meteor.radius, outer);
                DrawFilledCircle(hdc, meteor.pos, meteor.radius * 0.48f, inner);
                DrawOutlineCircle(hdc, meteor.pos, meteor.radius + 2.0f, RGB(255, 235, 180), 1);
            }
        }
    }

    void DrawEnemyBullets(HDC hdc) {
        EnsurePoopBulletImageLoaded();
        for (const auto& bullet : enemyBullets_) {
            if (g_poopBulletImage != nullptr) {
                Graphics graphics(hdc);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                const float size = bullet.radius * 2.8f;
                graphics.DrawImage(g_poopBulletImage.get(),
                                   RectF(bullet.pos.x - size * 0.5f,
                                         bullet.pos.y - size * 0.5f,
                                         size,
                                         size));
            } else {
                DrawFilledCircle(hdc, bullet.pos, bullet.radius, RGB(180, 120, 60));
            }
        }
    }

    void DrawShards(HDC hdc) {
        for (const auto& shard : shards_) {
            DrawFilledCircle(hdc, shard.pos, shard.radius, RGB(90, 255, 170));
            DrawOutlineCircle(hdc, shard.pos, shard.radius + 2.0f, RGB(180, 255, 220), 1);
        }
    }

    void DrawPulseEffects(HDC hdc) {
        EnsureSwordPulseImageLoaded();
        Graphics graphics(hdc);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        for (const auto& pulse : pulses_) {
            float t = ClampFloat(1.0f - pulse.life / pulse.maxLife, 0.0f, 1.0f);
            float radius = 42.0f + pulse.maxRadius * (0.25f + 0.75f * t);

            if (g_swordPulseImage == nullptr) {
                int intensity = static_cast<int>(255 - 120 * t);
                DrawOutlineCircle(hdc, pulse.pos, radius, RGB(120, intensity, 255), 3);
                continue;
            }

            const float sweepDegrees = 360.0f * t;
            const float swordAngle = -95.0f + sweepDegrees;
            const float swordRadians = swordAngle * 0.0174532925f;
            const float swordX = pulse.pos.x + std::cos(swordRadians) * radius;
            const float swordY = pulse.pos.y + std::sin(swordRadians) * radius;
            const float fade = ClampFloat(1.0f - t * 0.35f, 0.25f, 1.0f);

            for (int i = 0; i < 9; ++i) {
                float trailT = static_cast<float>(i) / 8.0f;
                float alphaScale = (1.0f - trailT) * fade;
                int alpha = static_cast<int>(145.0f * alphaScale);
                if (alpha <= 0) {
                    continue;
                }

                float trailRadius = radius - trailT * 18.0f;
                float trailSweep = std::min(68.0f + trailT * 38.0f, sweepDegrees);
                float start = swordAngle - trailSweep;
                Gdiplus::Pen outerPen(Color(alpha, 255, 72, 18), 18.0f - trailT * 8.0f);
                Gdiplus::Pen innerPen(Color(static_cast<BYTE>(alpha * 0.75f), 255, 190, 52), 7.0f - trailT * 2.5f);
                graphics.DrawArc(&outerPen,
                                 pulse.pos.x - trailRadius,
                                 pulse.pos.y - trailRadius,
                                 trailRadius * 2.0f,
                                 trailRadius * 2.0f,
                                 start,
                                 trailSweep);
                graphics.DrawArc(&innerPen,
                                 pulse.pos.x - trailRadius,
                                 pulse.pos.y - trailRadius,
                                 trailRadius * 2.0f,
                                 trailRadius * 2.0f,
                                 start + 4.0f,
                                 trailSweep * 0.82f);
            }

            const float swordHeight = 150.0f;
            const float swordWidth = swordHeight *
                                     static_cast<float>(g_swordPulseImage->GetWidth()) /
                                     static_cast<float>(g_swordPulseImage->GetHeight());
            const float flameOpacity = ClampFloat(0.85f - t * 0.25f, 0.35f, 0.85f);
            Gdiplus::ColorMatrix flameMatrix = {
                1.25f, 0.0f, 0.0f, 0.0f, 0.0f,
                0.35f, 0.72f, 0.0f, 0.0f, 0.0f,
                0.0f, 0.10f, 0.12f, 0.0f, 0.0f,
                0.0f, 0.0f, 0.0f, flameOpacity, 0.0f,
                0.18f, 0.04f, 0.0f, 0.0f, 1.0f,
            };
            ImageAttributes flameAttributes;
            flameAttributes.SetColorMatrix(&flameMatrix,
                                           Gdiplus::ColorMatrixFlagsDefault,
                                           Gdiplus::ColorAdjustTypeBitmap);

            Gdiplus::ColorMatrix swordMatrix = {
                1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 0.0f, fade, 0.0f,
                0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
            };
            ImageAttributes swordAttributes;
            swordAttributes.SetColorMatrix(&swordMatrix,
                                           Gdiplus::ColorMatrixFlagsDefault,
                                           Gdiplus::ColorAdjustTypeBitmap);

            Gdiplus::GraphicsState state = graphics.Save();
            graphics.TranslateTransform(swordX, swordY);
            graphics.RotateTransform(swordAngle + 132.0f);

            for (int i = 0; i < 4; ++i) {
                float scale = 1.18f + i * 0.09f;
                float offset = 8.0f + i * 4.0f;
                RectF flameRect(-swordWidth * scale * 0.5f - offset,
                                -swordHeight * scale * 0.5f,
                                swordWidth * scale,
                                swordHeight * scale);
                graphics.DrawImage(g_swordPulseImage.get(),
                                   flameRect,
                                   0.0f,
                                   0.0f,
                                   static_cast<float>(g_swordPulseImage->GetWidth()),
                                   static_cast<float>(g_swordPulseImage->GetHeight()),
                                   Gdiplus::UnitPixel,
                                   &flameAttributes);
            }

            Gdiplus::Pen flameCore(Color(static_cast<BYTE>(210 * fade), 255, 205, 84), 5.0f);
            graphics.DrawLine(&flameCore, -swordWidth * 0.05f, swordHeight * 0.44f, swordWidth * 0.20f, -swordHeight * 0.42f);

            RectF swordRect(-swordWidth * 0.5f, -swordHeight * 0.5f, swordWidth, swordHeight);
            graphics.DrawImage(g_swordPulseImage.get(),
                               swordRect,
                               0.0f,
                               0.0f,
                               static_cast<float>(g_swordPulseImage->GetWidth()),
                               static_cast<float>(g_swordPulseImage->GetHeight()),
                               Gdiplus::UnitPixel,
                               &swordAttributes);
            graphics.Restore(state);
        }
    }

    void DrawRingPowerup(HDC hdc) {
        if (!ringPowerup_.active) {
            return;
        }

        EnsureRingImageLoaded();

        const float bobOffset = std::sin(ringPowerup_.bobPhase) * 7.0f;
        const Vec2 drawPos{ringPowerup_.pos.x, ringPowerup_.pos.y + bobOffset};
        DrawOutlineCircle(hdc, drawPos, ringPowerup_.radius + 8.0f, RGB(255, 214, 110), 2);

        if (g_ringPowerupImage != nullptr) {
            Graphics graphics(hdc);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            const float size = ringPowerup_.radius * 2.7f;
            graphics.DrawImage(g_ringPowerupImage.get(),
                               RectF(drawPos.x - size * 0.5f,
                                     drawPos.y - size * 0.5f,
                                     size,
                                     size));
            return;
        }

        DrawFilledCircle(hdc, drawPos, ringPowerup_.radius, RGB(240, 190, 80));
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

        const bool hasInfiniteEnergy = player_.infiniteEnergyTimer > 0.0f;
        DrawMeter(hdc,
                  730,
                  22,
                  180,
                  18,
                  player_.energy / 100.0f,
                  hasInfiniteEnergy ? RGB(255, 214, 92) : RGB(80, 255, 180),
                  hasInfiniteEnergy ? "Inf.Eng" : "Energy");

        float cooldownRatio = player_.pulseCooldown <= 0.0f ? 1.0f : 1.0f - player_.pulseCooldown / config.pulseCooldown;
        DrawMeter(hdc, 730, 54, 180, 14, cooldownRatio, RGB(120, 180, 255), "Pulse");
        float hpRatio = static_cast<float>(player_.hp) / static_cast<float>(config.hp);
        DrawMeter(hdc, 730, 80, 180, 16, hpRatio, RGB(235, 68, 68), "HP");

        HFONT infoFont = CreateFontA(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, VARIABLE_PITCH, "Segoe UI");
        oldFont = static_cast<HFONT>(SelectObject(hdc, infoFont));
        SetTextColor(hdc, RGB(255, 245, 230));

        if (hasInfiniteEnergy) {
            std::ostringstream ringText;
            ringText << "Ring " << std::fixed << std::setprecision(1) << player_.infiniteEnergyTimer << "s";
            const std::string ringLabel = ringText.str();
            SetTextColor(hdc, RGB(255, 227, 140));
            TextOutA(hdc, 730, 108, ringLabel.c_str(), static_cast<int>(ringLabel.size()));
        }

        const char* controls = "Move WASD / Arrows | Space Pulse | Shift Focus | Esc Pause";
        TextOutA(hdc, 240, 602, controls, lstrlenA(controls));

        SelectObject(hdc, oldFont);
        DeleteObject(infoFont);
    }

    void DrawMenu(HDC hdc, const RECT& clientRect) {
        Graphics graphics(hdc);
        EnsureMenuBackgroundLoaded();
        if (g_menuBackgroundImage != nullptr) {
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            graphics.DrawImage(g_menuBackgroundImage.get(),
                               RectF(0.0f,
                                     0.0f,
                                     static_cast<float>(clientRect.right),
                                     static_cast<float>(clientRect.bottom)));
        } else {
            DrawBackground(hdc, clientRect);
        }

        SolidBrush dimBrush(Color(116, 0, 0, 0));
        graphics.FillRectangle(&dimBrush,
                               0.0f,
                               0.0f,
                               static_cast<float>(clientRect.right),
                               static_cast<float>(clientRect.bottom));

        const bool normalSelected = selectedMode_ == DifficultyMode::Normal;
        const bool hardSelected = selectedMode_ == DifficultyMode::Hard;
        DrawCenteredText(hdc, clientRect, 168, 44, FW_BOLD, RGB(245, 248, 255), "DARK SOULS V");
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
        DrawCenteredText(hdc, clientRect, 238, 40, FW_BOLD, RGB(245, 248, 255), "PAUSED");
        DrawCenteredText(hdc, clientRect, 294, 24, FW_NORMAL, RGB(180, 220, 255),
                         "Press Esc or P to continue.");
    }

    void DrawGameOver(HDC hdc, const RECT& clientRect) {
        const ModeConfig& config = GetModeConfig(activeMode_);
        Graphics graphics(hdc);
        EnsureGameOverImageLoaded();

        if (g_gameOverImage != nullptr) {
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            graphics.DrawImage(g_gameOverImage.get(),
                               RectF(0.0f,
                                     0.0f,
                                     static_cast<float>(clientRect.right),
                                     static_cast<float>(clientRect.bottom)));
        } else {
            DrawBackground(hdc, clientRect);
        }

        SolidBrush dimBrush(Color(84, 0, 0, 0));
        graphics.FillRectangle(&dimBrush,
                               0.0f,
                               0.0f,
                               static_cast<float>(clientRect.right),
                               static_cast<float>(clientRect.bottom));

        RECT banner{
            clientRect.left + 70,
            clientRect.top + 360,
            clientRect.right - 70,
            clientRect.top + 455,
        };
        HBRUSH bannerBrush = CreateSolidBrush(RGB(10, 10, 10));
        HPEN bannerPen = CreatePen(PS_SOLID, 1, RGB(40, 40, 40));
        HGDIOBJ oldBrush = SelectObject(hdc, bannerBrush);
        HGDIOBJ oldPen = SelectObject(hdc, bannerPen);
        RoundRect(hdc, banner.left, banner.top, banner.right, banner.bottom, 12, 12);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(bannerBrush);
        DeleteObject(bannerPen);

        DrawCenteredText(hdc, clientRect, 370, 74, FW_BOLD, RGB(170, 20, 20), "YOU DIED");

        std::ostringstream result;
        result << "Final score " << static_cast<int>(score_);
        DrawCenteredText(hdc, clientRect, 470, 24, FW_BOLD, RGB(255, 245, 220), result.str().c_str());

        std::ostringstream survived;
        survived << "Survived " << std::fixed << std::setprecision(1) << elapsed_ << " seconds";
        DrawCenteredText(hdc, clientRect, 504, 20, FW_NORMAL, RGB(220, 220, 220), survived.str().c_str());

        std::ostringstream best;
        best << config.label << " best " << GetBestScoreRef(activeMode_);
        DrawCenteredText(hdc, clientRect, 534, 20, FW_NORMAL, RGB(200, 200, 200), best.str().c_str());

        DrawCenteredText(hdc, clientRect, 575, 24, FW_BOLD, RGB(255, 234, 170),
                         "Press R or Enter to Restart");
        DrawCenteredText(hdc, clientRect, 608, 18, FW_NORMAL, RGB(200, 210, 220),
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
    std::vector<EnemyBullet> enemyBullets_;
    std::vector<EnergyShard> shards_;
    std::vector<PulseRing> pulses_;
    RingPowerup ringPowerup_;
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
    float ringSpawnTimer_ = 0.0f;
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
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr);

    Game game;
    g_game = &game;

    const char kClassName[] = "DarkSoulsVWindowClass";

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
        "Dark Souls V",
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

    g_gameOverImage.reset();
    g_ringPowerupImage.reset();
    g_enemySpriteImage.reset();
    g_poopBulletImage.reset();
    g_menuBackgroundImage.reset();
    g_swordPulseImage.reset();
    g_gameplayBackgroundImage.reset();
    g_playerSpriteImage.reset();
    if (g_gdiplusToken != 0) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
    }

    return static_cast<int>(msg.wParam);
}
