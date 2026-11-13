# Arcade Foundry Model 3 cores: Supermodel's machine as a library, one game per
# build, behind the afcore C interface (common/include/afcore.h).
#
#   cmake -B build -DM3CORE_ROMSET=vf3 && cmake --build build
#
# Builds afcore_vf3 (a shared library; a framework on Apple platforms with
# M3CORE_FRAMEWORK=ON) and m3run, the command-line runner used for checks.
# Supermodel's own Makefiles (Makefiles/) still build the stock emulator,
# which is the reference the cores are checked against.
cmake_minimum_required(VERSION 3.20)
project(ArcadeFoundryCoresModel3 C CXX)

# The core interface header, shared with the other core repos (submodule
# common/, Arcade-Foundry-Cores-Common, BSD-3-Clause).
set(AFCORE_INCLUDE ${CMAKE_SOURCE_DIR}/common/include)
if(NOT EXISTS ${AFCORE_INCLUDE}/afcore.h)
  message(FATAL_ERROR "common/ is empty: run git submodule update --init")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(M3CORE_ROMSET "vf3" CACHE STRING "The Model 3 ROM set this core is built for")
set(M3CORE_TITLE "" CACHE STRING "Display title (default: from the ROM set)")
option(M3CORE_FRAMEWORK "Build the core as an Apple framework" OFF)
# Without SDL (the default on Apple platforms) the core needs nothing beyond
# the system: threads come from the C++ library and the network board's TCP
# link is left out (link play is not in the Model 3 cores yet). The GL
# context never comes from SDL: CGL on macOS, EGL elsewhere.
if(APPLE)
  set(M3CORE_SDL_DEFAULT OFF)
else()
  set(M3CORE_SDL_DEFAULT ON)
endif()
option(M3CORE_SDL "Use SDL2 and SDL2_net in the core (threads, the network board's link)" ${M3CORE_SDL_DEFAULT})
# Apple only: draw with Metal (Core/src/metal) instead of OpenGL. Apple's
# OpenGL is a layer over Metal that runs Supermodel's quad geometry shader as
# a compute pass before every draw, and iOS and tvOS have no OpenGL at all.
option(M3CORE_METAL "Draw with Metal instead of OpenGL (Apple)" ${APPLE})
if(M3CORE_METAL AND NOT APPLE)
  message(FATAL_ERROR "M3CORE_METAL is for Apple platforms")
endif()
if(M3CORE_METAL AND M3CORE_SDL)
  message(FATAL_ERROR "M3CORE_METAL builds have no SDL")
endif()
if(M3CORE_METAL)
  enable_language(OBJCXX)
endif()

# GAMES:BEGIN titles (Scripts/gen_games.py writes these from games.toml)
set(M3_TITLES_vf3 "Virtua Fighter 3")
set(M3_TITLES_scud "Scud Race")
set(M3_TITLES_scudplus "Scud Race Plus")
set(M3_TITLES_daytona2 "Daytona USA 2")
set(M3_TITLES_dayto2pe "Daytona USA 2 Power Edition")
set(M3_TITLES_srally2 "Sega Rally 2")
set(M3_TITLES_lemans24 "Le Mans 24")
set(M3_TITLES_dirtdvls "Dirt Devils")
set(M3_TITLES_harley "Harley-Davidson and L.A. Riders")
set(M3_TITLES_ecau "Emergency Call Ambulance")
set(M3_TITLES_fvipers2 "Fighting Vipers 2")
set(M3_TITLES_von2 "Virtual-On Oratorio Tangram")
set(M3_TITLES_spikeout "Spikeout")
set(M3_TITLES_spikeofe "Spikeout Final Edition")
set(M3_TITLES_vs299a "Virtua Striker 2 '99")
set(M3_TITLES_swtrilgy "Star Wars Trilogy Arcade")
set(M3_TITLES_lostwsga "The Lost World")
set(M3_TITLES_lamachin "L.A. Machineguns")
set(M3_TITLES_oceanhuna "The Ocean Hunter")
set(M3_TITLES_magtruck "Magical Truck Adventure")
set(M3_TITLES_bassdx "Sega Bass Fishing")
set(M3_TITLES_getbass "Get Bass")
set(M3_TITLES_getbassur "Get Bass")
set(M3_TITLES_getbassdx "Get Bass")
# GAMES:END titles
if(NOT M3CORE_TITLE)
  set(M3CORE_TITLE "${M3_TITLES_${M3CORE_ROMSET}}")
  if(NOT M3CORE_TITLE)
    set(M3CORE_TITLE "${M3CORE_ROMSET}")
  endif()
