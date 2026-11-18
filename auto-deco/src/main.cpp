// Auto Deco - editor glue.
// Reads the selected blocks, asks deco_core where decoration pieces go, and
// pastes them into the level as one undoable step.
#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/ui/GeodeUI.hpp>

#include <string>
#include <unordered_map>
#include <vector>

#include "deco_core.hpp"

using namespace geode::prelude;

namespace {

// ---------------------------------------------------------------------------
// Object IDs
// ---------------------------------------------------------------------------

struct OutlineSet {
    int edge;  // straight line along one side of a block
    int inner; // small dot that closes the notch in a concave corner
};

struct GlowSet {
    int edge;  // straight glow strip
    int outer; // glow around a convex corner
    int inner; // glow tucked into a concave corner
};

OutlineSet outlineSet() {
    auto style = Mod::get()->getSettingValue<std::string>("outline-style");
    if (style == "Neon") return {1191, 1194};
    if (style == "Thick") return {1202, 1205};
    if (style == "Custom") {
        return {
            static_cast<int>(Mod::get()->getSettingValue<int64_t>("custom-outline-edge")),
            static_cast<int>(Mod::get()->getSettingValue<int64_t>("custom-outline-inner")),
        };
    }
    return {468, 472}; // Classic
}

GlowSet glowSet() {
    auto size = Mod::get()->getSettingValue<std::string>("glow-size");
    if (size == "Small") return {1292, 1009, 1010};
    if (size == "Large") return {1011, 1012, 1013};
    if (size == "Custom") {
        return {
            static_cast<int>(Mod::get()->getSettingValue<int64_t>("custom-glow-edge")),
            static_cast<int>(Mod::get()->getSettingValue<int64_t>("custom-glow-outer")),
            static_cast<int>(Mod::get()->getSettingValue<int64_t>("custom-glow-inner")),
        };
    }
    return {503, 504, 505}; // Medium
}

int zLayerValue(std::string const& name) {
    if (name == "B4") return -3;
    if (name == "B3") return -1;
    if (name == "B2") return 1;
    if (name == "B1") return 3;
    if (name == "T1") return 5;
    if (name == "T2") return 7;
    if (name == "T3") return 9;
    return 0;
}

// ---------------------------------------------------------------------------
// Sprite probing
// ---------------------------------------------------------------------------

// Renders an object once into an off-screen texture and measures where its
// pixels are relative to its position. Cached per object ID.
deco::Shape probe(int objectID) {
    static std::unordered_map<int, deco::Shape> cache;
    if (auto it = cache.find(objectID); it != cache.end()) return it->second;

    deco::Shape shape;
    constexpr int SIZE = 96; // points; comfortably larger than any 1x1 deco piece

    if (auto obj = GameObject::createWithKey(objectID)) {
        if (auto rt = CCRenderTexture::create(SIZE, SIZE)) {
            obj->setPosition({SIZE / 2.f, SIZE / 2.f});
            obj->setRotation(0.f);
            obj->setScale(1.f);
            obj->setVisible(true);

            rt->beginWithClear(0.f, 0.f, 0.f, 0.f);
            obj->visit();
            rt->end();

            // false = keep OpenGL row order, i.e. row 0 is the bottom row
            if (auto img = rt->newCCImage(false)) {
                shape = deco::measure(
                    img->getData(), img->getWidth(), img->getHeight(),
                    static_cast<float>(SIZE), static_cast<float>(SIZE)
                );
                img->release();
            }
        }
    }

    if (!shape.valid) {
        log::warn("could not measure object {}; using the default orientation", objectID);
    }
    cache[objectID] = shape;
    return shape;
}

deco::Anchors buildAnchors(OutlineSet const& outline, GlowSet const& glow) {
    // Used when a sprite can't be measured (or auto-orient is off).
    deco::Anchors fallback;
    fallback.outlineEdge = {deco::Up, deco::HALF};
    fallback.outlineInner = {deco::TopLeft, {-deco::HALF, deco::HALF}};
    fallback.glowEdge = {deco::Down, deco::HALF};
    fallback.glowOuter = {deco::BottomLeft, {-deco::HALF, -deco::HALF}};
    fallback.glowInner = {deco::BottomLeft, {-deco::HALF, -deco::HALF}};

    if (!Mod::get()->getSettingValue<bool>("auto-orient")) return fallback;

    deco::Anchors a;
    a.outlineEdge = deco::edgeAnchor(probe(outline.edge), fallback.outlineEdge);
    a.outlineInner = deco::cornerAnchor(probe(outline.inner), fallback.outlineInner);
    a.glowEdge = deco::edgeAnchor(probe(glow.edge), fallback.glowEdge);
    a.glowOuter = deco::cornerAnchor(probe(glow.outer), fallback.glowOuter);
    a.glowInner = deco::cornerAnchor(probe(glow.inner), fallback.glowInner);
    return a;
}

// ---------------------------------------------------------------------------
// Selection -> rectangles
// ---------------------------------------------------------------------------

std::vector<deco::Rect> solidRects(CCArray* selection) {
    std::vector<deco::Rect> rects;
    if (!selection) return rects;

    for (auto obj : CCArrayExt<GameObject*>(selection)) {
        if (!obj || obj->m_objectType != GameObjectType::Solid) continue;

        // only axis-aligned blocks can be put on a grid
        float rot = std::fmod(std::fabs(obj->getRotation()), 90.f);
        if (rot > 0.5f && rot < 89.5f) continue;

        CCRect r = obj->getObjectRect();
        if (r.size.width < 1.f || r.size.height < 1.f) {
            // hitbox not available: fall back to the sprite size
            CCSize size = obj->getScaledContentSize();
            bool sideways = std::fabs(std::fmod(std::fabs(obj->getRotation()), 180.f) - 90.f) < 0.5f;
            if (sideways) std::swap(size.width, size.height);
            CCPoint pos = obj->getPosition();
            r = CCRect(pos.x - size.width / 2.f, pos.y - size.height / 2.f, size.width, size.height);
        }
        rects.push_back({r.getMinX(), r.getMinY(), r.getMaxX(), r.getMaxY()});
    }
    return rects;
}

} // namespace

