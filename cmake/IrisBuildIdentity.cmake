# A configure-only timestamp needlessly recompiles module/host code and makes
# simulation caches stale. Identity follows actual source/toolchain inputs.
file(GLOB_RECURSE iris_identity_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/plugins/*.cpp")
list(APPEND iris_identity_sources "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/IrisBuildIdentity.cmake")
list(SORT iris_identity_sources)
set(iris_identity "${CMAKE_CXX_COMPILER_ID}|${CMAKE_CXX_COMPILER_VERSION}|${CMAKE_CXX_FLAGS}|${CMAKE_CXX_FLAGS_DEBUG}|${CMAKE_CXX_FLAGS_RELEASE}|${CMAKE_CXX_FLAGS_RELWITHDEBINFO}|${CMAKE_CXX_FLAGS_MINSIZEREL}")
foreach(iris_feature IRIS_ENABLE_ENSCAPE_PLUGIN IRIS_ENABLE_POSTPROCESS_PLUGIN)
    if(DEFINED ${iris_feature})
        string(APPEND iris_identity "|${iris_feature}=${${iris_feature}}")
    else()
        string(APPEND iris_identity "|${iris_feature}=ON")
    endif()
endforeach()
foreach(iris_source IN LISTS iris_identity_sources)
    file(SHA256 "${iris_source}" iris_source_hash)
    file(RELATIVE_PATH iris_relative "${CMAKE_CURRENT_SOURCE_DIR}" "${iris_source}")
    string(APPEND iris_identity "|${iris_relative}:${iris_source_hash}")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${iris_identity_sources})
string(SHA256 iris_identity_hash "${iris_identity}")
string(SUBSTRING "${iris_identity_hash}" 0 16 iris_identity_short)
set(MYRENDERER_BUILD_ID "${PROJECT_VERSION}+${iris_identity_short}-$<CONFIG>")
file(WRITE "${CMAKE_BINARY_DIR}/iris-build-identity.txt" "${PROJECT_VERSION}+${iris_identity_short}\n")
