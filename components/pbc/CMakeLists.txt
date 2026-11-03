# PBC-gAPI — property and copyright authority: Pablo B. C.
# ESP-IDF component: no desktop window, CUDA or std::thread dependency.
# loadObjFile uses the caller's mounted ESP-IDF VFS; loadObj parses in-memory text.
set(PBC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
idf_component_register(SRCS "${PBC_ROOT}/src/framebuffer.cpp" "${PBC_ROOT}/src/pipeline.cpp"
  "${PBC_ROOT}/src/cpu_renderer.cpp" "${PBC_ROOT}/src/obj.cpp"
  INCLUDE_DIRS "${PBC_ROOT}/include" REQUIRES freertos soc vfs)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