class $modify(AutoDecoUI, EditorUI) {
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;

        auto label = CCLabelBMFont::create("Auto\nDeco", "bigFont.fnt");
        label->setAlignment(kCCTextAlignmentCenter);
        auto sprite = EditorButtonSprite::create(label, EditorBaseColor::Pink);
        auto button = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(AutoDecoUI::onAutoDeco));
        button->setID("auto-deco-button"_spr);

        // Node IDs (if installed and loaded first) gives the edit-button menu an
        // ID and a layout, so the button can simply be appended to it.
        if (auto menu = this->getChildByID("editor-buttons-menu"); menu && menu->getLayout()) {
            if (auto sibling = menu->getChildByType<CCMenuItemSpriteExtra>(0)) {
                float want = sibling->getScaledContentSize().height;
                float have = button->getContentSize().height;
                if (want > 1.f && have > 1.f) button->setScale(want / have);
                button->m_baseScale = button->getScale();
            }
            menu->addChild(button);
            menu->updateLayout();
        } else if (m_copyBtn && m_copyBtn->getParent()) {
            // Vanilla layout: put the button just left of the row the Copy button is in.
            auto menu = m_copyBtn->getParent();
            const CCPoint copyPos = m_copyBtn->getPosition();
            const float size = m_copyBtn->getScaledContentSize().height;
            const float have = button->getContentSize().height;
            if (size > 1.f && have > 1.f) button->setScale(size / have);
            button->m_baseScale = button->getScale();

            float leftEdge = copyPos.x - m_copyBtn->getScaledContentSize().width / 2.f;
            for (auto child : CCArrayExt<CCNode*>(menu->getChildren())) {
                if (!child->isVisible() || std::fabs(child->getPositionY() - copyPos.y) > 5.f) continue;
                leftEdge = std::min(leftEdge, child->getPositionX() - child->getScaledContentSize().width / 2.f);
            }
            button->setPosition({leftEdge - 4.f - button->getScaledContentSize().width / 2.f, copyPos.y});
            menu->addChild(button);
        } else {
            // last resort: a menu of our own in the top right
            auto ownMenu = CCMenu::create();
            ownMenu->setID("auto-deco-menu"_spr);
            auto win = CCDirector::get()->getWinSize();
            ownMenu->setPosition({win.width - 130.f, win.height - 25.f});
            button->setScale(0.8f);
            button->m_baseScale = 0.8f;
            ownMenu->addChild(button);
            this->addChild(ownMenu, 100);
        }

        return true;
    }

    void onAutoDeco(CCObject*) {
        auto rects = solidRects(this->getSelectedObjects());

        if (rects.empty()) {
            createQuickPopup(
                "Auto Deco",
                "Select the <cy>solid blocks</c> you want decorated, then press this button again.\n"
                "Rotated-off-axis blocks, slopes and slabs are skipped.",
                "Settings", "OK",
                [](FLAlertLayer*, bool ok) {
                    if (!ok) openSettingsPopup(Mod::get());
                }
            );
            return;
        }

        std::vector<const char*> parts;
        if (Mod::get()->getSettingValue<bool>("fill")) parts.push_back("fill");
        if (Mod::get()->getSettingValue<bool>("outline")) parts.push_back("outline");
        if (Mod::get()->getSettingValue<bool>("glow")) parts.push_back("glow");
        std::string what = parts.empty() ? "<cr>nothing (everything is switched off)</c>" : "";
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i) what += ", ";
            what += parts[i];
        }

        createQuickPopup(
            "Auto Deco",
            fmt::format(
                "Decorate <cy>{}</c> selected block{}?\nAdding: <cg>{}</c>\n"
                "One <cl>undo</c> removes everything it places.",
                rects.size(), rects.size() == 1 ? "" : "s", what
            ),
            "Settings", "Decorate",
            [](FLAlertLayer*, bool decorate) {
                if (!decorate) {
                    openSettingsPopup(Mod::get());
                    return;
                }
                if (auto ui = EditorUI::get()) static_cast<AutoDecoUI*>(ui)->runAutoDeco();
            }
        );
    }

    void runAutoDeco() {
        auto mod = Mod::get();
        auto rects = solidRects(this->getSelectedObjects());
        if (rects.empty()) return;

        deco::Options options;
        options.fill = mod->getSettingValue<bool>("fill");
        options.outline = mod->getSettingValue<bool>("outline");
        options.glow = mod->getSettingValue<bool>("glow");
        if (!options.fill && !options.outline && !options.glow) {
            Notification::create("Auto Deco: every layer is switched off", NotificationIcon::Warning)->show();
            return;
        }

        auto origin = deco::gridOrigin(rects);
        auto cells = deco::cellsFromRects(rects, origin);
        if (cells.empty()) {
            Notification::create("Auto Deco: no full-size blocks in the selection", NotificationIcon::Warning)->show();
            return;
        }

        const OutlineSet outline = outlineSet();
        const GlowSet glow = glowSet();
        const auto placed = deco::generate(cells, origin, options, buildAnchors(outline, glow));

        const auto maxObjects = static_cast<size_t>(mod->getSettingValue<int64_t>("max-objects"));
        if (placed.size() > maxObjects) {
            Notification::create(
                fmt::format("Auto Deco: {} objects is over your limit of {}", placed.size(), maxObjects),
                NotificationIcon::Error
            )->show();
            return;
        }

        const int fillID = static_cast<int>(mod->getSettingValue<int64_t>("fill-object"));
        const int fillColor = static_cast<int>(mod->getSettingValue<int64_t>("fill-color"));
        const int outlineColor = static_cast<int>(mod->getSettingValue<int64_t>("outline-color"));
        const int glowColor = static_cast<int>(mod->getSettingValue<int64_t>("glow-color"));
        const int blockZ = zLayerValue(mod->getSettingValue<std::string>("block-z-layer"));
        const int glowZ = zLayerValue(mod->getSettingValue<std::string>("glow-z-layer"));
        const int editorLayer = static_cast<int>(mod->getSettingValue<int64_t>("editor-layer"));
        const bool noTouch = mod->getSettingValue<bool>("no-touch");

        // Build a level-format object string. Keys: 1 id, 2 x, 3 y, 6 rotation,
        // 20 editor layer, 21 colour channel, 24 z layer, 25 z order, 121 no-touch.
        // Saved Y is 90 lower than the in-editor node position.
        std::string data;
        data.reserve(placed.size() * 64);
        for (const auto& p : placed) {
            int id = 0, color = 0, zLayer = 0, zOrder = 0;
            switch (p.piece) {
                case deco::Piece::Fill:
                    id = fillID; color = fillColor; zLayer = blockZ; zOrder = 1;
                    break;
                case deco::Piece::OutlineEdge:
                    id = outline.edge; color = outlineColor; zLayer = blockZ; zOrder = 2;
                    break;
                case deco::Piece::OutlineInner:
                    id = outline.inner; color = outlineColor; zLayer = blockZ; zOrder = 2;
                    break;
                case deco::Piece::GlowEdge:
                    id = glow.edge; color = glowColor; zLayer = glowZ; zOrder = 0;
                    break;
                case deco::Piece::GlowOuter:
                    id = glow.outer; color = glowColor; zLayer = glowZ; zOrder = 0;
                    break;
                case deco::Piece::GlowInner:
                    id = glow.inner; color = glowColor; zLayer = glowZ; zOrder = 0;
                    break;
            }
            if (id <= 0) continue;

            data += fmt::format(
                "1,{},2,{:.3f},3,{:.3f},6,{},20,{},21,{},24,{},25,{}{};",
                id, p.x, p.y - 90.f, p.rotation, editorLayer, color, zLayer, zOrder,
                noTouch ? ",121,1" : ""
            );
        }
        if (data.empty()) return;

        // pasteObjects creates the objects, records a single undo step and
        // leaves the new objects selected so they can be tweaked right away.
        auto created = this->pasteObjects(data, false, false);
        this->updateButtons();

        Notification::create(
            fmt::format("Auto Deco: placed {} objects", created ? created->count() : 0u),
            NotificationIcon::Success
        )->show();
    }
};
