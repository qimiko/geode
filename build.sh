NDK="C:\\Users\\Mateus\\Desktop\\coding\\c++\\android-ndk-r25c"
ABI=armeabi-v7a
MINSDKVERSION=28

set -xe

cmake \
    -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=$ABI \
    -DANDROID_PLATFORM=android-$MINSDKVERSION \
    -DCPM_TulipHook_SOURCE="C:/Users/Mateus/Desktop/coding/c++/geode/TulipHook-android" \
    -DCMAKE_BUILD_TYPE=Debug -DGEODE_DEBUG=1 \
    -G Ninja -B build