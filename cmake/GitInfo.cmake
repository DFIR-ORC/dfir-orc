find_package(Git REQUIRED)

#
# git_monitor_status: trigger a cmake configuration on commit/index changes
#
function(git_monitor_status)
  set_property(DIRECTORY
    APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/.git/index)
endfunction()


function(git_info)
    set(OPTIONS)
    set(SINGLE
        COMMIT
        LAST_TAG
        LAST_SEMVER_TAG
        HEAD_TAG
        VERSION
        SEMANTIC_VERSION
        BRANCH
    )
    set(MULTI)

    cmake_parse_arguments(_GIT "${OPTIONS}" "${SINGLE}" "${MULTI}" ${ARGN})

    # Current commit
    if (_GIT_COMMIT)
        execute_process(
          COMMAND ${GIT_EXECUTABLE} log -1 --format=%H
          WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
          OUTPUT_VARIABLE GIT_COMMIT
          OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        set(${_GIT_COMMIT} ${GIT_COMMIT} PARENT_SCOPE)
    endif()

    # Most recent/close tag
    if (_GIT_LAST_TAG)
        execute_process(
          COMMAND ${GIT_EXECUTABLE} describe --abbrev=0 --tags HEAD --always
          WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
          OUTPUT_VARIABLE GIT_LAST_TAG
          OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        set(${_GIT_LAST_TAG} ${GIT_LAST_TAG} PARENT_SCOPE)
    endif()

    # Tag string from the most recent/closer Semantic Version tag
    if (_GIT_LAST_SEMVER_TAG)
        execute_process(
          COMMAND
              ${GIT_EXECUTABLE} describe --match "*[0-9].[0-9].[0-9]*" --tags --abbrev=0
          WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
          OUTPUT_VARIABLE GIT_LAST_SEMVER_TAG
          OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        set(${_GIT_LAST_SEMVER_TAG} ${GIT_LAST_SEMVER_TAG} PARENT_SCOPE)
    endif()

    # Tag on HEAD or nothing
    if (_GIT_HEAD_TAG)
        execute_process(
          COMMAND ${GIT_EXECUTABLE} tag --list --points-at=HEAD
          WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
          OUTPUT_VARIABLE GIT_HEAD_TAG
          OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        set(${_GIT_HEAD_TAG} ${GIT_HEAD_TAG} PARENT_SCOPE)
    endif()

    # Most recent/closer tag and commit id
    if (_GIT_VERSION)
        execute_process(
          COMMAND ${GIT_EXECUTABLE} describe --tags --always
          WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
          OUTPUT_VARIABLE GIT_VERSION
          OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        set(${_GIT_VERSION} ${GIT_VERSION} PARENT_SCOPE)
    endif()

    # Version string from the most recent/close Semantic Version tag
    if (_GIT_SEMANTIC_VERSION)
        execute_process(
          COMMAND
              ${GIT_EXECUTABLE} describe --match "*[0-9].[0-9].[0-9]*" --tags
          WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
          OUTPUT_VARIABLE GIT_SEMANTIC_VERSION
          OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        set(${_GIT_SEMANTIC_VERSION} ${GIT_SEMANTIC_VERSION} PARENT_SCOPE)
    endif()

    # Current branch
    if (_GIT_BRANCH)
        execute_process(
          COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
          WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
          OUTPUT_VARIABLE GIT_BRANCH
          OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        set(${_GIT_BRANCH} ${GIT_BRANCH} PARENT_SCOPE)
    endif()

endfunction()
