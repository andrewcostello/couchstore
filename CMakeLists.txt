INCLUDE(CheckFunctionExists)
INCLUDE(CheckIncludeFile)
INCLUDE(CheckIncludeFiles)
INCLUDE(CheckLibraryExists)
INCLUDE(CheckSymbolExists)
INCLUDE(CheckTypeSize)

INCLUDE_DIRECTORIES(BEFORE ${CMAKE_CURRENT_SOURCE_DIR}/include
                           ${CMAKE_CURRENT_SOURCE_DIR}/src
                           ${CMAKE_CURRENT_BINARY_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR}/../platform/include
                           ${CMAKE_INSTALL_PREFIX}/include)
INCLUDE_DIRECTORIES(AFTER ${ICU_INCLUDE_DIR}
                          ${V8_INCLUDE_DIR}
                          ${SNAPPY_INCLUDE_DIR})

CHECK_INCLUDE_FILES("netinet/in.h" HAVE_NETINET_IN_H)
CHECK_INCLUDE_FILES("inttypes.h" HAVE_INTTYPES_H)
CHECK_INCLUDE_FILES("unistd.h" HAVE_UNISTD_H)
CHECK_SYMBOL_EXISTS(fdatasync "unistd.h" HAVE_FDATASYNC)

IF (WIN32)
   SET(V8C_VERSION "V8_POST_3_19_API")
ELSE (WIN32)
   TRY_RUN(V8C_EXITCODE V8C_COMPILED
           ${CMAKE_CURRENT_BINARY_DIR}
           ${CMAKE_CURRENT_SOURCE_DIR}/tests/v8ver.cc
           CMAKE_FLAGS -DLINK_LIBRARIES:STRING=-lv8
           RUN_OUTPUT_VARIABLE V8C_OUTPUT)

   IF (V8C_COMPILED)
      SET(V8C_VERSION "${V8C_OUTPUT}")
   ELSE(V8C_COMPILED)
      SET(V8C_VERSION "V8_PRE_3_19_API")
   ENDIF(V8C_COMPILED)
ENDIF (WIN32)

MESSAGE(STATUS "Using v8 version: [${V8C_VERSION}]")
# Unfortunately it seems like our mac builder have a old
# version from homebrew..
#IF (EXISTS /usr/local/Cellar/v8/)
  # Homebrew provides at least version 3.21.17
#  ADD_DEFINITIONS(-DV8_POST_3_19_API=1)
#ENDIF(EXISTS /usr/local/Cellar/v8/)

IF (WIN32)
  SET(COUCHSTORE_FILE_OPS "src/os_win.c")
ELSE(WIN32)
  SET(COUCHSTORE_FILE_OPS "src/os.c")
ENDIF(WIN32)

SET(COUCHSTORE_SOURCES src/arena.cc src/bitfield.c src/btree_modify.cc
            src/btree_read.cc src/couch_db.cc src/couch_file_read.cc
            src/couch_file_write.cc src/couch_save.cc src/crc32.c
            src/db_compact.cc src/file_merger.cc src/file_name_utils.c
            src/file_sorter.cc src/iobuffer.cc src/llmsort.cc
            src/mergesort.cc src/node_types.cc src/reduces.cc
            src/rfc1321/md5c.c src/strerror.cc src/tree_writer.cc
            src/util.cc src/views/bitmap.c src/views/collate_json.c
            src/views/file_merger.c src/views/file_sorter.c
            src/views/index_header.c src/views/keys.c
            src/views/mapreduce/mapreduce.cc
            src/views/mapreduce/mapreduce_c.cc src/views/reducers.c
            src/views/reductions.c src/views/sorted_list.c
            src/views/spatial.c src/views/util.c src/views/values.c
            src/views/view_group.c src/views/purgers.c ${COUCHSTORE_FILE_OPS})
SET(COUCHSTORE_LIBRARIES ${V8_LIBRARIES} ${ICU_LIBRARIES} ${SNAPPY_LIBRARIES} platform)

ADD_LIBRARY(couchstore SHARED ${COUCHSTORE_SOURCES})
SET_TARGET_PROPERTIES(couchstore PROPERTIES COMPILE_FLAGS "-DLIBCOUCHSTORE_INTERNAL=1 -DLIBMAPREDUCE_INTERNAL=1")
TARGET_LINK_LIBRARIES(couchstore ${COUCHSTORE_LIBRARIES})

ADD_EXECUTABLE(couch_dbdump src/dbdump.c)
TARGET_LINK_LIBRARIES(couch_dbdump couchstore)

