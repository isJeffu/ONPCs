cmake_minimum_required(VERSION 3.21)

# ONPCs (formerly OStim NPCs Reforged) - the mod's SKSE plugin.
# Draws the mod's settings page in SKSE Menu Framework (F3 menu), keeps the values in
# Data\SKSE\Plugins\ONPCs.ini, exposes them to Papyrus as ONPCR_Settings natives
# and fires the "ONPCR_SettingsChanged" mod event when the player changes something.
# GPL-3.0-or-later (the mod is a fork of OStim NPCs by Aietos).

project(
	ONPCs
	VERSION 0.27.0
	LANGUAGES CXX
)

add_library("${PROJECT_NAME}" SHARED)
target_compile_features("${PROJECT_NAME}" PRIVATE cxx_std_23)

file(GLOB_RECURSE ONPCR_HEADERS LIST_DIRECTORIES false CONFIGURE_DEPENDS "include/*.h")
file(GLOB_RECURSE ONPCR_SOURCES LIST_DIRECTORIES false CONFIGURE_DEPENDS "src/*.cpp")
target_sources("${PROJECT_NAME}" PRIVATE ${ONPCR_HEADERS} ${ONPCR_SOURCES})

configure_file(cmake/Plugin.h.in "${CMAKE_CURRENT_BINARY_DIR}/cmake/Plugin.h" @ONLY)
configure_file(cmake/version.rc.in "${CMAKE_CURRENT_BINARY_DIR}/cmake/version.rc" @ONLY)
target_sources("${PROJECT_NAME}" PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}/cmake/Plugin.h"
	"${CMAKE_CURRENT_BINARY_DIR}/cmake/version.rc")

target_precompile_headers("${PROJECT_NAME}" PRIVATE include/PCH.h)

target_include_directories("${PROJECT_NAME}"
	PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
	PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/cmake")

# CommonLibSSE-NG vendored as a subdirectory (one DLL for SE 1.5.97 through 1.7.x).
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(SKSE_SUPPORT_XBYAK OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/CommonLibSSE-NG)

find_path(SIMPLEINI_INCLUDE_DIRS "SimpleIni.h" REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
target_include_directories("${PROJECT_NAME}" PRIVATE ${SIMPLEINI_INCLUDE_DIRS})

target_link_libraries("${PROJECT_NAME}" PUBLIC CommonLibSSE::CommonLibSSE nlohmann_json::nlohmann_json)

if(MSVC)
	add_compile_definitions(_UNICODE)
	target_compile_options("${PROJECT_NAME}" PRIVATE /MP /W4 /permissive- /utf-8 /Zc:preprocessor /EHsc)
	target_compile_options("${PROJECT_NAME}" PRIVATE
		"$<$<CONFIG:DEBUG>:/ZI;/Od>"
		"$<$<CONFIG:RELEASE>:/Zi;/O2;/Ob2;/Oi;/Ot;/Gy;/Gw>")
	target_link_options("${PROJECT_NAME}" PRIVATE
		"$<$<CONFIG:RELEASE>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>")
endif()

# Copy the built DLL and the default INI into build\SKSE\Plugins (next to this folder).
add_custom_command(TARGET "${PROJECT_NAME}" POST_BUILD
	COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/../build/SKSE/Plugins"
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${CMAKE_CURRENT_SOURCE_DIR}/../build/SKSE/Plugins/"
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/config/ONPCs.ini" "${CMAKE_CURRENT_SOURCE_DIR}/../build/SKSE/Plugins/")