endif()

if(NOT M3CORE_METAL)
  if(APPLE)
    find_package(OpenGL REQUIRED)
  else()
    find_package(OpenGL REQUIRED COMPONENTS OpenGL GLX EGL)
  endif()
endif()
find_package(ZLIB REQUIRED)
if(M3CORE_SDL)
  find_package(SDL2 REQUIRED)
endif()
if(M3CORE_SDL)
  find_library(SDL2_NET_LIBRARY NAMES SDL2_net)
  find_path(SDL2_NET_INCLUDE_DIR NAMES SDL_net.h PATH_SUFFIXES SDL2)
endif()

# Musashi 68000: the opcode handlers are generated at build time, as
# Supermodel's Makefiles do.
set(MUSASHI_DIR ${CMAKE_SOURCE_DIR}/Src/CPU/68K/Musashi)
set(MUSASHI_GEN ${CMAKE_BINARY_DIR}/musashi)
file(MAKE_DIRECTORY ${MUSASHI_GEN})
# The generator runs on the build machine, so a cross build (iOS, tvOS)
# compiles it here at configure time with the host's compiler, away from
# the target's SDK settings.
if(CMAKE_CROSSCOMPILING)
  set(M68KMAKE ${CMAKE_BINARY_DIR}/host/m68kmake)
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/host)
  if(CMAKE_HOST_APPLE)
    execute_process(COMMAND xcrun --sdk macosx --show-sdk-path
      OUTPUT_VARIABLE HOST_SDK OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    set(HOST_CC xcrun --sdk macosx clang -isysroot ${HOST_SDK}
                -target ${CMAKE_HOST_SYSTEM_PROCESSOR}-apple-macos11)
  else()
    set(HOST_CC cc)
  endif()
  execute_process(COMMAND ${HOST_CC} -O1 -o ${M68KMAKE} ${MUSASHI_DIR}/m68kmake.c
    COMMAND_ERROR_IS_FATAL ANY)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MUSASHI_DIR}/m68kmake.c)
  set(M68KMAKE_DEP ${M68KMAKE})
else()
  add_executable(m68kmake ${MUSASHI_DIR}/m68kmake.c)
  set(M68KMAKE $<TARGET_FILE:m68kmake>)
  set(M68KMAKE_DEP m68kmake)
endif()
set(MUSASHI_OUT ${MUSASHI_GEN}/m68kops.c ${MUSASHI_GEN}/m68kops.h ${MUSASHI_GEN}/m68kopac.c
                ${MUSASHI_GEN}/m68kopdm.c ${MUSASHI_GEN}/m68kopnz.c)
add_custom_command(OUTPUT ${MUSASHI_OUT}
  COMMAND ${M68KMAKE} ${MUSASHI_GEN} ${MUSASHI_DIR}/m68k_in.c
  DEPENDS ${M68KMAKE_DEP} ${MUSASHI_DIR}/m68k_in.c
  COMMENT "Generating the 68000 core")

# Games.xml, built in so a core needs nothing but the ROM set.
set(GAMES_XML_CPP ${CMAKE_BINARY_DIR}/games_xml.cpp)
add_custom_command(OUTPUT ${GAMES_XML_CPP}
  COMMAND ${CMAKE_COMMAND} -DIN=${CMAKE_SOURCE_DIR}/Config/Games.xml -DOUT=${GAMES_XML_CPP}
          -P ${CMAKE_SOURCE_DIR}/Core/cmake/embed_text.cmake
  DEPENDS ${CMAKE_SOURCE_DIR}/Config/Games.xml ${CMAKE_SOURCE_DIR}/Core/cmake/embed_text.cmake
  COMMENT "Building in Games.xml")

