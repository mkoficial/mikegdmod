/*
 * ============================================================================
 *  MOD MENU COMPLETO - Geometry Dash 2.2081 (Geode SDK v4.x)
 *  Alvos: Windows e Android
 * ============================================================================
 *
 *  RECURSOS
 *  --------
 *  PLAYER
 *    - Noclip (com contador de "hits" na tela)
 *    - Jump Hack (pulo infinito no ar)
 *    - Safe Mode (marca a run como Test Mode quando ha cheats ativos, assim
 *      nenhum progresso/porcentagem e salvo no seu save)
 *  SPEED
 *    - Speedhack (0.05x ate 10x), com ajuste fino e presets
 *    - Pitch da musica acompanha a velocidade (opcional)
 *    - Opcao de aplicar so dentro de niveis
 *  VISUAL
 *    - Mostrar hitboxes
 *    - Remover efeito de morte
 *  MISC
 *    - Desbloquear todos os icones e cores (so local)
 *    - FPS Bypass (30 ate 360)
 *    - Resetar todas as configuracoes
 *
 *  COMO ABRIR O MENU
 *    - Botao "H" no menu principal (barra inferior)
 *    - Botao "H" no menu de pause (dentro do nivel)
 *
 *  Tudo e salvo automaticamente (Mod::setSavedValue).
 *
 *  REQUISITOS: Geode SDK v4.x, mod.json com "gd": {"win": "2.2081", "android": "2.2081"}
 * ============================================================================
 */

#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/GameManager.hpp>
#include <Geode/modify/CCScheduler.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <string>
#include <utility>
#include <vector>

using namespace geode::prelude;

// ============================================================================
//  CONFIGURACAO GLOBAL
// ============================================================================
namespace hax {

    struct Config {
        // Player
        static inline bool noclip = false;
        static inline bool noclipCounter = true;
        static inline bool jumpHack = false;
        static inline bool safeMode = true;

        // Speed
        static inline bool speedhack = false;
        static inline bool speedAudio = true;
        static inline bool speedOnlyInLevel = true;
        static inline float speed = 1.f;

        // Visual
        static inline bool hitboxes = false;
        static inline bool noDeathEffect = false;

        // Misc
        static inline bool unlockAll = false;
        static inline bool fpsBypass = false;
        static inline int fps = 60;
        static inline double origInterval = 0.0; // intervalo original do jogo
    };

    template <class T>
    inline void set(char const* key, T& ref, T value) {
        ref = value;
        Mod::get()->setSavedValue<T>(key, value);
    }

    inline void load() {
        auto m = Mod::get();
        Config::noclip = m->getSavedValue<bool>("noclip", false);
        Config::noclipCounter = m->getSavedValue<bool>("noclip-counter", true);
        Config::jumpHack = m->getSavedValue<bool>("jump-hack", false);
        Config::safeMode = m->getSavedValue<bool>("safe-mode", true);
        Config::speedhack = m->getSavedValue<bool>("speedhack", false);
        Config::speedAudio = m->getSavedValue<bool>("speed-audio", true);
        Config::speedOnlyInLevel = m->getSavedValue<bool>("speed-only-in-level", true);
        Config::speed = m->getSavedValue<float>("speed", 1.f);
        Config::hitboxes = m->getSavedValue<bool>("hitboxes", false);
        Config::noDeathEffect = m->getSavedValue<bool>("no-death-effect", false);
        Config::unlockAll = m->getSavedValue<bool>("unlock-all", false);
        Config::fpsBypass = m->getSavedValue<bool>("fps-bypass", false);
        Config::fps = m->getSavedValue<int>("fps", 60);
    }

    inline void resetAll() {
        set<bool>("noclip", Config::noclip, false);
        set<bool>("noclip-counter", Config::noclipCounter, true);
        set<bool>("jump-hack", Config::jumpHack, false);
        set<bool>("safe-mode", Config::safeMode, true);
        set<bool>("speedhack", Config::speedhack, false);
        set<bool>("speed-audio", Config::speedAudio, true);
        set<bool>("speed-only-in-level", Config::speedOnlyInLevel, true);
        set<float>("speed", Config::speed, 1.f);
        set<bool>("hitboxes", Config::hitboxes, false);
        set<bool>("no-death-effect", Config::noDeathEffect, false);
        set<bool>("unlock-all", Config::unlockAll, false);
        set<bool>("fps-bypass", Config::fpsBypass, false);
        set<int>("fps", Config::fps, 60);
    }

