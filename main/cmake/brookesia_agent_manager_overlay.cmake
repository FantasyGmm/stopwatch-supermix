function(apply_brookesia_agent_manager_overlay)
    idf_component_get_property(agent_manager_lib
        espressif__brookesia_agent_manager COMPONENT_LIB)
    idf_component_get_property(agent_manager_dir
        espressif__brookesia_agent_manager COMPONENT_DIR)

    set(upstream_manager "${agent_manager_dir}/src/manager.cpp")
    set(upstream_state_machine "${agent_manager_dir}/src/state_machine.cpp")
    set(incomplete_agent_stop [=[    // Reset the active agent
    active_agent_.reset();]=])
    set(complete_agent_stop [=[    // Stop the active agent service before resetting its shared scheduler.
    if (active_agent_ != nullptr) {
        active_agent_->stop();
    }
    active_agent_.reset();]=])
    set(reallocating_action_queue_clear [=[    general_action_queue_ = std::queue<GeneralAction>();]=])
    set(stable_action_queue_clear [=[    while (!general_action_queue_.empty()) {
        general_action_queue_.pop();
    }]=])

    file(READ "${upstream_manager}" manager_source)
    string(FIND "${manager_source}" "${incomplete_agent_stop}" stop_position)
    if(stop_position EQUAL -1)
        message(FATAL_ERROR
            "Brookesia agent manager changed; review the active-agent stop overlay")
    endif()
    string(REPLACE "${incomplete_agent_stop}" "${complete_agent_stop}"
        manager_source "${manager_source}")

    file(READ "${upstream_state_machine}" state_machine_source)
    string(FIND "${state_machine_source}" "${reallocating_action_queue_clear}"
        action_queue_position)
    if(action_queue_position EQUAL -1)
        message(FATAL_ERROR
            "Brookesia agent state machine changed; review the action-queue overlay")
    endif()
    string(REPLACE "${reallocating_action_queue_clear}" "${stable_action_queue_clear}"
        state_machine_source "${state_machine_source}")

    set(generated_dir
        "${CMAKE_BINARY_DIR}/generated_overrides/brookesia_agent_manager")
    set(generated_manager "${generated_dir}/manager.cpp")
    set(generated_state_machine "${generated_dir}/state_machine.cpp")
    file(MAKE_DIRECTORY "${generated_dir}")
    file(WRITE "${generated_manager}" "${manager_source}")
    file(WRITE "${generated_state_machine}" "${state_machine_source}")

    get_target_property(agent_manager_sources ${agent_manager_lib} SOURCES)
    list(REMOVE_ITEM agent_manager_sources "${upstream_manager}")
    list(REMOVE_ITEM agent_manager_sources "${upstream_state_machine}")
    set_property(TARGET ${agent_manager_lib} PROPERTY SOURCES
        "${agent_manager_sources}")
    target_sources(${agent_manager_lib} PRIVATE
        "${generated_manager}" "${generated_state_machine}")
endfunction()
