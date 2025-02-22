cmake_minimum_required(VERSION 3.0 FATAL_ERROR)
project(redis VERSION 6.0)
#set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../src)
message(CMAKE_RUNTIME_OUTPUT_DIRECTORY is:${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
#if (NOT CMAKE_BUILD_TYPE)
message(STATUS "No build type defined; defaulting to 'Debug'")
set(CMAKE_BUILD_TYPE "Debug" CACHE STRING
        "The type of build. Possible values are: Debug, Release,
RelWithDebInfo and MinSizeRel.")

#endif()
message(STATUS "Host is: ${CMAKE_HOST_SYSTEM}. Build target is:
${CMAKE_SYSTEM}")
get_filename_component(REDIS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" ABSOLUTE)
message(STATUS "Project root directory is: ${REDIS_ROOT}")
# Just for debugging when handling a new platform.
if (false)
    message("C++ compiler supports these language features:")
    foreach (i ${CMAKE_CXX_COMPILE_FEATURES})
        message(" ${i}")
    endforeach ()
endif ()
message(STATUS "Generating release.h...")
execute_process(
        COMMAND sh -c ./mkreleasehdr.sh
        WORKING_DIRECTORY ${REDIS_ROOT}/src/
)
add_subdirectory(deps)
add_subdirectory(src/modules)
set(SRC_SERVER_TMP
        src/crcspeed.c
        src/crcspeed.h
        src/sha256.c
        src/sha256.h
        src/connection.c
        src/connection.h
        src/acl.c
        src/timeout.c
        src/tracking.c
        src/tls.c
        src/adlist.c
        src/ae.c
        src/anet.c
        # windows屏蔽掉下面两个文件，mac系统不需要屏蔽，这两个是mac环境多路复用的库
        # /usr/local/include/event.h
        # src/ae_kqueue.c
        src/mt19937-64.c
        src/mt19937-64.h
        #        src/monotonic.c
        #        src/monotonic.h
        src/dict.c
        src/sds.c
        src/zmalloc.c
        src/lzf_c.c
        src/lzf_d.c
        src/pqsort.c
        src/zipmap.c
        src/sha1.c
        src/ziplist.c
        src/release.c
        src/networking.c
        src/util.c
        src/object.c
        src/db.c
        src/replication.c
        src/rdb.c
        src/t_string.c
        src/t_list.c
        src/t_set.c
        src/t_zset.c
        src/evict.c
        src/defrag.c
        src/module.c
        src/quicklist.c
        src/expire.c
        src/childinfo.c
        src/redis-check-aof.c
        src/redis-check-rdb.c
        src/lazyfree.c
        src/geohash.c
        src/rax.c
        src/geohash_helper.c
        src/siphash.c
        src/geo.c
        src/t_hash.c
        src/config.c
        src/aof.c
        src/pubsub.c
        src/multi.c
        src/debug.c
        src/sort.c
        src/intset.c
        src/syncio.c
        src/cluster.c
        src/crc16.c
        src/endianconv.c
        src/slowlog.c
        src/scripting.c
        src/bio.c
        src/rio.c
        src/rand.c
        src/memtest.c
        src/crc64.c
        src/bitops.c
        src/sentinel.c
        src/notify.c
        src/setproctitle.c
        src/blocked.c
        src/hyperloglog.c
        src/latency.c
        src/sparkline.c
        src/t_stream.c
        src/lolwut.c
        src/lolwut.h
        src/lolwut5.c
        src/lolwut6.c
        src/listpack.c
        src/localtime.c
        src/gopher.c
)
set(SRC_SERVER src/server.c ${SRC_SERVER_TMP}
        src/test.cpp
        src/test.h)
set(SRC_CLI
        src/anet.c
        src/sds.c
        src/adlist.c
        src/redis-cli.c
        src/zmalloc.c
        src/release.c
        src/ae.c
        src/crc64.c
        src/crc16.c
        src/dict.c
        src/siphash.c
)
if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    # better not to work with jemalloc
endif()
set(EXECUTABLE_OUTPUT_PATH src)
add_executable(redis-server ${SRC_SERVER})
add_executable(redis-cli ${SRC_CLI})
set_property(TARGET redis-server PROPERTY C_STANDARD 99)
set_property(TARGET redis-server PROPERTY CXX_STANDARD 11)
set_property(TARGET redis-server PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET redis-cli PROPERTY C_STANDARD 99)
set_property(TARGET redis-cli PROPERTY CXX_STANDARD 11)
set_property(TARGET redis-cli PROPERTY CXX_STANDARD_REQUIRED ON)
target_include_directories(redis-server
        PRIVATE ${REDIS_ROOT}/deps/hiredis
        PRIVATE ${REDIS_ROOT}/deps/linenoise
        PRIVATE ${REDIS_ROOT}/deps/lua/src
)
target_include_directories(redis-cli
        PRIVATE ${REDIS_ROOT}/deps/hiredis
        PRIVATE ${REDIS_ROOT}/deps/linenoise
        PRIVATE ${REDIS_ROOT}/deps/lua/src
)
target_link_libraries(redis-server
        PRIVATE pthread
        PRIVATE m
        PRIVATE lua
        PRIVATE linenoise
        PRIVATE hiredis
)
target_link_libraries(redis-cli
        PRIVATE pthread
        PRIVATE m
        PRIVATE linenoise
        PRIVATE hiredis
)
link_directories(deps/hiredis/ deps/linenoise/ diredeps/lua/src)
install(TARGETS redis-server
        RUNTIME DESTINATION bin
)
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -c")