set(S ${CMAKE_SOURCE_DIR}/Src)
set(SUPERMODEL_SOURCES
  ${S}/BlockFile.cpp ${S}/GameLoader.cpp ${S}/ROMSet.cpp
  ${S}/CPU/PowerPC/ppc.cpp ${S}/CPU/PowerPC/PPCDisasm.cpp
  ${S}/CPU/68K/68K.cpp ${MUSASHI_DIR}/m68kcpu.c
  ${MUSASHI_GEN}/m68kops.c ${MUSASHI_GEN}/m68kopac.c ${MUSASHI_GEN}/m68kopdm.c ${MUSASHI_GEN}/m68kopnz.c
  ${S}/CPU/Z80/Z80.cpp
  ${S}/Model3/Model3.cpp ${S}/Model3/53C810.cpp ${S}/Model3/93C46.cpp ${S}/Model3/BillBoard.cpp
  ${S}/Model3/Crypto.cpp ${S}/Model3/DSB.cpp ${S}/Model3/IRQ.cpp ${S}/Model3/JTAG.cpp
  ${S}/Model3/MPC10x.cpp ${S}/Model3/NetBoard.cpp ${S}/Model3/NetBoardLink.cpp ${S}/Model3/SimNetBoard.cpp
  ${S}/Model3/PCI.cpp ${S}/Model3/Real3D.cpp ${S}/Model3/RTC72421.cpp ${S}/Model3/SoundBoard.cpp
  ${S}/Model3/TileGen.cpp
  ${S}/Model3/DriveBoard/DriveBoard.cpp ${S}/Model3/DriveBoard/WheelBoard.cpp
  ${S}/Model3/DriveBoard/JoystickBoard.cpp ${S}/Model3/DriveBoard/SkiBoard.cpp ${S}/Model3/DriveBoard/Z80CTC.cpp
  ${S}/Sound/SCSP.cpp ${S}/Sound/SCSPDSP.cpp ${S}/Sound/MPEG/MpegAudio.cpp
  ${S}/Graphics/FBO.cpp ${S}/Graphics/Render2D.cpp ${S}/Graphics/Shader.cpp ${S}/Graphics/SuperAA.cpp
  ${S}/Graphics/New3D/GLSLShader.cpp ${S}/Graphics/New3D/Mat4.cpp ${S}/Graphics/New3D/Model.cpp
  ${S}/Graphics/New3D/New3D.cpp ${S}/Graphics/New3D/PolyHeader.cpp ${S}/Graphics/New3D/R3DFloat.cpp
  ${S}/Graphics/New3D/R3DFrameBuffers.cpp ${S}/Graphics/New3D/R3DGpuGL.cpp ${S}/Graphics/New3D/R3DScrollFog.cpp
  ${S}/Graphics/New3D/R3DShader.cpp ${S}/Graphics/New3D/TextureBank.cpp ${S}/Graphics/New3D/VBO.cpp
  ${S}/Graphics/New3D/Vec.cpp
  ${S}/Inputs/Input.cpp ${S}/Inputs/Inputs.cpp ${S}/Inputs/InputSource.cpp ${S}/Inputs/InputSystem.cpp
  ${S}/Inputs/InputTypes.cpp ${S}/Inputs/MultiInputSource.cpp
  ${S}/OSD/Logger.cpp ${S}/OSD/Outputs.cpp
  ${S}/Util/ByteSwap.cpp ${S}/Util/ConfigBuilders.cpp ${S}/Util/Format.cpp ${S}/Util/NewConfig.cpp
  ${S}/Pkgs/glew.c ${S}/Pkgs/ioapi.c ${S}/Pkgs/unzip.c ${S}/Pkgs/tinyxml2.cpp
)

