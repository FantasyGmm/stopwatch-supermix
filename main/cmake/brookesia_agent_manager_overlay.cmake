function(apply_brookesia_agent_manager_overlay)
    idf_component_get_property(agent_manager_lib
        espressif__brookesia_agent_manager COMPONENT_LIB)
    idf_component_get_property(agent_manager_dir
        espressif__brookesia_agent_manager COMPONENT_DIR)

    set(upstream_manager "${agent_manager_dir}/src/manager.cpp")
    set(incomplete_agent_stop [=[    // Reset the active agent
    active_agent_.reset();]=])
    set(complete_agent_stop [=[    // Stop the active agent service before resetting its shared scheduler.
    if (active_agent_ != nullptr) {
        active_agent_->stop();
    }
    active_agent_.reset();]=])

    file(READ "${upstream_manager}" manager_source)
    string(FIND "${manager_source}" "${incomplete_agent_stop}" stop_position)
    if(stop_position EQUAL -1)
        message(FATAL_ERROR
            "Brookesia agent manager changed; review the active-agent stop overlay")
    endif()
    string(REPLACE "${incomplete_agent_stop}" "${complete_agent_stop}"
        manager_source "${manager_source}")

    set(generated_dir
        "${CMAKE_BINARY_DIR}/generated_overrides/brookesia_agent_manager")
    set(generated_manager "${generated_dir}/manager.cpp")
    file(MAKE_DIRECTORY "${generated_dir}")
    file(WRITE "${generated_manager}" "${manager_source}")

    get_target_property(agent_manager_sources ${agent_manager_lib} SOURCES)
    list(REMOVE_ITEM agent_manager_sources "${upstream_manager}")
    set_property(TARGET ${agent_manager_lib} PROPERTY SOURCES
        "${agent_manager_sources}")
    target_sources(${agent_manager_lib} PRIVATE "${generated_manager}")
endfunction()
