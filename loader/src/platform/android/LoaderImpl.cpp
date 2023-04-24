#include <Geode/loader/IPC.hpp>
#include <Geode/loader/Log.hpp>
#include <loader/ModImpl.hpp>
#include <iostream>
#include <loader/LoaderImpl.hpp>
#include <Geode/utils/string.hpp>

using namespace geode::prelude;

#ifdef GEODE_IS_ANDROID

#include <android/log.h>

void Loader::Impl::platformMessageBox(char const* title, std::string const& info) {
    // MessageBoxA(nullptr, info.c_str(), title, MB_ICONERROR);
    __android_log_write(ANDROID_LOG_VERBOSE, "Geode Error", (std::string(title) + " - " + info).c_str());
}

void Loader::Impl::logConsoleMessageWithSeverity(std::string const& msg, Severity severity) {
    // if (m_platformConsoleOpen) {
    //     std::cout << msg << "\n" << std::flush;
    // }
    __android_log_print(ANDROID_LOG_VERBOSE, "\x1b[32mGeode\x1b[39m", "\x1b[33m%s\x1b[39m", msg.c_str());
}

void Loader::Impl::openPlatformConsole() {
    return;
}

void Loader::Impl::closePlatformConsole() {
    return;
}

void Loader::Impl::setupIPC() {
}

bool Loader::Impl::userTriedToLoadDLLs() const {
    return false;
}

#endif
