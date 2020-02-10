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
        REPO_DIRECTORY
        COMMIT
        TAG
        SEMVER_TAG
        HEAD_TAG
        VERSION
        VERSION_FROM_SEMVER_TAG
        BRANCH
    )
    set(MULTI)

    cmake_parse_arguments(_GIT "${OPTIONS}" "${SINGLE}" "${MULTI}" ${ARGN})

    # Current commit
    if (_GIT_COMMIT)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} log -1 --format=%H
            WORKING_DIRECTORY ${_GIT_REPO_DIRECTORY}
            OUTPUT_VARIABLE GIT_COMMIT
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        set(${_GIT_COMMIT} ${GIT_COMMIT} PARENT_SCOPE)
    endif()

    # Closest tag
    if (_GIT_TAG)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --abbrev=0 --tags HEAD --always
            WORKING_DIRECTORY ${_GIT_REPO_DIRECTORY}
            OUTPUT_VARIABLE GIT_TAG
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        set(${_GIT_TAG} ${GIT_TAG} PARENT_SCOPE)
    endif()

    # Get the closest tag which match a semantic version (ex: 10.0.2)
    if (_GIT_SEMVER_TAG)
        execute_process(
            COMMAND
                ${GIT_EXECUTABLE} describe --match "*[0-9].[0-9].[0-9]*" --tags --abbrev=0
            WORKING_DIRECTORY ${_GIT_REPO_DIRECTORY}
            OUTPUT_VARIABLE GIT_SEMVER_TAG
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        set(${_GIT_SEMVER_TAG} ${GIT_SEMVER_TAG} PARENT_SCOPE)
    endif()

    # Tag on HEAD or nothing
    if (_GIT_HEAD_TAG)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} tag --list --points-at=HEAD
            WORKING_DIRECTORY ${_GIT_REPO_DIRECTORY}
            OUTPUT_VARIABLE GIT_HEAD_TAG
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        set(${_GIT_HEAD_TAG} ${GIT_HEAD_TAG} PARENT_SCOPE)
    endif()

    # Get a version string build on closest tag and commit id
    if (_GIT_VERSION)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --always
            WORKING_DIRECTORY ${_GIT_REPO_DIRECTORY}
            OUTPUT_VARIABLE GIT_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        set(${_GIT_VERSION} ${GIT_VERSION} PARENT_SCOPE)
    endif()

    # Get a version string built from the latest semantic version tag, number of
    # commit since and commit hash (ex: 10.0.2-73-g12af043)
    if (_GIT_VERSION_FROM_SEMVER_TAG)
        execute_process(
            COMMAND
                ${GIT_EXECUTABLE} describe --match "*[0-9].[0-9].[0-9]*" --tags --always
            WORKING_DIRECTORY ${_GIT_REPO_DIRECTORY}
            OUTPUT_VARIABLE GIT_DESCRIBE
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        string(REGEX MATCH
            "([0-9]+)\\.([0-9]+)\\.([0-9]+)-([0-9]+)-g([0-9a-f]+)"
            VERSION_STRING
            "${GIT_DESCRIBE}"
        )

        set(MAJOR 0)
        set(MINOR 0)
        set(PATCH 0)
        set(DISTANCE 0)
        set(COMMIT "")

        if(CMAKE_MATCH_COUNT)
            set(MAJOR ${CMAKE_MATCH_1})
            set(MINOR ${CMAKE_MATCH_2})
            set(PATCH ${CMAKE_MATCH_3})
            set(DISTANCE ${CMAKE_MATCH_4})
            set(COMMIT ${CMAKE_MATCH_5})
        else()
            string(REGEX MATCH
                "([0-9]+)\\.([0-9]+)\\.([0-9]+)"
                VERSION_STRING
                "${GIT_DESCRIBE}"
            )

            if(CMAKE_MATCH_COUNT)
                set(MAJOR ${CMAKE_MATCH_1})
                set(MINOR ${CMAKE_MATCH_2})
                set(PATCH ${CMAKE_MATCH_3})
            endif()
        endif()

        message(STATUS "Found SemVer: ${VERSION_STRING} (${GIT_DESCRIBE})")

        set(${_GIT_VERSION_FROM_SEMVER_TAG} ${GIT_DESCRIBE} PARENT_SCOPE)
        set(${_GIT_VERSION_FROM_SEMVER_TAG}_MAJOR ${MAJOR} PARENT_SCOPE)
        set(${_GIT_VERSION_FROM_SEMVER_TAG}_MINOR ${MINOR} PARENT_SCOPE)
        set(${_GIT_VERSION_FROM_SEMVER_TAG}_PATCH ${PATCH} PARENT_SCOPE)
        set(${_GIT_VERSION_FROM_SEMVER_TAG}_DISTANCE ${DISTANCE} PARENT_SCOPE)
        set(${_GIT_VERSION_FROM_SEMVER_TAG}_COMMIT ${COMMIT} PARENT_SCOPE)
    endif()

    # Current branch
    if (_GIT_BRANCH)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
            WORKING_DIRECTORY ${_GIT_REPO_DIRECTORY}
            OUTPUT_VARIABLE GIT_BRANCH
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        set(${_GIT_BRANCH} ${GIT_BRANCH} PARENT_SCOPE)
    endif()

endfunction()
