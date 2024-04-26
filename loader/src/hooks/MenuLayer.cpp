#include "../ui/internal/list/ModListLayer.hpp"

#include <Geode/loader/Index.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/Modify.hpp>
#include <Geode/modify/IDManager.hpp>
#include <Geode/utils/NodeIDs.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/MDPopup.hpp>
#include <Geode/utils/cocos.hpp>
#include <loader/ModImpl.hpp>
#include <loader/LoaderImpl.hpp>
#include <loader/updater.hpp>
#include <Geode/binding/ButtonSprite.hpp>

using namespace geode::prelude;

#pragma warning(disable : 4217)

class CustomMenuLayer;

static Ref<Notification> INDEX_UPDATE_NOTIF = nullptr;

$execute {
    new EventListener<IndexUpdateFilter>(+[](IndexUpdateEvent* event) {
        if (!INDEX_UPDATE_NOTIF) return;
        std::visit(makeVisitor {
            [](UpdateProgress const& prog) {},
            [](UpdateFinished const&) {
                INDEX_UPDATE_NOTIF->setIcon(NotificationIcon::Success);
                INDEX_UPDATE_NOTIF->setString("Index Up-to-Date");
                INDEX_UPDATE_NOTIF->waitAndHide();
                INDEX_UPDATE_NOTIF = nullptr;
            },
            [](UpdateFailed const& info) {
                INDEX_UPDATE_NOTIF->setIcon(NotificationIcon::Error);
                INDEX_UPDATE_NOTIF->setString(info);
                INDEX_UPDATE_NOTIF->setTime(NOTIFICATION_LONG_TIME);
                INDEX_UPDATE_NOTIF = nullptr;
            },
        }, event->status);
    });
};

struct CustomMenuLayer : Modify<CustomMenuLayer, MenuLayer> {
    static void onModify(auto& self) {
        if (!self.setHookPriority("MenuLayer::init", geode::node_ids::GEODE_ID_PRIORITY)) {
            log::warn("Failed to set MenuLayer::init hook priority, node IDs may not work properly");
        }
        GEODE_FORWARD_COMPAT_DISABLE_HOOKS_INNER("MenuLayer stuff disabled")
    }

    struct Fields {
        bool m_menuDisabled = false;
        CCSprite* m_geodeButton = nullptr;
    };

