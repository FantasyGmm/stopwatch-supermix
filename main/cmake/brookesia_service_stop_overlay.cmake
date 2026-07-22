function(apply_brookesia_service_stop_overlay)
    idf_component_get_property(service_manager_lib
        espressif__brookesia_service_manager COMPONENT_LIB)
    idf_component_get_property(service_manager_dir
        espressif__brookesia_service_manager COMPONENT_DIR)

    set(upstream_base "${service_manager_dir}/src/service/base.cpp")
    set(original_stop_entry [=[    if (!is_running()) {
        BROOKESIA_LOGD("Already stopped");
        return;
    }

    on_stop();]=])
    set(unlocked_stop_entry [=[    if (!is_running()) {
        BROOKESIA_LOGD("Already stopped");
        return;
    }

    // Reject new calls and let already-blocked call/event tasks observe the
    // stopped state before on_stop() joins service-owned worker threads.
    is_running_.store(false);
    if (state_lock != nullptr && state_lock->owns_lock()) {
        state_lock->unlock();
    }

    on_stop();]=])
    set(original_late_unlock [=[    // Mark the service as stopped now so any pending publish_event::emit_signal_task
    // that is blocked acquiring shared_lock(state_mutex_) will, once we release the
    // unique lock below, see is_running()==false and skip its emit instead of
    // touching torn-down state.
    is_running_.store(false);

    // Release the caller's unique lock on state_mutex_ before the potentially long
    // task_scheduler->stop() call. The scheduler joins its worker threads; if any
    // worker is blocked on shared_lock(state_mutex_) (e.g. a queued emit_signal_task
    // posted from on_stop()), holding the unique lock here would deadlock the join.
    if (state_lock != nullptr && state_lock->owns_lock()) {
        state_lock->unlock();
    }]=])
    set(already_unlocked [=[    // The stopped state is visible and the caller lock is already released,
    // so scheduler cancellation below can drain without a lock inversion.]=])

    file(READ "${upstream_base}" base_source)
    foreach(required_text IN ITEMS "${original_stop_entry}" "${original_late_unlock}")
        string(FIND "${base_source}" "${required_text}" required_position)
        if(required_position EQUAL -1)
            message(FATAL_ERROR
                "Brookesia service stop flow changed; review the lock-order overlay")
        endif()
    endforeach()
    string(REPLACE "${original_stop_entry}" "${unlocked_stop_entry}"
        base_source "${base_source}")
    string(REPLACE "${original_late_unlock}" "${already_unlocked}"
        base_source "${base_source}")

    set(generated_dir
        "${CMAKE_BINARY_DIR}/generated_overrides/brookesia_service_manager")
    set(generated_base "${generated_dir}/base.cpp")
    file(MAKE_DIRECTORY "${generated_dir}")
    file(WRITE "${generated_base}" "${base_source}")

    get_target_property(service_manager_sources ${service_manager_lib} SOURCES)
    list(REMOVE_ITEM service_manager_sources "${upstream_base}")
    set_property(TARGET ${service_manager_lib} PROPERTY SOURCES
        "${service_manager_sources}")
    target_sources(${service_manager_lib} PRIVATE "${generated_base}")
endfunction()
