# share libraries
add_subdirectory(lib)

if (NOT ANDROID)
add_subdirectory(apps)
endif()
