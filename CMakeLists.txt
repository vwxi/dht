cmake_minimum_required(VERSION 3.1...3.25)

project(
	dht
	VERSION 1.0
	LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")
set(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -g")

set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)
find_package(Boost 1.50 REQUIRED COMPONENTS system thread)

if(NOT TARGET Boost::system)
	add_library(Boost::system IMPORTED INTERFACE)
	set_property(TARGET Boost::system PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${Boost_INCLUDE_DIR})
	set_property(TARGET Boost::system PROPERTY INTERFACE_LINK_LIBRARIES ${Boost_LIBARIES})
endif()

if(NOT TARGET Boost::thread)
	add_library(Boost::thread IMPORTED INTERFACE)
	set_property(TARGET Boost::thread PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${Boost_INCLUDE_DIR})
	set_property(TARGET Boost::thread PROPERTY INTERFACE_LINK_LIBRARIES ${Boost_LIBARIES})
endif()

find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
# Update submodules as needed
    option(GIT_SUBMODULE "Check submodules during build" ON)
    if(GIT_SUBMODULE)
        message(STATUS "Submodule update")
        execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --remote --recursive
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                        RESULT_VARIABLE GIT_SUBMOD_RESULT)
        if(NOT GIT_SUBMOD_RESULT EQUAL "0")
            message(FATAL_ERROR "git submodule update --init --remote --recursive failed with ${GIT_SUBMOD_RESULT}, please checkout submodules")
        endif()
    endif()
endif()

if(NOT EXISTS "${PROJECT_SOURCE_DIR}/extern/spdlog/CMakeLists.txt" OR 
	NOT EXISTS "${PROJECT_SOURCE_DIR}/extern/msgpack-c/CMakeLists.txt" OR 
	NOT EXISTS "${PROJECT_SOURCE_DIR}/extern/miniupnp/miniupnpc/CMakeLists.txt")
    message(FATAL_ERROR "The submodules were not downloaded! GIT_SUBMODULE was turned off or failed. Please update submodules and try again.")
endif()

add_subdirectory(extern/spdlog)
add_subdirectory(extern/msgpack-c)
add_subdirectory(extern/cryptopp)
add_subdirectory(extern/miniupnp/miniupnpc)

add_executable(
	dht
	src/main.cpp
	src/network.cpp
	src/bucket.cpp
	src/routing.cpp
	src/dht.cpp
	src/crypto.cpp
	src/upnp.cpp
)

target_include_directories(
	${PROJECT_NAME} PUBLIC
	"${PROJECT_BINARY_DIR}"
	"${PROJECT_SOURCE_DIR}/include/dht"
	"${PROJECT_SOURCE_DIR}/extern"
)

target_link_libraries(dht PUBLIC Boost::system Boost::thread spdlog::spdlog pthread msgpack-cxx cryptopp miniupnpc)