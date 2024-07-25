#include <Geode/DefaultInclude.hpp>

#include <Geode/loader/Mod.hpp>
#include <loader/ModImpl.hpp>

#include <jni.h>
#include <Geode/cocos/platform/android/jni/JniHelper.h>

using namespace geode::prelude;

template <typename T>
T findSymbolOrMangled(void* so, char const* name, char const* mangled) {
    auto res = reinterpret_cast<T>(dlsym(so, name));
    if (!res) {
        res = reinterpret_cast<T>(dlsym(so, mangled));
    }
    return res;
}

Result<> Mod::Impl::loadPlatformBinary() {
    auto so =
        dlopen((m_tempDirName / m_metadata.getBinaryName()).string().c_str(), RTLD_LAZY);
    if (so) {
        if (m_platformInfo) {
            delete m_platformInfo;
        }
        m_platformInfo = new PlatformInfo{so};

        auto geodeImplicitEntry = findSymbolOrMangled<void(*)()>(so, "geodeImplicitEntry", "_Z17geodeImplicitEntryv");
        if (geodeImplicitEntry) {
            geodeImplicitEntry();
        }

        auto geodeCustomEntry = findSymbolOrMangled<void(*)()>(so, "geodeCustomEntry", "_Z15geodeCustomEntryv");
        if (geodeCustomEntry) {
            geodeCustomEntry();
        }

        return Ok();
    }
    std::string err = dlerror();
    log::error("Unable to load the SO: dlerror returned ({})", err);
    return Err("Unable to load the SO: dlerror returned (" + err + ")");
}

bool JNI_loadInternalBinary(std::string const& libraryName) {
    JniMethodInfo t;
    if (JniHelper::getStaticMethodInfo(
            t, "com/geode/launcher/utils/GeodeUtils", "loadInternalBinary", "(Ljava/lang/String;)V"
        )) {
        jstring stringArg1 = t.env->NewStringUTF(libraryName.c_str());

        t.env->CallStaticVoidMethod(t.classID, t.methodID, stringArg1);

        t.env->DeleteLocalRef(stringArg1);
        t.env->DeleteLocalRef(t.classID);
        return true;
    }
    return false;
}

Result<> Mod::Impl::loadInternalBinary() {
    if (!m_metadata.getInternalBinary()) {
        return Ok();
    }

    auto internalBinary = *m_metadata.getInternalBinary();
    auto internalBinaryFilename = "lib" + internalBinary + ".so";

    JNI_loadInternalBinary(internalBinary);

    auto so = dlopen(internalBinaryFilename.c_str(), RTLD_LAZY);
    if (so) {
        if (m_platformInfo) {
            delete m_platformInfo;
        }
        m_platformInfo = new PlatformInfo{so};

        return Ok();
    }
    std::string err = dlerror();
    log::error("Unable to load the SO: dlerror returned ({})", err);
    return Err("Unable to load the SO: dlerror returned (" + err + ")");
}
