
#include "LoaderImpl.hpp"

#include <Geode/loader/Dirs.hpp>
#include <Geode/loader/IPC.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/map.hpp>
#include <Geode/utils/ranges.hpp>
#include <Geode/utils/web.hpp>
#include <InternalMod.hpp>
#include <about.hpp>
#include <crashlog.hpp>
#include <fmt/format.h>
#include <hash.hpp>
#include <iostream>
#include <resources.hpp>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

USE_GEODE_NAMESPACE();

Loader::Impl* LoaderImpl::get() {
    return Loader::get()->m_impl.get();
}

Loader::Impl::Impl() {}

Loader::Impl::~Impl() {}

// Initialization

void Loader::Impl::createDirectories() {
#ifdef GEODE_IS_MACOS
    ghc::filesystem::create_directory(dirs::getSaveDir());
#endif

    ghc::filesystem::create_directories(dirs::getGeodeResourcesDir());
    ghc::filesystem::create_directory(dirs::getModConfigDir());
    ghc::filesystem::create_directory(dirs::getModsDir());
    ghc::filesystem::create_directory(dirs::getGeodeLogDir());
    ghc::filesystem::create_directory(dirs::getTempDir());
    ghc::filesystem::create_directory(dirs::getModRuntimeDir());

    if (!ranges::contains(m_modSearchDirectories, dirs::getModsDir())) {
        m_modSearchDirectories.push_back(dirs::getModsDir());
    }
}

Result<> Loader::Impl::setup() {
    if (m_isSetup) {
        return Ok();
    }

    log::Logger::setup();

    if (crashlog::setupPlatformHandler()) {
        log::debug("Set up platform crash logger");
    }
    else {
        log::debug("Unable to set up platform crash logger");
    }

    log::debug("Setting up Loader...");

    log::debug("Set up internal mod representation");
    log::debug("Loading hooks... ");

    if (!this->loadHooks()) {
        return Err("There were errors loading some hooks, see console for details");
    }

    log::debug("Loaded hooks");

    log::debug("Setting up IPC...");

    this->setupIPC();

    this->createDirectories();
    auto sett = this->loadData();
    if (!sett) {
        log::warn("Unable to load loader settings: {}", sett.unwrapErr());
    }
    this->refreshModsList();

    this->queueInGDThread([]() {
        Loader::get()->addSearchPaths();
    });

    m_isSetup = true;

    return Ok();
}

void Loader::Impl::addSearchPaths() {
    CCFileUtils::get()->addPriorityPath(dirs::getGeodeResourcesDir().string().c_str());
    CCFileUtils::get()->addPriorityPath(dirs::getModRuntimeDir().string().c_str());
}

void Loader::Impl::updateResources() {
    log::debug("Adding resources");

    // add own spritesheets
    this->updateModResources(InternalMod::get());

    // add mods' spritesheets
    for (auto const& [_, mod] : m_mods) {
        this->updateModResources(mod);
    }
}

std::vector<Mod*> Loader::Impl::getAllMods() {
    return map::values(m_mods);
}

Mod* Loader::Impl::getInternalMod() {
    return InternalMod::get();
}

std::vector<InvalidGeodeFile> Loader::Impl::getFailedMods() const {
    return m_invalidMods;
}

// Version info

VersionInfo Loader::Impl::getVersion() {
    return LOADER_VERSION;
}

VersionInfo Loader::Impl::minModVersion() {
    return VersionInfo { 0, 3, 1 };
}

VersionInfo Loader::Impl::maxModVersion() {
    return VersionInfo {
        this->getVersion().getMajor(),
        this->getVersion().getMinor(),
        // todo: dynamic version info (vM.M.*)
        99999999,
    };
}

bool Loader::Impl::isModVersionSupported(VersionInfo const& version) {
    return
        version >= this->minModVersion() &&
        version <= this->maxModVersion();
}

// Data saving

Result<> Loader::Impl::saveData() {
    // save mods' data
    for (auto& [_, mod] : m_mods) {
        auto r = mod->saveData();
        if (!r) {
            log::warn("Unable to save data for mod \"{}\": {}", mod->getID(), r.unwrapErr());
        }
    }
    // save loader data
    GEODE_UNWRAP(InternalMod::get()->saveData());
    
    return Ok();
}

