#include <Geode/DefaultInclude.hpp>

#ifdef GEODE_IS_ANDROID

#include <Geode/loader/Mod.hpp>
#include <loader/ModImpl.hpp>

using namespace geode::prelude;

Result<> Mod::Impl::loadPlatformBinary() {
    return Err("Unable to load the so!");
}

Result<> Mod::Impl::unloadPlatformBinary() {
    return Err("Unable to unload the so!");
}

#endif
