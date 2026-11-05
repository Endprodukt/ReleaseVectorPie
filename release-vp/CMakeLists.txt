cmake_minimum_required(VERSION 3.21)
project(release C CXX)

# The installed VectorPie SDK of this platform (CMakePresets.json sets
# CMAKE_PREFIX_PATH to ~/VectorPie/dist/<pc|pi|linux>).
find_package(VectorPie 0.1 REQUIRED CONFIG)

# main.c includes original.c (the Android original's gameplay); service.c and
# vpbind.c are the cabinet's shared service menu and controls (vpbind.c is a
# copy of the Tempest 2000 port's master, kept the same in every game).
add_executable(release src/main.c src/data.c src/service.c src/vpbind.c)
set_target_properties(release PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
target_link_libraries(release PRIVATE m)
vp_program(release)

# The original's sounds and music, next to the executable (vp_asset_path).
add_custom_command(TARGET release POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:release>/assets)

# The package for the cabinet: cmake --build --preset pi --target vpk
set(STAGE ${CMAKE_BINARY_DIR}/stage/release)
add_custom_target(vpk
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/stage
    COMMAND ${CMAKE_COMMAND} -E make_directory ${STAGE}
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:release> ${STAGE}/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/package/game.ini ${STAGE}/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/package/marquee.png ${STAGE}/
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets ${STAGE}/assets
    COMMAND sh ${VECTORPIE_PACK} ${STAGE} ${CMAKE_BINARY_DIR}
    DEPENDS release
    COMMENT "Making the .vpk")