Result<> Loader::Impl::loadData() {
    auto e = InternalMod::get()->loadData();
    if (!e) {
        log::warn("Unable to load loader settings: {}", e.unwrapErr());
    }
    for (auto& [_, mod] : m_mods) {
        auto r = mod->loadData();
        if (!r) {
            log::warn("Unable to load data for mod \"{}\": {}", mod->getID(), r.unwrapErr());
        }
    }
    return Ok();
}

// Mod loading

Result<Mod*> Loader::Impl::loadModFromInfo(ModInfo const& info) {
    if (m_mods.count(info.id)) {
        return Err(fmt::format("Mod with ID '{}' already loaded", info.id));
    }

    // create Mod instance
    auto mod = new Mod(info);
    m_mods.insert({ info.id, mod });
    mod->m_enabled = InternalMod::get()->getSavedValue<bool>(
        "should-load-" + info.id, true
    );

    // add mod resources
    this->queueInGDThread([this, mod]() {
        auto searchPath = dirs::getModRuntimeDir() / mod->getID() / "resources";

        CCFileUtils::get()->addSearchPath(searchPath.string().c_str());
        this->updateModResources(mod);
    });

    // this loads the mod if its dependencies are resolved
    GEODE_UNWRAP(mod->updateDependencies());

    return Ok(mod);
}

Result<Mod*> Loader::Impl::loadModFromFile(ghc::filesystem::path const& file) {
    auto res = ModInfo::createFromGeodeFile(file);
    if (!res) {
        m_invalidMods.push_back(InvalidGeodeFile {
            .path = file,
            .reason = res.unwrapErr(),
        });
        return Err(res.unwrapErr());
    }
    return this->loadModFromInfo(res.unwrap());
}

bool Loader::Impl::isModInstalled(std::string const& id) const {
    return m_mods.count(id) && !m_mods.at(id)->isUninstalled();
}

Mod* Loader::Impl::getInstalledMod(std::string const& id) const {
    if (m_mods.count(id) && !m_mods.at(id)->isUninstalled()) {
        return m_mods.at(id);
    }
    return nullptr;
}

bool Loader::Impl::isModLoaded(std::string const& id) const {
    return m_mods.count(id) && m_mods.at(id)->isLoaded();
}

Mod* Loader::Impl::getLoadedMod(std::string const& id) const {
    if (m_mods.count(id)) {
        auto mod = m_mods.at(id);
        if (mod->isLoaded()) {
            return mod;
        }
    }
    return nullptr;
}

void Loader::Impl::dispatchScheduledFunctions(Mod* mod) {
    std::lock_guard _(m_scheduledFunctionsMutex);
    for (auto& func : m_scheduledFunctions) {
        func();
    }
    m_scheduledFunctions.clear();
}

void Loader::Impl::scheduleOnModLoad(Mod* mod, ScheduledFunction func) {
    std::lock_guard _(m_scheduledFunctionsMutex);
    if (mod) {
        return func();
    }
    m_scheduledFunctions.push_back(func);
}

void Loader::Impl::updateModResources(Mod* mod) {
    if (!mod->m_info.spritesheets.size()) {
        return;
    }

    auto searchPath = dirs::getModRuntimeDir() / mod->getID() / "resources";

    log::debug("Adding resources for {}", mod->getID());

    // add spritesheets
    for (auto const& sheet : mod->m_info.spritesheets) {
        log::debug("Adding sheet {}", sheet);
        auto png = sheet + ".png";
        auto plist = sheet + ".plist";
        auto ccfu = CCFileUtils::get();

        if (png == std::string(ccfu->fullPathForFilename(png.c_str(), false)) ||
            plist == std::string(ccfu->fullPathForFilename(plist.c_str(), false))) {
            log::warn(
                "The resource dir of \"{}\" is missing \"{}\" png and/or plist files",
                mod->m_info.id, sheet
            );
        }
        else {
            CCTextureCache::get()->addImage(png.c_str(), false);
            CCSpriteFrameCache::get()->addSpriteFramesWithFile(plist.c_str());
        }
    }
}

// Dependencies and refreshing