    // Ha algum cheat que afeta a legitimidade de uma run?
    inline bool cheatsActive() {
        return Config::noclip
            || Config::jumpHack
            || (Config::speedhack && std::abs(Config::speed - 1.f) > 0.001f);
    }

    // Aplica o pitch da musica (FMOD) conforme o speedhack
    inline void applyAudioPitch(bool forceNormal = false) {
        auto engine = FMODAudioEngine::sharedEngine();
        if (!engine || !engine->m_system) return;

        bool inLevel = PlayLayer::get() != nullptr;
        bool active = Config::speedhack
            && Config::speedAudio
            && (inLevel || !Config::speedOnlyInLevel)
            && !forceNormal;

        FMOD::ChannelGroup* group = nullptr;
        if (engine->m_system->getMasterChannelGroup(&group) == FMOD_OK && group) {
            group->setPitch(active ? Config::speed : 1.f);
        }
    }

    // Aplica o FPS bypass
    inline void applyFps() {
        auto director = CCDirector::sharedDirector();
        if (Config::origInterval <= 0.0) {
            Config::origInterval = director->getAnimationInterval();
        }
        if (Config::fpsBypass) {
            director->setAnimationInterval(1.0 / static_cast<double>(std::clamp(Config::fps, 30, 360)));
        }
        else {
            director->setAnimationInterval(Config::origInterval);
        }
    }

    inline void applyStartup() {
        applyFps();
        applyAudioPitch();
    }

} // namespace hax

// ============================================================================
//  POPUP DO MENU
// ============================================================================
class HackMenu : public geode::Popup<> {
protected:
    static constexpr size_t PAGE_COUNT = 4;

    std::array<std::vector<CCNode*>, PAGE_COUNT> m_pageNodes;
    std::array<ButtonSprite*, PAGE_COUNT> m_tabSprites{};
    CCLabelBMFont* m_speedLabel = nullptr;
    CCLabelBMFont* m_fpsLabel = nullptr;

    // Converte coordenada local da mainLayer para o espaco do m_buttonMenu
    CCPoint toMenu(CCPoint p) {
        return m_buttonMenu->convertToNodeSpace(m_mainLayer->convertToWorldSpace(p));
    }

    // ------------------------------------------------------------------ UI helpers
    CCLabelBMFont* addLabel(size_t page, std::string const& text, float scale, CCPoint pos, CCPoint anchor = { 0.5f, 0.5f }) {
        auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        label->setScale(scale);
        label->setAnchorPoint(anchor);
        label->setPosition(pos);
        m_mainLayer->addChild(label);
        m_pageNodes[page].push_back(label);
        return label;
    }

