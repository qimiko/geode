#include <Geode/loader/Loader.hpp>

using namespace geode::prelude;

#include <Geode/modify/AppDelegate.hpp>
#include <Geode/modify/CCApplication.hpp>

namespace {
    void saveModData() {
        log::info("Saving mod data...");
        log::pushNest();

        auto begin = std::chrono::high_resolution_clock::now();

        (void)Loader::get()->saveData();

        auto end = std::chrono::high_resolution_clock::now();
        auto time = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
        log::info("Took {}s", static_cast<float>(time) / 1000.f);

        log::popNest();
    }
}

struct SaveLoader : Modify<SaveLoader, AppDelegate> {
    GEODE_FORWARD_COMPAT_DISABLE_HOOKS("save moved to CCApplication::gameDidSave()")
    void trySaveGame() {
        saveModData();
        return AppDelegate::trySaveGame();
    }
};

#ifdef GEODE_IS_WINDOWS

struct FallbackSaveLoader : Modify<FallbackSaveLoader, CCApplication> {
    GEODE_FORWARD_COMPAT_ENABLE_HOOKS("")
    void gameDidSave() {
        saveModData();
        return CCApplication::gameDidSave();
    }
};

#endif

#ifdef GEODE_IS_ANDROID

#include <Geode/modify/FileOperation.hpp>
#include <Geode/loader/Dirs.hpp>

// redirects the save path to what geode knows, in case launcher's fopen hook fails
gd::string FileOperation_getFilePath_hook() {
    return dirs::getSaveDir().string() + "/";
}

$execute {
    (void)geode::Mod::get()->hook(
        reinterpret_cast<void*>(&FileOperation::getFilePath),
        &FileOperation_getFilePath_hook,
        "FileOperation::getFilePath"
    );
}

#endif