void Loader::Impl::loadModsFromDirectory(
    ghc::filesystem::path const& dir,
    bool recursive
) {
    log::debug("Searching {}", dir);
    for (auto const& entry : ghc::filesystem::directory_iterator(dir)) {
        // recursively search directories
        if (ghc::filesystem::is_directory(entry) && recursive) {
            this->loadModsFromDirectory(entry.path(), true);
            continue;
        }

        // skip this entry if it's not a file
        if (!ghc::filesystem::is_regular_file(entry)) {
            continue;
        }

        // skip this entry if its extension is not .geode
        if (entry.path().extension() != GEODE_MOD_EXTENSION) {
            continue;
        }
        // skip this entry if it's already loaded
        if (map::contains<std::string, Mod*>(m_mods, [entry](Mod* p) -> bool {
            return p->m_info.path == entry.path();
        })) {
            continue;
        }

        // if mods should be loaded immediately, do that
        if (m_earlyLoadFinished) {
            auto load = this->loadModFromFile(entry);
            if (!load) {
                log::error("Unable to load {}: {}", entry, load.unwrapErr());
            }
        }
        // otherwise collect mods to load first to make sure the correct 
        // versions of the mods are loaded and that early-loaded mods are 
        // loaded early
        else {
            auto res = ModInfo::createFromGeodeFile(entry.path());
            if (!res) {
                m_invalidMods.push_back(InvalidGeodeFile {
                    .path = entry.path(),
                    .reason = res.unwrapErr(),
                });
                continue;
            }
            auto info = res.unwrap();

            // skip this entry if it's already set to be loaded
            if (ranges::contains(m_modsToLoad, info)) {
                continue;
            }

            // add to list of mods to load
            m_modsToLoad.push_back(info);
        }
    }
}

void Loader::Impl::refreshModsList() {
    log::debug("Loading mods...");

    // find mods
    for (auto& dir : m_modSearchDirectories) {
        this->loadModsFromDirectory(dir);
    }
    
    // load early-load mods first
    for (auto& mod : m_modsToLoad) {
        if (mod.needsEarlyLoad) {
            auto load = this->loadModFromInfo(mod);
            if (!load) {
                log::error("Unable to load {}: {}", mod.id, load.unwrapErr());
            }
        }
    }

    // UI can be loaded now
    m_earlyLoadFinished = true;

    // load the rest of the mods
    for (auto& mod : m_modsToLoad) {
        if (!mod.needsEarlyLoad) {
            auto load = this->loadModFromInfo(mod);
            if (!load) {
                log::error("Unable to load {}: {}", mod.id, load.unwrapErr());
            }
        }
    }
    m_modsToLoad.clear();
}

void Loader::Impl::updateAllDependencies() {
    for (auto const& [_, mod] : m_mods) {
        (void)mod->updateDependencies();
    }
}

void Loader::Impl::waitForModsToBeLoaded() {
    auto lock = std::unique_lock(m_earlyLoadFinishedMutex);
    m_earlyLoadFinishedCV.wait(lock, [this] {
        return bool(m_earlyLoadFinished);
    });
}

bool Loader::Impl::didLastLaunchCrash() const {
    return crashlog::didLastLaunchCrash();
}




void Loader::Impl::reset() {
    this->closePlatformConsole();

    for (auto& [_, mod] : m_mods) {
        delete mod;
    }
    m_mods.clear();
    log::Logger::clear();
    ghc::filesystem::remove_all(dirs::getModRuntimeDir());
    ghc::filesystem::remove_all(dirs::getTempDir());
}
bool Loader::Impl::isReadyToHook() const {
    return m_readyToHook;
}

void Loader::Impl::addInternalHook(Hook* hook, Mod* mod) {
    m_internalHooks.push_back({hook, mod});
}

bool Loader::Impl::loadHooks() {
    m_readyToHook = true;
    auto thereWereErrors = false;
    for (auto const& hook : m_internalHooks) {
        auto res = hook.second->addHook(hook.first);
        if (!res) {
            log::internalLog(Severity::Error, hook.second, "{}", res.unwrapErr());
            thereWereErrors = true;
        }
    }
    // free up memory
    m_internalHooks.clear();
    return !thereWereErrors;
}

void Loader::Impl::queueInGDThread(ScheduledFunction func) {
    std::lock_guard<std::mutex> lock(m_gdThreadMutex);
    m_gdThreadQueue.push_back(func);
}

void Loader::Impl::executeGDThreadQueue() {
    // copy queue to avoid locking mutex if someone is
    // running addToGDThread inside their function
    m_gdThreadMutex.lock();
    auto queue = m_gdThreadQueue;
    m_gdThreadQueue.clear();
    m_gdThreadMutex.unlock();

    // call queue
    for (auto const& func : queue) {
        func();
    }
}

