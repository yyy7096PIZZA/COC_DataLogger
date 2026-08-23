file(GLOB_RECURSE SRC_FILES "*.c" "*.h")
idf_component_register(SRCS ${SRC_FILES} INCLUDE_DIRS "." "include")

# git version info: use -D overrides (CI) or query git (local)
if(NOT DEFINED FW_GIT_TAG)
    execute_process(COMMAND git describe --tags --always --dirty
        WORKING_DIRECTORY ${COMPONENT_DIR}
        OUTPUT_VARIABLE FW_GIT_DESC OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    string(REGEX REPLACE "-g[0-9a-f]+" "" FW_GIT_TAG "${FW_GIT_DESC}")
    execute_process(COMMAND git rev-parse --short=8 HEAD
        WORKING_DIRECTORY ${COMPONENT_DIR}
        OUTPUT_VARIABLE FW_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

    # reconfigure on git HEAD change to keep version fresh
    execute_process(COMMAND git rev-parse --show-toplevel
        WORKING_DIRECTORY ${COMPONENT_DIR}
        OUTPUT_VARIABLE GIT_ROOT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(GIT_ROOT)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${GIT_ROOT}/.git/HEAD)
    endif()
endif()

if(NOT FW_GIT_TAG)
    set(FW_GIT_TAG "unknown")
endif()
if(NOT FW_GIT_HASH)
    set(FW_GIT_HASH "unknown")
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    FW_GIT_TAG="${FW_GIT_TAG}"
    FW_GIT_HASH="${FW_GIT_HASH}"
)

file(GLOB_RECURSE SRC_FILES "*.c" "*.h")
idf_component_register(SRCS ${SRC_FILES} INCLUDE_DIRS "." "include")

# git version info: use -D overrides (CI) or query git (local)
if(NOT DEFINED FW_GIT_TAG)
    execute_process(COMMAND git describe --tags --always --dirty
        WORKING_DIRECTORY ${COMPONENT_DIR}
        OUTPUT_VARIABLE FW_GIT_DESC OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    string(REGEX REPLACE "-g[0-9a-f]+" "" FW_GIT_TAG "${FW_GIT_DESC}")
    execute_process(COMMAND git rev-parse --short=8 HEAD
        WORKING_DIRECTORY ${COMPONENT_DIR}
        OUTPUT_VARIABLE FW_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

    # reconfigure on git HEAD change to keep version fresh
    execute_process(COMMAND git rev-parse --show-toplevel
        WORKING_DIRECTORY ${COMPONENT_DIR}
        OUTPUT_VARIABLE GIT_ROOT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(GIT_ROOT)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${GIT_ROOT}/.git/HEAD)
    endif()
endif()

if(NOT FW_GIT_TAG)
    set(FW_GIT_TAG "unknown")
endif()
if(NOT FW_GIT_HASH)
    set(FW_GIT_HASH "unknown")
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    FW_GIT_TAG="${FW_GIT_TAG}"
    FW_GIT_HASH="${FW_GIT_HASH}"
)