    CCMenuItemSpriteExtra* addButton(size_t page, char const* text, float scale, CCPoint pos, std::function<void()> cb) {
        auto spr = ButtonSprite::create(text);
        spr->setScale(scale);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [cb](CCMenuItemSpriteExtra*) { cb(); });
        btn->setPosition(toMenu(pos));
        m_buttonMenu->addChild(btn);
        m_pageNodes[page].push_back(btn);
        return btn;
    }

    void addToggle(size_t page, char const* title, char const* desc, bool value, float y, std::function<void(bool)> cb) {
        auto width = m_mainLayer->getContentSize().width;

        auto toggler = CCMenuItemExt::createTogglerWithStandardSprites(0.6f, [cb](CCMenuItemToggler* t) {
            // o estado so inverte depois do callback
            cb(!t->isToggled());
        });
        toggler->toggle(value);
        toggler->setPosition(toMenu({ 40.f, y }));
        m_buttonMenu->addChild(toggler);
        m_pageNodes[page].push_back(toggler);

        auto label = addLabel(page, title, 0.5f, { 64.f, y }, { 0.f, 0.5f });
        label->limitLabelWidth(260.f, 0.5f, 0.2f);

        auto info = InfoAlertButton::create(title, desc, 0.6f);
        info->setPosition(toMenu({ width - 40.f, y }));
        m_buttonMenu->addChild(info);
        m_pageNodes[page].push_back(info);
    }

    // ------------------------------------------------------------------ Speed helpers
    void refreshSpeedLabel() {
        if (m_speedLabel) {
            m_speedLabel->setString(fmt::format("Speed: {:.2f}x", hax::Config::speed).c_str());
        }
    }

    void setSpeed(float v) {
        v = std::clamp(std::round(v * 100.f) / 100.f, 0.05f, 10.f);
        hax::set<float>("speed", hax::Config::speed, v);
        refreshSpeedLabel();
        hax::applyAudioPitch();
    }

    void refreshFpsLabel() {
        if (m_fpsLabel) {
            m_fpsLabel->setString(fmt::format("FPS: {}", hax::Config::fps).c_str());
        }
    }

    void setFps(int v) {
        v = std::clamp(v, 30, 360);
        hax::set<int>("fps", hax::Config::fps, v);
        refreshFpsLabel();
        hax::applyFps();
    }

    // ------------------------------------------------------------------ Paginas
    void showPage(size_t page) {
        for (size_t i = 0; i < PAGE_COUNT; i++) {
            for (auto node : m_pageNodes[i]) {
                node->setVisible(i == page);
            }
            if (m_tabSprites[i]) {
                m_tabSprites[i]->updateBGImage(i == page ? "GJ_button_02.png" : "GJ_button_01.png");
            }
        }
    }

    void buildTabs(CCSize size) {
        char const* names[PAGE_COUNT] = { "Player", "Speed", "Visual", "Misc" };
        float spacing = 92.f;
        float startX = size.width / 2.f - spacing * (PAGE_COUNT - 1) / 2.f;

        for (size_t i = 0; i < PAGE_COUNT; i++) {
            auto spr = ButtonSprite::create(names[i], 80, true, "bigFont.fnt", "GJ_button_01.png", 28.f, 0.6f);
            m_tabSprites[i] = spr;
            auto btn = CCMenuItemExt::createSpriteExtra(spr, [this, i](CCMenuItemSpriteExtra*) {
                this->showPage(i);
            });
            btn->setPosition(toMenu({ startX + spacing * i, size.height - 52.f }));
            m_buttonMenu->addChild(btn);
        }
    }

    void buildPlayerPage(CCSize size) {
        float y = size.height - 92.f;
        float step = 36.f;

        addToggle(0, "Noclip", "Voce nao morre ao tocar em espinhos ou blocos.", hax::Config::noclip, y,
            [](bool on) { hax::set<bool>("noclip", hax::Config::noclip, on); });
        y -= step;

        addToggle(0, "Noclip Counter", "Mostra no canto da tela quantas vezes voce 'morreria' com o noclip ligado.", hax::Config::noclipCounter, y,
            [](bool on) { hax::set<bool>("noclip-counter", hax::Config::noclipCounter, on); });
        y -= step;

        addToggle(0, "Jump Hack", "Pulo infinito: permite pular mesmo estando no ar.", hax::Config::jumpHack, y,
            [](bool on) { hax::set<bool>("jump-hack", hax::Config::jumpHack, on); });
        y -= step;

        addToggle(0, "Safe Mode", "Quando algum cheat esta ativo, a run vira Test Mode: nada de progresso, porcentagem ou conclusao e salvo. Para sair do modo, saia do nivel.", hax::Config::safeMode, y,
            [](bool on) { hax::set<bool>("safe-mode", hax::Config::safeMode, on); });
    }

    void buildSpeedPage(CCSize size) {
        float cx = size.width / 2.f;
        float y = size.height - 92.f;
        float step = 36.f;

        addToggle(1, "Speedhack", "Muda a velocidade do jogo pelo multiplicador abaixo.", hax::Config::speedhack, y,
            [](bool on) {
                hax::set<bool>("speedhack", hax::Config::speedhack, on);
                hax::applyAudioPitch();
            });
        y -= step;

        addToggle(1, "Speed Audio", "A musica acompanha a velocidade (pitch).", hax::Config::speedAudio, y,
            [](bool on) {
                hax::set<bool>("speed-audio", hax::Config::speedAudio, on);
                hax::applyAudioPitch();
            });
        y -= step;

        addToggle(1, "Only In Level", "Aplica o speedhack apenas dentro dos niveis (menus ficam normais).", hax::Config::speedOnlyInLevel, y,
            [](bool on) {
                hax::set<bool>("speed-only-in-level", hax::Config::speedOnlyInLevel, on);
                hax::applyAudioPitch();
            });
        y -= step + 2.f;

        // Ajuste fino
        m_speedLabel = addLabel(1, "", 0.55f, { cx, y });
        refreshSpeedLabel();

        addButton(1, "-0.25", 0.55f, { cx - 150.f, y }, [this] { setSpeed(hax::Config::speed - 0.25f); });
        addButton(1, "-0.05", 0.55f, { cx - 100.f, y }, [this] { setSpeed(hax::Config::speed - 0.05f); });
        addButton(1, "+0.05", 0.55f, { cx + 100.f, y }, [this] { setSpeed(hax::Config::speed + 0.05f); });
        addButton(1, "+0.25", 0.55f, { cx + 150.f, y }, [this] { setSpeed(hax::Config::speed + 0.25f); });
        y -= step;

        // Presets
        float presets[] = { 0.25f, 0.5f, 1.f, 1.5f, 2.f, 3.f };
        for (int i = 0; i < 6; i++) {
            float v = presets[i];
            addButton(1, fmt::format("{}x", v).c_str(), 0.5f, { cx + (i - 2.5f) * 62.f, y }, [this, v] { setSpeed(v); });
        }
    }

    void buildVisualPage(CCSize size) {
        float y = size.height - 92.f;
        float step = 36.f;

        addToggle(2, "Show Hitboxes", "Desenha as hitboxes de objetos e do player durante o nivel. Pode ser necessario reiniciar a tentativa para aparecer.", hax::Config::hitboxes, y,
            [](bool on) { hax::set<bool>("hitboxes", hax::Config::hitboxes, on); });
        y -= step;

        addToggle(2, "No Death Effect", "Remove a explosao do player ao morrer.", hax::Config::noDeathEffect, y,
            [](bool on) { hax::set<bool>("no-death-effect", hax::Config::noDeathEffect, on); });
    }

    void buildMiscPage(CCSize size) {
        float cx = size.width / 2.f;
        float y = size.height - 92.f;
        float step = 36.f;

        addToggle(3, "Unlock All", "Libera todos os icones e cores localmente.", hax::Config::unlockAll, y,
            [](bool on) { hax::set<bool>("unlock-all", hax::Config::unlockAll, on); });
        y -= step;

        addToggle(3, "FPS Bypass", "Usa o FPS escolhido abaixo no lugar do limite padrao.", hax::Config::fpsBypass, y,
            [](bool on) {
                hax::set<bool>("fps-bypass", hax::Config::fpsBypass, on);
                hax::applyFps();
            });
        y -= step;

        m_fpsLabel = addLabel(3, "", 0.55f, { cx, y });
        refreshFpsLabel();
        addButton(3, "-60", 0.55f, { cx - 150.f, y }, [this] { setFps(hax::Config::fps - 60); });
        addButton(3, "-10", 0.55f, { cx - 100.f, y }, [this] { setFps(hax::Config::fps - 10); });
        addButton(3, "+10", 0.55f, { cx + 100.f, y }, [this] { setFps(hax::Config::fps + 10); });
        addButton(3, "+60", 0.55f, { cx + 150.f, y }, [this] { setFps(hax::Config::fps + 60); });
        y -= step;

        addButton(3, "Reset All", 0.6f, { cx, y - 4.f }, [this] {
            createQuickPopup(
                "Reset",
                "Resetar <cr>todas</c> as configuracoes do menu?",
                "Cancel", "Reset",
                [this](FLAlertLayer*, bool btn2) {
                    if (!btn2) return;
                    hax::resetAll();
                    hax::applyFps();
                    hax::applyAudioPitch();
                    // reabre o menu para atualizar todos os toggles
                    this->onClose(nullptr);
                    if (auto menu = HackMenu::create()) menu->show();
                }
            );
        });
    }

    bool setup() override {
        this->setTitle("Mod Menu");

        auto size = m_mainLayer->getContentSize();
        buildTabs(size);
        buildPlayerPage(size);
        buildSpeedPage(size);
        buildVisualPage(size);
        buildMiscPage(size);
        showPage(0);

        return true;
    }

