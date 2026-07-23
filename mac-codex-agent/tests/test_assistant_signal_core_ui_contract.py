import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class AssistantSignalCoreUiContractTests(unittest.TestCase):
    def cpp_function_body(self, source, signature):
        signature_start = source.index(signature)
        body_start = source.index("{", signature_start)
        depth = 0
        for index in range(body_start, len(source)):
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
                if depth == 0:
                    return source[body_start + 1 : index]
        self.fail(f"unterminated function: {signature}")

    def state_case_body(self, function_body, state):
        case = re.search(
            rf"case AssistantVisualState::{state}:(?P<body>.*?)"
            r"(?=\n\s*case AssistantVisualState::|\Z)",
            function_body,
            re.S,
        )
        self.assertIsNotNone(case, state)
        return case.group("body")

    def wave_frame(self, source, name):
        frame = re.search(
            rf"constexpr WaveFrame {name}\{{(?P<values>[^}}]+)\}};",
            source,
        )
        self.assertIsNotNone(frame, name)
        return tuple(int(value) for value in frame.group("values").split(","))

    def wave_frames(self, source, name):
        table = re.search(
            rf"constexpr std::array<WaveFrame, 4> {name}\{{\{{"
            r"(?P<rows>.*?)\}\};",
            source,
            re.S,
        )
        self.assertIsNotNone(table, name)
        return tuple(
            tuple(int(value) for value in row.split(","))
            for row in re.findall(r"\{([^{}]+)\}", table.group("rows"))
        )

    def callback_guard_body(self, runtime, event_name):
        callback = re.search(
            rf"ManagerHelper::EventId::{event_name},\s*"
            r"\[this, session_id\]\(const std::string& source, bool \w+\) \{"
            r"(?P<body>.*?)\n        \}\);",
            runtime,
            re.S,
        )
        self.assertIsNotNone(callback)
        guard = re.search(
            rf"if \(callbackIsCurrentXiaozhi\(\s*session_id,\s*source,\s*"
            rf"manager_event_name\(ManagerHelper::EventId::{event_name}\)\)\) \{{"
            r"(?P<body>.*?)\n            \}",
            callback.group("body"),
            re.S,
        )
        self.assertIsNotNone(guard)
        return guard.group("body")

    def test_runtime_forwards_real_listening_and_speaking_status(self):
        mailbox = (ROOT / "main/runtime/assistant/assistant_event_mailbox.h").read_text()
        runtime = (ROOT / "main/runtime/assistant/assistant_runtime.cpp").read_text()

        self.assertIn("ListeningStatus", mailbox)
        self.assertIn("SpeakingStatus", mailbox)
        listening_guard = self.callback_guard_body(runtime, "ListeningStatusChanged")
        speaking_guard = self.callback_guard_body(runtime, "SpeakingStatusChanged")
        self.assertRegex(
            listening_guard,
            r"mailbox\.publish\(\{session_id,\s*"
            r"AssistantEventKind::ListeningStatus,\s*\{\},\s*listening\}\)",
        )
        self.assertRegex(
            speaking_guard,
            r"mailbox\.publish\(\{session_id,\s*"
            r"AssistantEventKind::SpeakingStatus,\s*\{\},\s*speaking\}\)",
        )

    def test_runtime_forwards_awake_as_wake_detected(self):
        mailbox = (ROOT / "main/runtime/assistant/assistant_event_mailbox.h").read_text()
        runtime = (ROOT / "main/runtime/assistant/assistant_runtime.cpp").read_text()

        self.assertIn("WakeDetected", mailbox)
        awake_branch = re.search(
            r"event == event_name\(ManagerHelper::GeneralEvent::Awake\)\) \{"
            r"(?P<body>.*?)\n\s*\}",
            runtime,
            re.S,
        )
        self.assertIsNotNone(awake_branch)
        self.assertRegex(
            awake_branch.group("body"),
            r"if \(!unexpected\) \{\s*"
            r"mailbox\.publish\(\{session_id,\s*AssistantEventKind::WakeDetected,\s*"
            r"\{\},\s*false\}\);",
        )

    def test_close_suppression_includes_listening_and_speaking_status(self):
        app = (ROOT / "main/apps/app_assistant/app_assistant.cpp").read_text()
        conversation_events = re.search(
            r"bool isConversationEvent\(AssistantEventKind kind\)\s*\{(?P<body>.*?)\n\}",
            app,
            re.S,
        )

        self.assertIsNotNone(conversation_events)
        self.assertIn("AssistantEventKind::ListeningStatus", conversation_events.group("body"))
        self.assertIn("AssistantEventKind::SpeakingStatus", conversation_events.group("body"))
        self.assertIn("AssistantEventKind::WakeDetected", conversation_events.group("body"))

    def test_view_uses_exact_fixed_signal_core_geometry_and_frames(self):
        header = (ROOT / "main/apps/app_assistant/view/assistant_view.h").read_text()
        source = (ROOT / "main/apps/app_assistant/view/assistant_view.cpp").read_text()
        combined = header + source
        init = self.cpp_function_body(source, "void AssistantView::init")

        expected_geometry = {
            "panel_size": 466,
            "safe_text_width": 370,
            "status_y": 34,
            "status_line_height": 22,
            "phase_y": 68,
            "wave_y": 88,
            "wave_height": 132,
            "title_y": 228,
            "divider_y": 276,
            "content_y": 294,
            "content_height": 60,
            "hint_bottom": -74,
            "battery_y": 419,
            "network_dot_x": 102,
            "network_label_x": 116,
            "wave_bar_width": 8,
            "wave_total_width": 222,
        }
        constants = {
            name: int(value)
            for name, value in re.findall(r"constexpr int (\w+) = (-?\d+);", source)
        }
        self.assertEqual(
            {name: constants.get(name) for name in expected_geometry},
            expected_geometry,
        )

        self.assertIn("std::array<lv_obj_t*, 15>", header)
        wave_create = init.index("_wave_container = lv_obj_create(_panel->get());")
        wave_size = init.index(
            "lv_obj_set_size(_wave_container, panel_size, wave_height);"
        )
        wave_position = init.index("lv_obj_set_pos(_wave_container, 0, wave_y);")
        self.assertLess(wave_create, wave_size)
        self.assertLess(wave_size, wave_position)
        self.assertIn("lv_obj_set_width(_wave_bars[index], wave_bar_width);", init)
        self.assertIn("_phase_label->align(LV_ALIGN_TOP_MID, 0, phase_y);", init)
        self.assertIn("_title_label->align(LV_ALIGN_TOP_MID, 0, title_y);", init)
        self.assertIn("lv_obj_set_pos(_divider, 60, divider_y);", init)
        self.assertIn(
            "lv_obj_set_size(_content_label->get(), safe_text_width, content_height);",
            init,
        )
        self.assertIn("_content_label->align(LV_ALIGN_TOP_MID, 0, content_y);", init)
        self.assertIn("_hint_label->align(LV_ALIGN_BOTTOM_MID, 0, hint_bottom);", init)
        self.assertRegex(
            init,
            r"static_cast<int>\(index\)\s*\*\s*"
            r"\(wave_total_width - wave_bar_width\)\s*/\s*"
            r"static_cast<int>\(_wave_bars\.size\(\) - 1\)",
        )
        self.assertIn("lv_obj_set_pos(_network_dot, network_dot_x, status_y + 7);", init)
        self.assertIn(
            "_network_label->align(LV_ALIGN_TOP_LEFT, network_label_x, status_y);",
            init,
        )

        self.assertEqual(
            self.wave_frame(source, "inactive_wave"),
            (16, 18, 16, 24, 18, 32, 22, 44, 22, 32, 18, 24, 16, 18, 16),
        )
        self.assertEqual(
            self.wave_frames(source, "active_wave"),
            (
                (18, 28, 42, 58, 76, 66, 88, 104, 88, 66, 76, 58, 42, 28, 18),
                (28, 46, 68, 44, 82, 58, 96, 72, 102, 64, 86, 48, 70, 36, 24),
                (44, 30, 76, 52, 94, 62, 84, 108, 78, 98, 56, 88, 46, 64, 34),
                (22, 40, 58, 80, 50, 92, 68, 100, 74, 88, 54, 76, 48, 34, 20),
            ),
        )
        self.assertEqual(
            self.wave_frames(source, "thinking_wave"),
            (
                (8, 10, 13, 17, 22, 28, 35, 44, 35, 28, 22, 17, 13, 10, 8),
                (10, 13, 17, 22, 28, 35, 44, 55, 44, 35, 28, 22, 17, 13, 10),
                (13, 17, 22, 28, 35, 44, 54, 66, 54, 44, 35, 28, 22, 17, 13),
                (10, 13, 17, 22, 28, 35, 44, 55, 44, 35, 28, 22, 17, 13, 10),
            ),
        )

        self.assertNotIn("实时字幕", combined)
        self.assertNotRegex(combined, r"lv_timer_create|xTaskCreate|TaskHandle_t|FFT|gif")
        self.assertNotRegex(combined, r"LV_ALIGN_BOTTOM_MID,\s*0,\s*-[0-3][0-9]\b")

    def test_animation_intervals_and_update_label_are_bounded(self):
        source = (ROOT / "main/apps/app_assistant/view/assistant_view.cpp").read_text()
        update = self.cpp_function_body(source, "void AssistantView::update")
        init = self.cpp_function_body(source, "void AssistantView::init")

        self.assertRegex(
            update,
            r"case AssistantVisualState::Listening:\s*"
            r"interval_ms = 60;\s*break;\s*"
            r"case AssistantVisualState::Speaking:\s*interval_ms = 120;",
        )
        self.assertRegex(
            update,
            r"case AssistantVisualState::Connecting:\s*"
            r"case AssistantVisualState::Activating:\s*"
            r"case AssistantVisualState::Thinking:\s*"
            r"case AssistantVisualState::Closing:\s*interval_ms = 170;",
        )
        self.assertRegex(
            update,
            r"case AssistantVisualState::WaitingWake:\s*"
            r"case AssistantVisualState::Failed:\s*"
            r"case AssistantVisualState::StopTimedOut:\s*return;",
        )
        self.assertIn("_update_label->setSize(160, status_line_height);", init)
        self.assertIn(
            "lv_label_set_long_mode(_update_label->get(), LV_LABEL_LONG_MODE_DOTS);",
            init,
        )
        self.assertIn("_update_label->align(LV_ALIGN_TOP_RIGHT, -112, status_y);", init)

    def test_battery_matches_codex_bottom_label_style(self):
        header = (ROOT / "main/apps/app_assistant/view/assistant_view.h").read_text()
        source = (ROOT / "main/apps/app_assistant/view/assistant_view.cpp").read_text()
        init = self.cpp_function_body(source, "void AssistantView::init")
        setter = self.cpp_function_body(source, "void AssistantView::setBattery")

        self.assertIn("constexpr int battery_y = 419;", source)
        self.assertIn("_battery_label = lv_label_create(_panel->get());", init)
        self.assertIn(
            "lv_obj_set_style_text_font(_battery_label, &lv_font_montserrat_14, 0);",
            init,
        )
        self.assertIn("lv_obj_set_width(_battery_label, 150);", init)
        self.assertIn(
            "lv_obj_set_style_text_align(_battery_label, LV_TEXT_ALIGN_CENTER, 0);",
            init,
        )
        self.assertIn(
            "lv_obj_align(_battery_label, LV_ALIGN_TOP_MID, 0, battery_y);",
            init,
        )
        self.assertNotIn("_battery_body", header + source)
        self.assertNotIn("_battery_tip", header + source)
        self.assertIn("LV_SYMBOL_CHARGE", setter)
        self.assertIn("battery_symbol(clamped)", setter)
        self.assertIn("0x3CB371", source)
        self.assertIn("0xE74C3C", source)

    def test_fixed_titles_use_montserrat_ascii_fallback(self):
        source = (ROOT / "main/apps/app_assistant/view/assistant_view.cpp").read_text()
        init = self.cpp_function_body(source, "void AssistantView::init")
        copy = self.cpp_function_body(source, "void AssistantView::applyDefaultCopy")
        expected_titles = {
            "Connecting": "CONNECTING",
            "Activating": "XIAOZHI",
            "WaitingWake": "HELLO XIAOZHI",
            "Listening": "LISTENING",
            "Thinking": "THINKING",
            "Speaking": "XIAOZHI",
            "Failed": "UNAVAILABLE",
            "Closing": "CLOSING",
            "StopTimedOut": "PROTECTED",
        }

        self.assertIn("_title_label->setTextFont(&lv_font_montserrat_24);", init)
        self.assertIn("_phase_label->setTextFont(&font_puhui_14_1);", init)
        self.assertIn("_content_label->setTextFont(&font_puhui_14_1);", init)
        fixed_titles = [
            title for title in re.findall(r'title = "([^"]*)";', copy) if title
        ]
        self.assertEqual(len(fixed_titles), len(expected_titles))
        self.assertTrue(all(title.isascii() for title in fixed_titles))
        for state, title in expected_titles.items():
            self.assertTrue(title.isascii())
            self.assertRegex(
                self.state_case_body(copy, state),
                rf'title = "{re.escape(title)}";',
            )

    def test_waiting_state_copy_and_inactive_wave_are_case_local(self):
        source = (ROOT / "main/apps/app_assistant/view/assistant_view.cpp").read_text()
        copy = self.cpp_function_body(source, "void AssistantView::applyDefaultCopy")
        wave = self.cpp_function_body(source, "void AssistantView::applyWaveFrame")
        waiting = self.state_case_body(copy, "WaitingWake")

        self.assertIn('phase = "等待唤醒";', waiting)
        self.assertIn('title = "HELLO XIAOZHI";', waiting)
        self.assertIn('content = "说“你好小智”开始对话";', waiting)
        self.assertIn("wave_color = inactive_wave_color;", waiting)
        self.assertRegex(wave, r"const WaveFrame\* frame = &inactive_wave;")
        self.assertRegex(
            wave,
            r"case AssistantVisualState::WaitingWake:\s*"
            r"case AssistantVisualState::Failed:\s*"
            r"case AssistantVisualState::StopTimedOut:\s*break;",
        )

    def test_destroy_clears_children_and_raw_handles_before_panel(self):
        source = (ROOT / "main/apps/app_assistant/view/assistant_view.cpp").read_text()
        init = self.cpp_function_body(source, "void AssistantView::init")
        destroy = self.cpp_function_body(source, "void AssistantView::destroy")
        panel_reset = destroy.index("_panel.reset();")

        self.assertLess(init.index("destroy();"), init.index("_panel ="))
        for child in (
            "_hint_label",
            "_content_label",
            "_title_label",
            "_phase_label",
            "_update_label",
            "_network_label",
        ):
            self.assertLess(destroy.index(f"{child}.reset();"), panel_reset)
        for raw_handle in (
            "_wave_container",
            "_divider",
            "_network_dot",
            "_battery_label",
        ):
            self.assertLess(destroy.index(f"{raw_handle} = nullptr;"), panel_reset)
        self.assertLess(destroy.index("_wave_bars.fill(nullptr);"), panel_reset)

    def test_app_routes_running_to_waiting_and_status_events_exhaustively(self):
        app = (ROOT / "main/apps/app_assistant/app_assistant.cpp").read_text()
        self.assertIn("AssistantPresentationSignal::Running", app)
        self.assertIn("AssistantEventKind::ListeningStatus", app)
        self.assertIn("event.active", app)
        self.assertIn("AssistantEventKind::SpeakingStatus", app)
        self.assertNotRegex(
            app,
            r'event\.text == "Running"[\s\S]{0,180}AssistantVisualState::Listening',
        )

    def test_app_samples_battery_outside_the_lvgl_lock(self):
        app = (ROOT / "main/apps/app_assistant/app_assistant.cpp").read_text()
        function = app[app.index("void AppAssistant::updateTelemetry") :]
        sample = function.index("GetHAL().getBatteryLevel()")
        lock = function.index("LvglLockGuard")
        self.assertLess(sample, lock)
        self.assertIn("_view->setBattery", function[lock:])

    def test_listening_wave_uses_existing_pcm_input_outside_lvgl_lock(self):
        app = (ROOT / "main/apps/app_assistant/app_assistant.cpp").read_text()
        view = (ROOT / "main/apps/app_assistant/view/assistant_view.cpp").read_text()
        backend = (ROOT / "main/hal/audio/stopwatch_audio_backend.cpp").read_text()
        running = self.cpp_function_body(app, "void AppAssistant::onRunning")
        update = self.cpp_function_body(view, "void AssistantView::update")
        read = self.cpp_function_body(backend, "std::size_t StopWatchAudioBackend::read")

        sample = running.index("GetStopWatchAudioBackend().inputLevels()")
        lock = running.index("LvglLockGuard")
        self.assertLess(sample, lock)
        self.assertIn("_view->setInputLevels(input_levels);", running[lock:])
        self.assertIn(
            "applyWaveHeights(_input_waveform.update(_input_levels));",
            update,
        )
        self.assertIn("MeasurePcmSparseLevel", read)
        self.assertIn("_impl->publishInputLevel(level)", read)
        self.assertNotIn("MeasurePcmEnvelopeLevels", read)
        self.assertNotIn("std::pow", view)

    def test_presentation_bridge_renders_only_real_state_transitions(self):
        app = (ROOT / "main/apps/app_assistant/app_assistant.cpp").read_text()
        bridge = self.cpp_function_body(
            app, "void AppAssistant::applyPresentationSignal"
        )

        previous = "const AssistantVisualState previous_state = _presentation.state();"
        self.assertIn(previous, bridge)
        self.assertLess(bridge.index(previous), bridge.index("_presentation.apply(signal, active);"))
        self.assertRegex(
            bridge,
            r"if \(previous_state != _presentation\.state\(\)\) \{\s*"
            r"_view->setState\(_presentation\.state\(\)\);\s*\}",
        )
        self.assertLess(
            bridge.index("_view->setState(_presentation.state());"),
            bridge.index("if (signal == AssistantPresentationSignal::Connecting)"),
        )

    def test_ignored_status_events_preserve_conversation_text(self):
        app = (ROOT / "main/apps/app_assistant/app_assistant.cpp").read_text()
        event_handler = self.cpp_function_body(
            app, "void AppAssistant::applyRuntimeEvent"
        )
        listening = re.search(
            r"case AssistantEventKind::ListeningStatus:(?P<body>.*?)"
            r"(?=\n\s*case AssistantEventKind::|\Z)",
            event_handler,
            re.S,
        )
        speaking = re.search(
            r"case AssistantEventKind::SpeakingStatus:(?P<body>.*?)"
            r"(?=\n\s*case AssistantEventKind::|\Z)",
            event_handler,
            re.S,
        )

        self.assertIsNotNone(listening)
        self.assertIsNotNone(speaking)
        self.assertNotIn("setConversationText", listening.group("body"))
        self.assertNotIn("setConversationText", speaking.group("body"))
        self.assertRegex(
            speaking.group("body"),
            r"if \(!event\.active &&\s*"
            r"previous_state == AssistantVisualState::Speaking &&\s*"
            r"_presentation\.state\(\) == AssistantVisualState::WaitingWake\) \{\s*"
            r"_has_conversation_text = false;\s*\}",
        )

    def test_signal_core_adds_no_task_timer_asset_or_full_font(self):
        paths = [
            ROOT / "main/apps/app_assistant/app_assistant.cpp",
            ROOT / "main/apps/app_assistant/view/assistant_view.cpp",
        ]
        combined = "\n".join(path.read_text() for path in paths)
        self.assertNotRegex(combined, r"xTaskCreate|lv_timer_create|esp_timer_create|FFT")
        self.assertNotIn("LV_IMG_DECLARE", combined)


if __name__ == "__main__":
    unittest.main()
