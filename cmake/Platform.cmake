if (NOT DEFINED GEODE_TARGET_PLATFORM)
	if(APPLE)
		if(IOS)
			set(GEODE_TARGET_PLATFORM "iOS")
		else()
			set(GEODE_TARGET_PLATFORM "MacOS")
		endif()
	elseif(WIN32)
		set(GEODE_TARGET_PLATFORM "Win32")
	elseif(ANDROID)
		set(GEODE_TARGET_PLATFORM "Android")
	else()
		message(FATAL_ERROR "Unable to detect platform, please set GEODE_TARGET_PLATFORM in the root CMake file.")
	endif()
endif()

if (NOT ${PROJECT_NAME} STREQUAL ${CMAKE_PROJECT_NAME})
	set(GEODE_TARGET_PLATFORM GEODE_TARGET_PLATFORM PARENT_SCOPE)
endif()

if (GEODE_TARGET_PLATFORM STREQUAL "iOS")
	set_target_properties(${PROJECT_NAME} PROPERTIES
		SYSTEM_NAME iOS
		OSX_SYSROOT ${GEODE_IOS_SDK}
		OSX_ARCHITECTURES arm64
	)

	set(GEODE_PLATFORM_BINARY "GeodeIOS.dylib")
elseif (GEODE_TARGET_PLATFORM STREQUAL "MacOS")
	set_target_properties(${PROJECT_NAME} PROPERTIES 
		SYSTEM_NAME MacOS
		APPLE_SILICON_PROCESSOR x86_64
	)

	# this should be set globally
	set(CMAKE_OSX_ARCHITECTURES "x86_64")

	# only exists as a global property
	set(CMAKE_OSX_DEPLOYMENT_TARGET 10.14)

	target_link_libraries(${PROJECT_NAME} INTERFACE
		curl "-framework Cocoa"
		${GEODE_LOADER_PATH}/include/link/libfmod.dylib
	)
	target_compile_options(${PROJECT_NAME} INTERFACE -fms-extensions #[[-Wno-deprecated]] -Wno-ignored-attributes -Os #[[-flto]] #[[-fvisibility=internal]])

	set(GEODE_PLATFORM_BINARY "Geode.dylib")

elseif (GEODE_TARGET_PLATFORM STREQUAL "Win32")
	set_target_properties(${PROJECT_NAME} PROPERTIES
		SYSTEM_NAME Win32
		GENERATOR_PLATFORM x86
	)

	target_compile_definitions(${PROJECT_NAME} INTERFACE NOMINMAX)

	target_link_libraries(${PROJECT_NAME} INTERFACE 
		${GEODE_LOADER_PATH}/include/link/libcocos2d.lib
		${GEODE_LOADER_PATH}/include/link/libExtensions.lib
		${GEODE_LOADER_PATH}/include/link/libcurl.lib
		${GEODE_LOADER_PATH}/include/link/glew32.lib
		${GEODE_LOADER_PATH}/include/link/gdstring.lib
		${GEODE_LOADER_PATH}/include/link/fmod.lib
	)

	# Windows links against .lib and not .dll
	set(GEODE_PLATFORM_BINARY "Geode.lib")
elseif (GEODE_TARGET_PLATFORM STREQUAL "Android")
	set_target_properties(${PROJECT_NAME} PROPERTIES
		SYSTEM_NAME Android
	)

	target_link_libraries(${PROJECT_NAME} INTERFACE
		${GEODE_LOADER_PATH}/include/Geode/cocos/libcocos2dcpp.so
		${GEODE_LOADER_PATH}/include/link/libcurl.a
		${GEODE_LOADER_PATH}/include/link/libssl.a
		${GEODE_LOADER_PATH}/include/link/libcrypto.a
		log
	)

	set(GEODE_PLATFORM_BINARY "libgeode.so")
endif()