    bool init() {
        if (!MenuLayer::init()) return false;

        // make sure to add the string IDs for nodes (Geode has no manual
        // hook order support yet so gotta do this to ensure)
        NodeIDs::provideFor(this);

        auto winSize = CCDirector::sharedDirector()->getWinSize();

        m_fields->m_menuDisabled = Loader::get()->getLaunchFlag("disable-custom-menu") || !LoaderImpl::get()->hasExternalMods();

        // add geode button
        if (!m_fields->m_menuDisabled) {
            m_fields->m_geodeButton = CircleButtonSprite::createWithSpriteFrameName(
                "geode-logo-outline-gold.png"_spr,
                1.0f,
                CircleBaseColor::Green,
                CircleBaseSize::MediumAlt
            );
            auto geodeBtnSelector = &CustomMenuLayer::onGeode;
            if (!m_fields->m_geodeButton) {
                geodeBtnSelector = &CustomMenuLayer::onMissingTextures;
                m_fields->m_geodeButton = ButtonSprite::create("!!");
            }

            auto bottomMenu = static_cast<CCMenu*>(this->getChildByID("bottom-menu"));

            auto btn = CCMenuItemSpriteExtra::create(
                m_fields->m_geodeButton, this,
                static_cast<SEL_MenuHandler>(geodeBtnSelector)
            );
            btn->setID("geode-button"_spr);
            bottomMenu->addChild(btn);
            bottomMenu->setContentSize({ winSize.width / 2, bottomMenu->getScaledContentSize().height });

            bottomMenu->updateLayout();

            // this->fixSocialMenu();

            if (auto node = this->getChildByID("settings-gamepad-icon")) {
                node->setPositionX(
                    bottomMenu->getChildByID("settings-button")->getPositionX() + winSize.width / 2
                );
            }
        }

        // show if some mods failed to load
        static bool shownFailedNotif = false;
        if (!shownFailedNotif) {
            shownFailedNotif = true;
            auto problems = Loader::get()->getProblems();
            if (std::any_of(problems.begin(), problems.end(), [&](auto& item) {
                    return item.type != LoadProblem::Type::Suggestion && item.type != LoadProblem::Type::Recommendation;
                })) {
                Notification::create("There were problems loading some mods", NotificationIcon::Error)->show();
            }
        }

        // show if the user tried to be naughty and load arbitrary DLLs
        static bool shownTriedToLoadDlls = false;
        if (!shownTriedToLoadDlls) {
            shownTriedToLoadDlls = true;
            if (LoaderImpl::get()->userTriedToLoadDLLs()) {
                auto popup = FLAlertLayer::create(
                    "Hold up!",
                    "It appears that you have tried to <cr>load DLLs</c> with Geode. "
                    "Please note that <cy>Geode is incompatible with ALL DLLs</c>, "
                    "as they can cause Geode mods to <cr>error</c>, or even "
                    "<cr>crash</c>.\n\n"
                    "Remove the DLLs / other mod loaders you have, or <cr>proceed at "
                    "your own risk.</c>",
                    "OK"
                );
                popup->m_scene = this;
                // popup->m_noElasticity = true;
                popup->show();
            }
        }

/*
        // show auto update message
        static bool shownUpdateInfo = false;
        if (updater::isNewUpdateDownloaded() && !shownUpdateInfo) {
            shownUpdateInfo = true;
            auto popup = FLAlertLayer::create(
                "Update downloaded",
                "A new <cy>update</c> for Geode has been installed! "
                "Please <cy>restart the game</c> to apply.",
                "OK"
            );
            popup->m_scene = this;
            // popup->m_noElasticity = true;
            popup->show();
        }
*/

        // show crash info
        static bool shownLastCrash = false;
        if (
            crashlog::didLastLaunchCrash() &&
            !shownLastCrash &&
            !Mod::get()->template getSettingValue<bool>("disable-last-crashed-popup")
        ) {
            shownLastCrash = true;
            auto popup = createQuickPopup(
                "Crashed",
                "It appears that the last session crashed. Would you like to "
                "open the <cy>crashlog folder</c>?",
                "No",
                "Yes",
                [](auto, bool btn2) {
                    if (btn2) {
                        file::openFolder(dirs::getCrashlogsDir());
                    }
                },
                false
            );
            popup->m_scene = this;
            // popup->m_noElasticity = true;
            popup->show();
        }

/*
        // update mods index
        if (!m_fields->m_menuDisabled && !INDEX_UPDATE_NOTIF && !Index::get()->hasTriedToUpdate()) {
            this->addChild(EventListenerNode<IndexUpdateFilter>::create(
                this, &CustomMenuLayer::onIndexUpdate
            ));
            INDEX_UPDATE_NOTIF = Notification::create(
                "Updating Index", NotificationIcon::Loading, 0
            );
            INDEX_UPDATE_NOTIF->setTime(NOTIFICATION_LONG_TIME);
            INDEX_UPDATE_NOTIF->show();
            Index::get()->update();
        }

        this->addUpdateIndicator();
*/

        for (auto mod : Loader::get()->getAllMods()) {
            if (mod->getMetadata().usesDeprecatedIDForm()) {
                log::error(
                    "Mod ID '{}' will be rejected in the future - "
                    "IDs must match the regex `[a-z0-9\\-_]+\\.[a-z0-9\\-_]+`",
                    mod->getID()
                );
            }
        }
    
        return true;
    }

