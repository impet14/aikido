include(CMakeParseArguments)

set(global_prefix "${PROJECT_NAME}_")
set(component_prefix "${PROJECT_NAME}_component_")

define_property(GLOBAL PROPERTY "${global_prefix}INCLUDE_DIRS"
  BRIEF_DOCS "Global include directories used by all components."
  FULL_DOCS "Global include directories used by all components."
)
define_property(GLOBAL PROPERTY "${global_prefix}COMPONENTS"
  BRIEF_DOCS "List all known Aikido components."
  FULL_DOCS "List all known Aikido components."
)

#==============================================================================
function(add_component component)
  set(target "${component_prefix}${component}")
  add_custom_target("${target}")

  install(EXPORT "${component_prefix}${component}"
    FILE "${PROJECT_NAME}_${component}Targets.cmake"
    DESTINATION "${CONFIG_INSTALL_DIR}"
  )

  set_property(TARGET "${target}" PROPERTY "${component_prefix}COMPONENT" TRUE)
  set_property(TARGET "${target}" PROPERTY "${component_prefix}DEPENDENCIES")
  set_property(TARGET "${target}" PROPERTY "${component_prefix}INCLUDE_DIRS")
  set_property(TARGET "${target}" PROPERTY "${component_prefix}LIBRARIES")
  set_property(GLOBAL APPEND
    PROPERTY "${global_prefix}COMPONENTS" "${component}")
endfunction()

#==============================================================================
function(is_component output_variable component)
  set(target "${component_prefix}${component}")

  if(TARGET "${target}")
    get_property(output TARGET "${target}"
      PROPERTY "${component_prefix}COMPONENT")
    set("${output_variable}" ${output} PARENT_SCOPE)
  else()
    set("${output_variable}" FALSE PARENT_SCOPE)
  endif()
endfunction()

#==============================================================================
function(add_component_include_directories component)
  set(target "${component_prefix}${component}")
  set_property(TARGET "${target}" APPEND
    PROPERTY "${component_prefix}INCLUDE_DIRS" ${ARGN})
endfunction()

#==============================================================================
function(add_component_dependencies component)
  set(dependency_components ${ARGN})

  is_component(is_valid "${component}")
  if(NOT ${is_valid})
    message(FATAL_ERROR
      "Target '${component}' is not a component of ${PROJECT_NAME}.")
  endif()

  set(target "${component_prefix}${component}")

  foreach(dependency_component ${dependency_components})
    is_component(is_valid "${dependency_component}")
    if(NOT ${is_valid})
      message(FATAL_ERROR
        "Target '${dependency_component}' is not a component of ${PROJECT_NAME}.")
    endif()

    set(dependency_target "${component_prefix}${dependency_component}")
    add_dependencies("${target}" "${dependency_target}")
  endforeach()

  set_property(TARGET "${target}" APPEND
    PROPERTY "${component_prefix}DEPENDENCIES" ${dependency_components})
endfunction()

#==============================================================================
function(add_component_targets component)
  set(dependency_targets ${ARGN})

  is_component(is_valid "${component}")
  if(NOT ${is_valid})
    message(FATAL_ERROR
      "Target '${component}' is not a component of ${PROJECT_NAME}.")
  endif()

  set(target "${component_prefix}${component}")
  add_dependencies("${target}" ${ARGN})

  foreach(dependency_target ${dependency_targets})
    if(NOT TARGET "${dependency_target}")
      message(FATAL_ERROR "Target '${dependency_target}' does not exist.")
    endif()

    get_property(dependency_type TARGET "${dependency_target}" PROPERTY TYPE)
    if(NOT ("${dependency_type}" STREQUAL STATIC_LIBRARY
         OR "${dependency_type}" STREQUAL SHARED_LIBRARY))
      message(FATAL_ERROR
        "Target '${dependency_target}' has unsupported type"
        " '${dependency_type}'. Only 'STATIC_LIBRARY' and 'SHARED_LIBRARY'"
        " are supported.")
    endif()

    install(TARGETS "${dependency_target}"
      EXPORT "${component_prefix}${component}"
      ARCHIVE DESTINATION "${LIBRARY_INSTALL_DIR}"
      LIBRARY DESTINATION "${LIBRARY_INSTALL_DIR}"
    )
  endforeach()

  set_property(TARGET "${target}" APPEND
    PROPERTY "${component_prefix}LIBRARIES" ${dependency_targets})
endfunction()

#==============================================================================
function(install_component_exports)
  get_property(components GLOBAL PROPERTY "${global_prefix}COMPONENTS")

  set(output_prefix "${CMAKE_CURRENT_BINARY_DIR}/${CONFIG_INSTALL_DIR}")

  foreach(component ${components})
    set(target "${component_prefix}${component}")

    # TODO: Replace this manual generation with a configure_file.
    set(output_path
      "${output_prefix}/${PROJECT_NAME}_${component}Component.cmake")
    file(WRITE "${output_path}" "")

    get_property(dependencies TARGET "${target}"
      PROPERTY "${component_prefix}DEPENDENCIES")
    file(APPEND "${output_path}"
      "set(\"${PROJECT_NAME}_${component}_DEPENDENCIES\" ${dependencies})\n")

    get_property(libraries TARGET "${target}"
      PROPERTY "${component_prefix}LIBRARIES")
    file(APPEND "${output_path}"
      "set(\"${PROJECT_NAME}_${component}_LIBRARIES\" ${libraries})\n")

    install(FILES "${output_path}"
      DESTINATION "${CONFIG_INSTALL_DIR}")
  endforeach()
endfunction()