set(CORE_SOURCES
  ${CMAKE_SOURCE_DIR}/Core/src/m3core.cpp
  ${CMAKE_SOURCE_DIR}/common/src/state.c  # save states' bytes (afcommon/state.h)
  ${GAMES_XML_CPP}
)
if(M3CORE_METAL)
  # Supermodel's OpenGL renderers out, the Metal one in
  list(REMOVE_ITEM SUPERMODEL_SOURCES
    ${S}/Graphics/FBO.cpp ${S}/Graphics/Render2D.cpp ${S}/Graphics/Shader.cpp ${S}/Graphics/SuperAA.cpp
    ${S}/Graphics/New3D/GLSLShader.cpp ${S}/Graphics/New3D/R3DFrameBuffers.cpp ${S}/Graphics/New3D/R3DGpuGL.cpp
    ${S}/Graphics/New3D/R3DScrollFog.cpp ${S}/Graphics/New3D/R3DShader.cpp ${S}/Graphics/New3D/TextureBank.cpp
    ${S}/Graphics/New3D/VBO.cpp ${S}/Pkgs/glew.c)
  list(APPEND CORE_SOURCES ${CMAKE_SOURCE_DIR}/Core/src/metal/metal_renderer.mm)
  set_source_files_properties(${CMAKE_SOURCE_DIR}/Core/src/metal/metal_renderer.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
else()
  list(APPEND CORE_SOURCES ${CMAKE_SOURCE_DIR}/Core/src/gl_context.cpp)
endif()
if(M3CORE_SDL)
  list(APPEND SUPERMODEL_SOURCES ${S}/Network/NetServer.cpp ${S}/Network/TCPConnection.cpp ${S}/OSD/SDL/Thread.cpp)
else()
  # Supermodel's network code (the net board's TCP link) on POSIX sockets in
  # place of SDL_net (Core/src/nosdl/sdlnet_posix.cpp)
  list(APPEND SUPERMODEL_SOURCES ${S}/Network/NetServer.cpp ${S}/Network/TCPConnection.cpp)
  list(APPEND CORE_SOURCES ${CMAKE_SOURCE_DIR}/Core/src/nosdl/thread_std.cpp ${CMAKE_SOURCE_DIR}/Core/src/nosdl/sdlnet_posix.cpp)
endif()

# Every directory a Supermodel source sits in is on the include path, as in
# its Makefiles.
set(SUPERMODEL_INCLUDES ${S} ${S}/OSD/SDL ${S}/Pkgs ${S}/CPU/68K/Musashi ${MUSASHI_GEN}
    ${AFCORE_INCLUDE} ${CMAKE_SOURCE_DIR}/Core/src)
foreach(f ${SUPERMODEL_SOURCES})
  get_filename_component(d ${f} DIRECTORY)
  list(APPEND SUPERMODEL_INCLUDES ${d})
endforeach()
list(REMOVE_DUPLICATES SUPERMODEL_INCLUDES)
if(NOT M3CORE_SDL)
  # Its SDLIncludes.h comes before Supermodel's.
  list(PREPEND SUPERMODEL_INCLUDES ${CMAKE_SOURCE_DIR}/Core/src/nosdl)
endif()

set(CORE_TARGET afcore_${M3CORE_ROMSET})
add_library(${CORE_TARGET} SHARED ${SUPERMODEL_SOURCES} ${CORE_SOURCES})
target_include_directories(${CORE_TARGET} PRIVATE ${SUPERMODEL_INCLUDES})
if(M3CORE_SDL)
  target_include_directories(${CORE_TARGET} PRIVATE ${SDL2_INCLUDE_DIRS} ${SDL2_NET_INCLUDE_DIR})
endif()
target_compile_definitions(${CORE_TARGET} PRIVATE
  GLEW_STATIC AFCORE_BUILD
  M3CORE_ROMSET=${M3CORE_ROMSET} M3CORE_TITLE="${M3CORE_TITLE}"
  $<$<COMPILE_LANGUAGE:C>:INLINE=static\ inline>)
if(APPLE)
  target_compile_definitions(${CORE_TARGET} PRIVATE SUPERMODEL_OSX GL_SILENCE_DEPRECATION)
endif()
if(APPLE AND M3CORE_SDL)
  # Supermodel includes <SDL2_net/SDL_net.h> (its framework layout);
  # Homebrew installs the header as SDL2/SDL_net.h.
  file(WRITE ${CMAKE_BINARY_DIR}/sdl_compat/SDL2_net/SDL_net.h "#include <SDL_net.h>\n")
  target_include_directories(${CORE_TARGET} PRIVATE ${CMAKE_BINARY_DIR}/sdl_compat)
endif()
set_target_properties(${CORE_TARGET} PROPERTIES
  C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
if(M3CORE_FRAMEWORK AND APPLE)
  set_target_properties(${CORE_TARGET} PROPERTIES FRAMEWORK TRUE MACOSX_FRAMEWORK_IDENTIFIER org.arcadefoundry.core.${M3CORE_ROMSET})
  set_target_properties(${CORE_TARGET} PROPERTIES XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER org.arcadefoundry.core.${M3CORE_ROMSET})
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(${CORE_TARGET} PRIVATE -w)
endif()
target_link_libraries(${CORE_TARGET} PRIVATE ZLIB::ZLIB)
if(M3CORE_METAL)
  target_compile_definitions(${CORE_TARGET} PRIVATE M3CORE_METAL SUPERMODEL_NO_OPENGL)
  target_link_libraries(${CORE_TARGET} PRIVATE "-framework Metal" "-framework CoreVideo" "-framework Foundation")
elseif(APPLE)
  target_link_libraries(${CORE_TARGET} PRIVATE OpenGL::GL)
else()
  # The context is EGL's (gl_context.cpp); GLEW still looks the functions up
  # with glXGetProcAddress, whose GLVND entry points serve any current context.
  target_link_libraries(${CORE_TARGET} PRIVATE OpenGL::OpenGL OpenGL::GLX OpenGL::EGL)
endif()
# Only what M3CORE_SDL asks for, whatever an older configure left in the
# cache (a cached SDL2_net linked into an Apple build brought SDL back).
if(M3CORE_SDL)
  target_link_libraries(${CORE_TARGET} PRIVATE ${SDL2_LIBRARIES} ${SDL2_NET_LIBRARY})
endif()
target_compile_definitions(${CORE_TARGET} PRIVATE $<$<BOOL:${M3CORE_SDL}>:M3CORE_SDL>)
if(APPLE AND NOT M3CORE_METAL)
  target_link_libraries(${CORE_TARGET} PRIVATE "-framework Cocoa")
endif()

# m3run: runs a core for a number of frames, with an input script, and writes
# screens and sound for comparison.
add_executable(m3run ${CMAKE_SOURCE_DIR}/Core/tools/m3run.cpp ${CMAKE_SOURCE_DIR}/common/src/state.c)
target_include_directories(m3run PRIVATE ${AFCORE_INCLUDE})
target_compile_definitions(m3run PRIVATE M3CORE_ROMSET=${M3CORE_ROMSET})
target_link_libraries(m3run PRIVATE ${CORE_TARGET} ZLIB::ZLIB)

# m3netlink: a network board with no game, for link tests without ROMs
# (Core/tests/netlink_test.cpp, netlink_ring.sh). m3netlink_sdl is the same on
# SDL_net, as stock Supermodel builds it, so a ring can mix the two.
if(NOT CMAKE_CROSSCOMPILING)
  set(NETLINK_SOURCES ${CMAKE_SOURCE_DIR}/Core/tests/netlink_test.cpp
    ${S}/Model3/SimNetBoard.cpp ${S}/Model3/NetBoardLink.cpp
    ${S}/Network/NetServer.cpp ${S}/Network/TCPConnection.cpp
    ${S}/OSD/Logger.cpp ${S}/Util/NewConfig.cpp ${S}/Util/Format.cpp ${S}/Util/ByteSwap.cpp)
  find_package(Threads)
  add_executable(m3netlink ${NETLINK_SOURCES}
    ${CMAKE_SOURCE_DIR}/Core/src/nosdl/sdlnet_posix.cpp ${CMAKE_SOURCE_DIR}/Core/src/nosdl/thread_std.cpp)
  target_include_directories(m3netlink PRIVATE ${CMAKE_SOURCE_DIR}/Core/src/nosdl ${S} ${S}/OSD/SDL ${S}/Model3)
  target_compile_options(m3netlink PRIVATE -w)
  target_link_libraries(m3netlink PRIVATE Threads::Threads)
  if(M3CORE_SDL AND SDL2_NET_LIBRARY)
    add_executable(m3netlink_sdl ${NETLINK_SOURCES} ${S}/OSD/SDL/Thread.cpp)
    target_include_directories(m3netlink_sdl PRIVATE ${S} ${S}/OSD ${S}/OSD/SDL ${S}/Model3 ${SDL2_INCLUDE_DIRS} ${SDL2_NET_INCLUDE_DIR})
    target_compile_options(m3netlink_sdl PRIVATE -w)
    target_link_libraries(m3netlink_sdl PRIVATE ${SDL2_LIBRARIES} ${SDL2_NET_LIBRARY} Threads::Threads)
  endif()
endif()