    void fixSocialMenu() {
        // I did NOT have fun doing this
        auto socialMenu = static_cast<CCMenu*>(this->getChildByID("social-media-menu"));
        socialMenu->ignoreAnchorPointForPosition(false);
        socialMenu->setAnchorPoint({0.0f, 0.0f});
        socialMenu->setPosition({13.f, 13.f});

        auto robtopButton = static_cast<CCMenuItemSpriteExtra*>(socialMenu->getChildByID("robtop-logo-button"));
        robtopButton->setPosition(robtopButton->getScaledContentSize() / 2);

        float horizontalGap = 3.5f;
        float verticalGap = 5.0f;
        auto facebookButton = static_cast<CCMenuItemSpriteExtra*>(socialMenu->getChildByID("facebook-button"));
        facebookButton->setPosition({ 
            facebookButton->getScaledContentSize().width / 2,
            robtopButton->getScaledContentSize().height + verticalGap + facebookButton->getScaledContentSize().height / 2
        });

        auto twitterButton = static_cast<CCMenuItemSpriteExtra*>(socialMenu->getChildByID("twitter-button"));
        twitterButton->setPosition({
            facebookButton->getScaledContentSize().width + horizontalGap + twitterButton->getScaledContentSize().width / 2,
            robtopButton->getScaledContentSize().height + verticalGap + twitterButton->getScaledContentSize().height / 2
        });

        auto youtubeButton = static_cast<CCMenuItemSpriteExtra*>(socialMenu->getChildByID("youtube-button"));
        youtubeButton->setPosition({
            twitterButton->getPositionX() + twitterButton->getScaledContentSize().width / 2 + horizontalGap + youtubeButton->getScaledContentSize().width / 2,
            robtopButton->getScaledContentSize().height + verticalGap + youtubeButton->getScaledContentSize().height / 2
        });

        socialMenu->setContentSize({
            youtubeButton->getPositionX() + youtubeButton->getScaledContentSize().width / 2,
            facebookButton->getPositionY() + facebookButton->getScaledContentSize().height / 2
        });

        auto bottomMenu = static_cast<CCMenu*>(this->getChildByID("bottom-menu"));
        float spacing = 5.0f;
        float buttonMenuLeftMargin = bottomMenu->getPositionX() - bottomMenu->getScaledContentSize().width * bottomMenu->getAnchorPoint().x;
        float overlap = (socialMenu->getPositionX() + socialMenu->getScaledContentSize().width) - buttonMenuLeftMargin + spacing;
        if (overlap > 0) {
            float neededContentSize = buttonMenuLeftMargin - spacing - socialMenu->getPositionX();
            float neededSize = neededContentSize * socialMenu->getScale() / socialMenu->getScaledContentSize().width;
            socialMenu->setScale(neededSize);
        }
    }

    void onIndexUpdate(IndexUpdateEvent* event) {
        if (
            std::holds_alternative<UpdateFinished>(event->status) ||
            std::holds_alternative<UpdateFailed>(event->status)
        ) {
            this->addUpdateIndicator();
        }
    }

    void addUpdateIndicator() {
        if (!m_fields->m_menuDisabled && Index::get()->areUpdatesAvailable()) {
            auto icon = CCSprite::createWithSpriteFrameName("updates-available.png"_spr);
            icon->setPosition(
                m_fields->m_geodeButton->getContentSize() - CCSize { 10.f, 10.f }
            );
            icon->setZOrder(99);
            icon->setScale(.5f);
            m_fields->m_geodeButton->addChild(icon);
        }
    }

    void onMissingTextures(CCObject*) {
        
    #ifdef GEODE_IS_DESKTOP

        (void) utils::file::createDirectoryAll(dirs::getGeodeDir() / "update" / "resources" / "geode.loader");

        createQuickPopup(
            "Missing Textures",
            "You appear to be missing textures, and the automatic texture fixer "
            "hasn't fixed the issue.\n"
            "Download <cy>resources.zip</c> from the latest release on GitHub, "
            "and <cy>unzip its contents</c> into <cb>geode/update/resources/geode.loader</c>.\n"
            "Afterwards, <cg>restart the game</c>.\n"
            "You may also continue without installing resources, but be aware that "
            "you won't be able to open <cr>the Geode menu</c>.",
            "Dismiss", "Open Github",
            [](auto, bool btn2) {
                if (btn2) {
                    web::openLinkInBrowser("https://github.com/geode-sdk/geode/releases/latest");
                    file::openFolder(dirs::getGeodeDir() / "update" / "resources");
                    FLAlertLayer::create(
                        "Info",
                        "Opened GitHub in your browser and the destination in "
                        "your file browser.\n"
                        "Download <cy>resources.zip</c>, "
                        "and <cy>unzip its contents</c> into the destination "
                        "folder.\n"
                        "<cb>Don't add any new folders to the destination!</c>",
                        "OK"
                    )->show();
                }
            }
        );

    #else

        // dunno if we can auto-create target directory on mobile, nor if the 
        // user has access to moving stuff there

        FLAlertLayer::create(
            "Missing Textures",
            "You appear to be missing textures, and the automatic texture fixer "
            "hasn't fixed the issue.\n"
            "**<cy>Report this bug to the Geode developers</c>**. It is very likely "
            "that your game <cr>will crash</c> until the issue is resolved.",
            "OK"
        )->show();

    #endif
    }

    void onGeode(CCObject*) {
        ModListLayer::scene();
    }
};