public:
    static HackMenu* create() {
        auto ret = new HackMenu();
        if (ret->initAnchored(420.f, 270.f)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

static void openHackMenu() {
    if (auto menu = HackMenu::create()) {
        menu->show();
    }
}

// ============================================================================
//  HOOKS: BOTOES DE ACESSO AO MENU
// ============================================================================
class $modify(HackMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        static bool firstRun = true;
        if (firstRun) {
            firstRun = false;
            hax::applyStartup();
        }

        auto label = CCLabelBMFont::create("H", "bigFont.fnt");
        auto spr = CircleButtonSprite::create(label, CircleBaseColor::Green, CircleBaseSize::MediumAlt);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [](CCMenuItemSpriteExtra*) {
            openHackMenu();
        });
        btn->setID("hack-menu-button"_spr);

        if (auto menu = this->getChildByID("bottom-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        }
        else {
            auto win = CCDirector::sharedDirector()->getWinSize();
            auto m = CCMenu::create();
            m->setPosition({ win.width - 30.f, 30.f });
            btn->setPosition({ 0.f, 0.f });
            m->addChild(btn);
            this->addChild(m);
        }
        return true;
    }
};

class $modify(HackPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto label = CCLabelBMFont::create("H", "bigFont.fnt");
        auto spr = CircleButtonSprite::create(label, CircleBaseColor::Green, CircleBaseSize::Small);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [](CCMenuItemSpriteExtra*) {
            openHackMenu();
        });
        btn->setID("hack-menu-button"_spr);

        if (auto menu = this->getChildByID("right-button-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        }
        else {
            auto win = CCDirector::sharedDirector()->getWinSize();
            auto m = CCMenu::create();
            m->setPosition({ win.width - 30.f, 30.f });
            btn->setPosition({ 0.f, 0.f });
            m->addChild(btn);
            this->addChild(m);
        }
    }
};

// ============================================================================
//  HOOK: SPEEDHACK (scheduler global)
// ============================================================================
class $modify(HackScheduler, CCScheduler) {
    void update(float dt) {
        if (hax::Config::speedhack && std::abs(hax::Config::speed - 1.f) > 0.001f) {
            if (!hax::Config::speedOnlyInLevel || PlayLayer::get()) {
                dt *= hax::Config::speed;
            }
        }
        CCScheduler::update(dt);
    }
};

// ============================================================================
//  HOOK: PLAYLAYER (noclip, safe mode, hitboxes, contador)
// ============================================================================
class $modify(HackPlayLayer, PlayLayer) {
    struct Fields {
        CCLabelBMFont* label = nullptr;
        int hits = 0;
        std::chrono::steady_clock::time_point last{};
        bool hitboxesForced = false;
    };

    void refreshCounter() {
        if (!m_fields->label) return;
        m_fields->label->setString(fmt::format("Noclip hits: {}", m_fields->hits).c_str());
    }

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();

        auto win = CCDirector::sharedDirector()->getWinSize();
        auto label = CCLabelBMFont::create("Noclip hits: 0", "bigFont.fnt");
        label->setScale(0.4f);
        label->setOpacity(150);
        label->setAnchorPoint({ 0.f, 1.f });
        label->setPosition({ 8.f, win.height - 8.f });
        label->setVisible(false);
        m_uiLayer->addChild(label, 100);
        m_fields->label = label;

        hax::applyAudioPitch();
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        m_fields->hits = 0;
        refreshCounter();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        // o spike anti-cheat do jogo nunca deve ser ignorado
        if (object == m_anticheatSpike) {
            return PlayLayer::destroyPlayer(player, object);
        }

        if (hax::Config::noclip) {
            // debounce: conta como 1 hit se ficar >200ms sem colisao
            auto now = std::chrono::steady_clock::now();
            if (now - m_fields->last > std::chrono::milliseconds(200)) {
                m_fields->hits++;
                refreshCounter();
            }
            m_fields->last = now;
            return;
        }

        PlayLayer::destroyPlayer(player, object);
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        // Safe Mode: tenta nao salvar nada se houver cheats ativos
        if (hax::Config::safeMode && hax::cheatsActive()) {
            m_isTestMode = true;
        }

        if (m_fields->label) {
            m_fields->label->setVisible(hax::Config::noclip && hax::Config::noclipCounter);
        }

        // Hitboxes
        if (hax::Config::hitboxes) {
            m_isDebugDrawEnabled = true;
            m_fields->hitboxesForced = true;
            this->updateDebugDraw();
        }
        else if (m_fields->hitboxesForced) {
            m_fields->hitboxesForced = false;
            m_isDebugDrawEnabled = false;
            this->updateDebugDraw();
        }
    }

    void onQuit() {
        hax::applyAudioPitch(true); // volta a musica ao normal
        PlayLayer::onQuit();
    }
};

// ============================================================================
//  HOOK: PLAYER (jump hack, efeito de morte)
// ============================================================================
class $modify(HackPlayer, PlayerObject) {
    bool pushButton(PlayerButton button) {
        if (hax::Config::jumpHack) {
            m_isOnGround = true; // engana o jogo: sempre "no chao" ao apertar
        }
        return PlayerObject::pushButton(button);
    }

    void playDeathEffect() {
        if (hax::Config::noDeathEffect) return;
        PlayerObject::playDeathEffect();
    }
};

// ============================================================================
//  HOOK: GAMEMANAGER (unlock all)
// ============================================================================
class $modify(HackGameManager, GameManager) {
    bool isIconUnlocked(int id, IconType type) {
        if (hax::Config::unlockAll) return true;
        return GameManager::isIconUnlocked(id, type);
    }

    bool isColorUnlocked(int id, UnlockType type) {
        if (hax::Config::unlockAll) return true;
        return GameManager::isColorUnlocked(id, type);
    }
};

// ============================================================================
//  CARREGAMENTO DO MOD
// ============================================================================
$on_mod(Loaded) {
    hax::load();
}
