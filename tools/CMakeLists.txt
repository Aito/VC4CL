if(INCLUDE_COMPILER AND EXISTS "${VC4C_HEADER_PATH}")
	set(VC4CL_BUILD_DUMP_ANALYZER ON)
	add_executable(vc4cl_dump_analyzer "")
	target_link_libraries(vc4cl_dump_analyzer VC4CL ${SYSROOT_LIBRARY_FLAGS})
	target_include_directories(vc4cl_dump_analyzer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
	target_compile_options(vc4cl_dump_analyzer PRIVATE ${VC4CL_ENABLED_WARNINGS})
endif()

if(BUILD_ICD)
  if(VC4CL_BUILD_DUMP_ANALYZER)
    target_compile_definitions(vc4cl_dump_analyzer PRIVATE -Dcl_khr_icd=1 -Duse_cl_khr_icd=1)
  endif()
endif()

include(sources.list)
##
# Installation targets
##
if(VC4CL_BUILD_DUMP_ANALYZER)
  install(TARGETS vc4cl_dump_analyzer EXPORT vc4cl_dump_analyzer-targets RUNTIME DESTINATION bin)
endif()