ADD_EXECUTABLE(couch_dbinfo src/dbinfo.c)
TARGET_LINK_LIBRARIES(couch_dbinfo couchstore)

ADD_EXECUTABLE(couch_compact src/compactor.c)
TARGET_LINK_LIBRARIES(couch_compact couchstore)

ADD_EXECUTABLE(couch_view_file_sorter src/views/bin/couch_view_file_sorter.c)
TARGET_LINK_LIBRARIES(couch_view_file_sorter couchstore)

ADD_EXECUTABLE(couch_view_file_merger src/views/bin/couch_view_file_merger.c)
TARGET_LINK_LIBRARIES(couch_view_file_merger couchstore)

ADD_EXECUTABLE(couch_view_index_builder src/views/bin/couch_view_index_builder.c)
TARGET_LINK_LIBRARIES(couch_view_index_builder couchstore)

ADD_EXECUTABLE(couch_view_group_cleanup src/views/bin/couch_view_group_cleanup.c)
TARGET_LINK_LIBRARIES(couch_view_group_cleanup couchstore)

ADD_EXECUTABLE(couch_view_index_updater src/views/bin/couch_view_index_updater.c)
TARGET_LINK_LIBRARIES(couch_view_index_updater couchstore)

IF (INSTALL_HEADER_FILES)
INSTALL(FILES
        include/libcouchstore/couch_db.h
        include/libcouchstore/couch_index.h
        include/libcouchstore/couch_common.h
        include/libcouchstore/error.h
        include/libcouchstore/file_ops.h
        include/libcouchstore/visibility.h
        DESTINATION include/libcouchstore)
ENDIF(INSTALL_HEADER_FILES)

INSTALL(TARGETS couchstore
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)

INSTALL(TARGETS couch_dbdump
                couch_dbinfo
                couch_compact
                couch_view_file_sorter
                couch_view_file_merger
                couch_view_index_builder
                couch_view_group_cleanup
                couch_view_index_updater
        RUNTIME DESTINATION bin)

CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/config.cmake.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/config.h)

# Tests:

IF (LUA_FOUND)
ADD_EXECUTABLE(couchscript src/couchscript.cc)
SET_TARGET_PROPERTIES(couchscript PROPERTIES COMPILE_FLAGS -I${LUA_INCLUDE_DIR})
TARGET_LINK_LIBRARIES(couchscript couchstore ${LUA_LIBRARIES})

ADD_TEST(couchstore-localdoc couchscript tests/localdoc.lua)
ADD_TEST(couchstore-corrupt couchscript tests/corrupt.lua)
ADD_TEST(couchstore-bulk couchscript tests/bulk.lua)
ADD_TEST(couchstore-changes-since-filter couchscript tests/changessincefilter.lua)
ADD_TEST(couchstore-compact couchscript tests/compact.lua)
ADD_TEST(couchstore-dropdel couchscript tests/dropdel.lua)
ADD_TEST(couchstore-largefile couchscript tests/largefile.lua)
ADD_TEST(couchstore-large couchscript tests/large.lua)
ENDIF(LUA_FOUND)

MACRO (M_ADD_PYTHON_TEST name pyfile)
  ADD_TEST(${name} python ${pyfile})
  SET_PROPERTY(TEST ${name} PROPERTY ENVIRONMENT
    "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/python"
    "LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}")
ENDMACRO()

M_ADD_PYTHON_TEST(couchstore-purge tests/purge.py)
M_ADD_PYTHON_TEST(couchstore-rewind tests/rewind.py)


ADD_EXECUTABLE(couchstore_testapp tests/testapp.c
	tests/file_merger_tests.c
	tests/file_sorter_tests.c
	tests/mapreduce/tests.c
	tests/mapreduce/map.c
	tests/mapreduce/reduce.c
	tests/mapreduce/builtin.c
	tests/views/tests.c
	tests/views/collate_json_test.c
	tests/views/index_headers.c
	tests/views/bitmaps.c
	tests/views/sorted_lists.c
	tests/views/reductions.c
	tests/views/keys.c
	tests/views/values.c
	tests/views/reducers.c
	tests/views/cleanup.c
	tests/views/spatial.c
	tests/btree_purge/purge_tests.h
	tests/btree_purge/tests.c
	tests/btree_purge/purge.c
        ${COUCHSTORE_SOURCES} )
SET_TARGET_PROPERTIES(couchstore_testapp PROPERTIES COMPILE_FLAGS "-DLIBCOUCHSTORE_NO_VISIBILITY=1")

TARGET_LINK_LIBRARIES(couchstore_testapp ${COUCHSTORE_LIBRARIES})
ADD_TEST(couchstore-testapp couchstore_testapp)
