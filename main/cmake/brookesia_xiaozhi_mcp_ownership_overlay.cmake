function(apply_brookesia_xiaozhi_mcp_ownership_overlay)
    idf_component_get_property(xiaozhi_agent_lib
        espressif__brookesia_agent_xiaozhi COMPONENT_LIB)
    idf_component_get_property(xiaozhi_agent_dir
        espressif__brookesia_agent_xiaozhi COMPONENT_DIR)

    set(upstream_xiaozhi_agent "${xiaozhi_agent_dir}/src/agent_xiaozhi.cpp")
    set(borrowed_mcp_engine [=[    chat_config.mcp_engine = mcp_engine;
    auto ret_init = esp_xiaozhi_chat_init(&chat_config, &chat_handle_);]=])
    set(owned_mcp_engine [=[    chat_config.mcp_engine = mcp_engine;
    // The engine is created for this chat session. Transfer ownership so
    // esp_xiaozhi_chat_deinit() destroys it after stopping MCP runtime users.
    chat_config.owns_mcp_engine = true;
    auto ret_init = esp_xiaozhi_chat_init(&chat_config, &chat_handle_);]=])

    file(READ "${upstream_xiaozhi_agent}" xiaozhi_agent_source)
    string(FIND "${xiaozhi_agent_source}" "${borrowed_mcp_engine}"
        ownership_position)
    if(ownership_position EQUAL -1)
        message(FATAL_ERROR
            "Brookesia XiaoZhi MCP setup changed; review the ownership overlay")
    endif()
    string(REPLACE "${borrowed_mcp_engine}" "${owned_mcp_engine}"
        xiaozhi_agent_source "${xiaozhi_agent_source}")

    set(generated_dir
        "${CMAKE_BINARY_DIR}/generated_overrides/brookesia_xiaozhi_mcp_ownership")
    set(generated_xiaozhi_agent "${generated_dir}/agent_xiaozhi.cpp")
    file(MAKE_DIRECTORY "${generated_dir}")
    file(WRITE "${generated_xiaozhi_agent}" "${xiaozhi_agent_source}")

    get_target_property(xiaozhi_agent_sources ${xiaozhi_agent_lib} SOURCES)
    list(REMOVE_ITEM xiaozhi_agent_sources "${upstream_xiaozhi_agent}")
    set_property(TARGET ${xiaozhi_agent_lib} PROPERTY SOURCES
        "${xiaozhi_agent_sources}")
    target_sources(${xiaozhi_agent_lib} PRIVATE "${generated_xiaozhi_agent}")
endfunction()