void Loader::Impl::logConsoleMessage(std::string const& msg) {
    if (m_platformConsoleOpen) {
        // TODO: make flushing optional
        std::cout << msg << '\n' << std::flush;
    }
}

bool Loader::Impl::platformConsoleOpen() const {
    return m_platformConsoleOpen;
}

void Loader::Impl::downloadLoaderResources() {
    auto version = this->getVersion().toString();
    auto tempResourcesZip = dirs::getTempDir() / "new.zip";
    auto resourcesDir = dirs::getGeodeResourcesDir() / InternalMod::get()->getID();

    web::AsyncWebRequest()
        .join("update-geode-loader-resources")
        .fetch(fmt::format(
            "https://github.com/geode-sdk/geode/releases/download/{}/resources.zip", version
        ))
        .into(tempResourcesZip)
        .then([tempResourcesZip, resourcesDir](auto) {
            // unzip resources zip
            auto unzip = file::Unzip::intoDir(tempResourcesZip, resourcesDir, true);
            if (!unzip) {
                return ResourceDownloadEvent(
                    UpdateFailed("Unable to unzip new resources: " + unzip.unwrapErr())
                ).post();
            }
            ResourceDownloadEvent(UpdateFinished()).post();
        })
        .expect([](std::string const& info) {
            ResourceDownloadEvent(
                UpdateFailed("Unable to download resources: " + info)
            ).post();
        })
        .progress([](auto&, double now, double total) {
            ResourceDownloadEvent(
                UpdateProgress(
                    static_cast<uint8_t>(now / total * 100.0),
                    "Downloading resources"
                )
            ).post();
        });
}

bool Loader::Impl::verifyLoaderResources() {
    static std::optional<bool> CACHED = std::nullopt;
    if (CACHED.has_value()) {
        return CACHED.value();
    }

    // geode/resources/geode.loader
    auto resourcesDir = dirs::getGeodeResourcesDir() / InternalMod::get()->getID();

    // if the resources dir doesn't exist, then it's probably incorrect
    if (!(
        ghc::filesystem::exists(resourcesDir) &&
        ghc::filesystem::is_directory(resourcesDir)
    )) {
        this->downloadLoaderResources();
        return false;
    }

    // make sure every file was covered
    size_t coverage = 0;

    // verify hashes
    for (auto& file : ghc::filesystem::directory_iterator(resourcesDir)) {
        auto name = file.path().filename().string();
        // skip unknown files
        if (!LOADER_RESOURCE_HASHES.count(name)) {
            continue;
        }
        // verify hash
        auto hash = calculateSHA256(file.path());
        if (hash != LOADER_RESOURCE_HASHES.at(name)) {
            log::debug(
                "compare {} {} {}", file.path().string(), hash, LOADER_RESOURCE_HASHES.at(name)
            );
            this->downloadLoaderResources();
            return false;
        }
        coverage += 1;
    }

    // make sure every file was found
    if (coverage != LOADER_RESOURCE_HASHES.size()) {
        this->downloadLoaderResources();
        return false;
    }

    return true;
}

nlohmann::json Loader::Impl::processRawIPC(void* rawHandle, std::string const& buffer) {
    nlohmann::json reply;
    try {
        // parse received message
        auto json = nlohmann::json::parse(buffer);
        if (!json.contains("mod") || !json["mod"].is_string()) {
            log::warn("Received IPC message without 'mod' field");
            return reply;
        }
        if (!json.contains("message") || !json["message"].is_string()) {
            log::warn("Received IPC message without 'message' field");
            return reply;
        }
        nlohmann::json data;
        if (json.contains("data")) {
            data = json["data"];
        }
        // log::debug("Posting IPC event");
        // ! warning: if the event system is ever made asynchronous this will break!
        IPCEvent(rawHandle, json["mod"], json["message"], data, reply).post();
    } catch(...) {
        log::warn("Received IPC message that isn't valid JSON");
    }
    return reply;
}

ResourceDownloadEvent::ResourceDownloadEvent(
    UpdateStatus const& status
) : status(status) {}

ListenerResult ResourceDownloadFilter::handle(
    std::function<Callback> fn,
    ResourceDownloadEvent* event
) {
    fn(event);
    return ListenerResult::Propagate;
}

ResourceDownloadFilter::ResourceDownloadFilter() {}