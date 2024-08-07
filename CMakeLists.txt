cmake_minimum_required(VERSION 3.24)

enable_testing()

project(comp_proj)

function(set_proj_trg_props trg)
	cmake_parse_arguments(ARGS "COPY_DLLS" "PCH_NAME" "" ${ARGN})

	target_include_directories(${trg} PUBLIC ${PROJECT_SOURCE_DIR})

	target_precompile_headers(${trg} PRIVATE ${ARGS_PCH_NAME})

	set_target_properties(${trg} PROPERTIES
		COMPILE_WARNING_AS_ERROR ON
		C_STANDARD 11
		C_EXTENSIONS OFF
	)

	target_compile_definitions(${trg} PRIVATE UNICODE=1 _UNICODE=1)
	
	if (MSVC)
		target_compile_options(${trg} PRIVATE /W3)
	endif()

	if (ARGS_COPY_DLLS)
		set(runtime_dlls )
		set(has_runtime_dlls $<BOOL:${runtime_dlls}>)

		add_custom_command(TARGET ${trg} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:${trg}> ${CMAKE_CURRENT_BINARY_DIR}
			COMMAND_EXPAND_LISTS
		)
	endif()
endfunction()

function(add_proj_test_exe test_prefix exe_name)
	set(main_file ${exe_name}.c)
	set(exe_name ${test_prefix}_${exe_name})

	cmake_parse_arguments(ARGS "" "" "LIBRARIES;DEPENDENCIES" ${ARGN})

	add_executable(${exe_name} ${main_file})

	set_proj_trg_props(${exe_name})

	target_link_libraries(${exe_name} ${ARGS_LIBRARIES})

	add_dependencies(${exe_name} ${ARGS_DEPENDENCIES})

	add_test(NAME ${exe_name}_test COMMAND ${exe_name})
endfunction()

add_subdirectory(cmpl_lib)
add_subdirectory(main_dev)
add_subdirectory(ul_lib)
add_subdirectory(wa_lib)
