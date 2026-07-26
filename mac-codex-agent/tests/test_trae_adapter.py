import json
import os
import pathlib
import sqlite3
import sys
import tempfile
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from trae_adapter import TraePaths, TraeStateCollector
from trae_tasks import TraeTask, TraeTaskSnapshot


class FakeTracker:
    def __init__(self, tasks):
        self.tasks = tuple(tasks)

    def refresh(self, log_paths, now):
        return TraeTaskSnapshot(
            tasks=self.tasks,
            model=None,
            effort=None,
            revision=9,
        )


def create_state_db(path, key, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    connection = sqlite3.connect(path)
    connection.execute(
        "CREATE TABLE IF NOT EXISTS ItemTable (key TEXT PRIMARY KEY, value BLOB)"
    )
    connection.execute(
        "INSERT OR REPLACE INTO ItemTable VALUES (?, ?)",
        (key, json.dumps(value)),
    )
    connection.commit()
    connection.close()


class TraeStateCollectorTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temporary.name)
        self.logs = self.root / "logs"
        self.workspace_storage = self.root / "workspaceStorage"
        self.sandbox = self.root / "sandbox"
        self.paths = TraePaths(
            logs=self.logs,
            workspace_storage=self.workspace_storage,
            sandbox=self.sandbox,
            cursor=self.root / "cache" / "cursor.json",
            ledger=self.root / "cache" / "ledger.json",
        )

    def tearDown(self):
        self.temporary.cleanup()

    def collector(self, online=True, now=10.0):
        return TraeStateCollector(
            paths=self.paths,
            process_is_running=lambda: online,
            now=lambda: now,
        )

    def write_log(self, *lines):
        self.logs.mkdir(parents=True, exist_ok=True)
        path = self.logs / "agent.log"
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return path

    def write_sandbox(self, value, name="current.json"):
        self.sandbox.mkdir(parents=True, exist_ok=True)
        path = self.sandbox / name
        path.write_text(json.dumps(value), encoding="utf-8")
        return path

    def write_workspace(self, folder):
        root = self.workspace_storage / "workspace-a"
        root.mkdir(parents=True, exist_ok=True)
        path = root / "workspace.json"
        path.write_text(json.dumps({"folder": folder}), encoding="utf-8")
        return path

    def test_offline_state_does_not_touch_logs(self):
        self.write_log(
            'model_info: CustomModel { config_name: "model-a", '
            'display_model_name: Some("Model A"), '
            'reasoning_effort: Some("high") }'
        )

        state = self.collector(online=False).get_state()

        self.assertFalse(state["trae_online"])
        self.assertEqual(state["trae_model"], "--")
        self.assertEqual(state["trae_effort"], "--")
        self.assertEqual(state["trae_tasks"], [])
        self.assertFalse(self.paths.cursor.exists())
        self.assertFalse(self.paths.ledger.exists())

    def test_online_state_reports_actual_selected_model_and_effort(self):
        self.write_log(
            'model_info: CustomModel { config_name: "model-a", '
            'display_model_name: Some("Model A"), '
            'reasoning_effort: Some("high") }'
        )

        state = self.collector().get_state()

        self.assertEqual(state["trae_model"], "Model A")
        self.assertEqual(state["trae_effort"], "high")

    def test_first_run_can_use_session_correlated_current_agent_metadata(self):
        create_state_db(
            self.workspace_storage / "workspace-a" / "state.vscdb",
            "trae.currentAgent",
            {
                "session_id": "session-a",
                "task_id": "task-a",
                "selected_model": "Model DB",
                "selected_effort": "medium",
                "project_path": "/work/StopWatch",
                "task_title": "Build status page",
                "task_detail": "Adding the collector",
                "state": "running",
            },
        )

        state = self.collector().get_state()

        self.assertEqual(state["trae_model"], "Model DB")
        self.assertEqual(state["trae_effort"], "medium")
        self.assertEqual(state["trae_tasks"][0]["project"], "StopWatch")
        self.assertEqual(state["trae_tasks"][0]["title"], "Build status page")

    def test_real_session_state_enriches_project_title_model_and_effort(self):
        workspace = self.workspace_storage / "workspace-real"
        workspace.mkdir(parents=True)
        (workspace / "workspace.json").write_text(
            json.dumps({"folder": "file:///work/CloudTest"}),
            encoding="utf-8",
        )
        create_state_db(
            workspace / "state.vscdb",
            "lukeren_ai-chat:sessionRelation:modelMap",
            {
                "session-a": {
                    "solo_coder": "1_-_openrouter-3o",
                }
            },
        )
        create_state_db(
            self.workspace_storage.parent / "globalStorage" / "state.vscdb",
            "lukeren_AI.agent.model.model_list_map",
            {
                "solo_agent": [
                    {
                        "name": "openrouter-3o",
                        "display_name": "OpenRouter 3o",
                        "default_reasoning_effort": "xhigh",
                    }
                ]
            },
        )
        self.write_log(
            "2026-07-26T21:00:00+08:00 "
            "generate_session_title_and_icon: "
            "ai_agent::domain::chat::service: "
            'generate_session_title_and_icon_cloud: session_id: "session-a", '
            'title: "Fix status collector", icon: "terminal"'
        )
        collector = self.collector(online=True)
        collector._tracker = FakeTracker(
            tasks=(
                TraeTask(
                    "session-a",
                    "task-a",
                    "--",
                    "Trae task",
                    "",
                    "running",
                    10.0,
                ),
            )
        )

        state = collector.get_state()

        self.assertEqual(state["trae_model"], "OpenRouter 3o")
        self.assertEqual(state["trae_effort"], "xhigh")
        self.assertEqual(state["trae_tasks"][0]["project"], "CloudTest")
        self.assertEqual(
            state["trae_tasks"][0]["title"], "Fix status collector"
        )
        self.assertEqual(state["trae_tasks"][0]["model"], "OpenRouter 3o")
        self.assertEqual(state["trae_tasks"][0]["effort"], "xhigh")

    def test_real_session_state_does_not_use_newest_unrelated_workspace(self):
        for name, session, model in (
            ("ProjectA", "session-a", "model-a"),
            ("Unrelated", "session-b", "model-b"),
        ):
            workspace = self.workspace_storage / name
            workspace.mkdir(parents=True)
            (workspace / "workspace.json").write_text(
                json.dumps({"folder": "file:///work/{}".format(name)}),
                encoding="utf-8",
            )
            create_state_db(
                workspace / "state.vscdb",
                "lukeren_ai-chat:sessionRelation:modelMap",
                {session: {"solo_coder": "1_-_{}".format(model)}},
            )
        collector = self.collector(online=True)
        collector._tracker = FakeTracker(
            tasks=(
                TraeTask(
                    "session-a",
                    "task-a",
                    "--",
                    "Trae task",
                    "",
                    "running",
                    10.0,
                ),
            )
        )

        state = collector.get_state()

        self.assertEqual(state["trae_tasks"][0]["project"], "ProjectA")
        self.assertEqual(state["trae_tasks"][0]["model"], "model-a")

    def test_memento_correlates_workspace_and_uses_its_global_model(self):
        correct = self.workspace_storage / "a-correct"
        correct.mkdir(parents=True)
        (correct / "workspace.json").write_text(
            json.dumps({"folder": "file:///work/CorrectProject"}),
            encoding="utf-8",
        )
        create_state_db(
            correct / "state.vscdb",
            "memento/icube-ai-agent-storage",
            {
                "list": [
                    {
                        "isCurrent": True,
                        "sessionId": "session-a",
                        "messages": [],
                    }
                ],
                "currentSessionId": "session-a",
            },
        )
        create_state_db(
            correct / "state.vscdb",
            "lukeren_ai-chat:sessionRelation:modelMap",
            {},
        )
        create_state_db(
            correct / "state.vscdb",
            "lukeren_ai-chat:sessionRelation:globalModelMap",
            {"solo_coder": "1_-_model-a"},
        )

        unrelated = self.workspace_storage / "z-unrelated"
        unrelated.mkdir(parents=True)
        (unrelated / "workspace.json").write_text(
            json.dumps({"folder": "file:///work/WrongProject"}),
            encoding="utf-8",
        )
        create_state_db(
            unrelated / "state.vscdb",
            "lukeren_ai-chat:sessionRelation:modelMap",
            {"session-b": {"solo_coder": "1_-_model-b"}},
        )
        self.write_log(
            "generate_session_title_and_icon_cloud: "
            'session_id: "session-a", title: "Correct title", icon: "terminal"'
        )
        collector = self.collector(online=True)
        collector._tracker = FakeTracker(
            tasks=(
                TraeTask(
                    "session-a",
                    "task-a",
                    "--",
                    "Trae task",
                    "",
                    "running",
                    10.0,
                ),
            )
        )

        task = collector.get_state()["trae_tasks"][0]

        self.assertEqual(task["project"], "CorrectProject")
        self.assertEqual(task["title"], "Correct title")
        self.assertEqual(task["model"], "model-a")

    def test_none_effort_becomes_double_dash(self):
        self.write_sandbox(
            {
                "session_id": "session-a",
                "task_id": "task-a",
                "selected_model": "Model A",
                "selected_effort": None,
            }
        )

        state = self.collector().get_state()

        self.assertEqual(state["trae_model"], "Model A")
        self.assertEqual(state["trae_effort"], "--")

    def test_project_prefers_session_correlated_sandbox_mapping(self):
        self.write_workspace("file:///fallback/FallbackProject")
        self.write_sandbox(
            {
                "session_id": "session-a",
                "task_id": "task-a",
                "project_path": "/work/StopWatch",
                "task_title": "Task",
            }
        )

        state = self.collector().get_state()

        self.assertEqual(state["trae_tasks"][0]["project"], "StopWatch")

    def test_project_falls_back_to_workspace_json_folder_name(self):
        self.write_workspace("file:///workspace/FallbackProject")
        self.write_sandbox(
            {
                "session_id": "session-a",
                "task_id": "task-a",
                "task_title": "Task",
            }
        )

        state = self.collector().get_state()

        self.assertEqual(state["trae_tasks"][0]["project"], "FallbackProject")

    def test_task_text_is_single_line_bounded_and_control_character_free(self):
        self.write_sandbox(
            {
                "session_id": "session-a",
                "task_id": "task-a",
                "project_path": "/work/" + ("Project " * 10),
                "task_title": ("Long\n\tTitle " * 10) + "\u0000",
                "task_detail": ("Detailed\r\nprogress " * 10) + "\u0007",
                "state": "pending",
            }
        )

        task = self.collector().get_state()["trae_tasks"][0]

        self.assertLessEqual(len(task["project"]), 32)
        self.assertLessEqual(len(task["title"]), 48)
        self.assertLessEqual(len(task["detail"]), 72)
        for value in (task["project"], task["title"], task["detail"]):
            self.assertNotRegex(value, r"[\x00-\x1f\x7f]")

    def test_uncorrelated_input_history_is_ignored(self):
        create_state_db(
            self.workspace_storage / "workspace-a" / "state.vscdb",
            "history.entries",
            {
                "prompt": "private-uncorrelated-prompt",
                "response": "private-uncorrelated-response",
            },
        )

        state = self.collector().get_state()

        serialized = json.dumps(state, sort_keys=True)
        self.assertNotIn("private-uncorrelated-prompt", serialized)
        self.assertNotIn("private-uncorrelated-response", serialized)
        self.assertEqual(state["trae_tasks"], [])

    def test_missing_or_invalid_private_state_fails_closed(self):
        self.sandbox.mkdir(parents=True)
        (self.sandbox / "broken.json").write_text("{invalid", encoding="utf-8")
        self.workspace_storage.mkdir(parents=True)
        (self.workspace_storage / "broken").mkdir()
        (self.workspace_storage / "broken" / "workspace.json").write_text(
            "not-json",
            encoding="utf-8",
        )

        state = self.collector().get_state()

        self.assertTrue(state["trae_online"])
        self.assertEqual(state["trae_model"], "--")
        self.assertEqual(state["trae_effort"], "--")
        self.assertEqual(state["trae_tasks"], [])

    def test_collector_never_returns_raw_prompt_response_or_log_content(self):
        self.write_log(
            "ordinary line prompt=private-log-prompt",
            'model_info: CustomModel { config_name: "model-a", '
            'display_model_name: Some("Model A"), '
            'reasoning_effort: Some("high") }',
        )
        self.write_sandbox(
            {
                "session_id": "session-a",
                "task_id": "task-a",
                "task_title": "Safe title",
                "raw_prompt": "private-sandbox-prompt",
                "raw_response": "private-sandbox-response",
                "tool_output": "private-tool-output",
            }
        )

        serialized = json.dumps(self.collector().get_state(), sort_keys=True)

        self.assertNotIn("private-log-prompt", serialized)
        self.assertNotIn("private-sandbox-prompt", serialized)
        self.assertNotIn("private-sandbox-response", serialized)
        self.assertNotIn("private-tool-output", serialized)

    def test_collector_counts_all_active_tasks_before_detail_limit(self):
        collector = self.collector(online=True)
        collector._tracker = FakeTracker(
            tasks=tuple(
                TraeTask(
                    "session-{}".format(index),
                    "task-{}".format(index),
                    "Project {}".format(index),
                    "Task {}".format(index),
                    "detail",
                    "pending" if index < 3 else "running",
                    float(100 - index),
                )
                for index in range(7)
            )
        )

        state = collector.get_state()

        self.assertEqual(state["trae_pending_count"], 3)
        self.assertEqual(state["trae_running_count"], 4)
        self.assertEqual(len(state["trae_tasks"]), 4)
        self.assertTrue(state["trae_tasks_truncated"])

    def test_process_probe_unknown_is_not_reported_as_off(self):
        state = self.collector(online=None).get_state()
        self.assertEqual(state["trae_process_state"], "unknown")
        self.assertFalse(state["trae_online"])
        self.assertEqual(state["trae_tasks"], [])

    def test_task_payload_uses_opaque_id_and_task_telemetry(self):
        collector = self.collector(online=True)
        collector._tracker = FakeTracker(
            tasks=(
                TraeTask(
                    "session-a",
                    "task-a",
                    "Project",
                    "Task",
                    "detail",
                    "running",
                    10.0,
                    model="Task Model",
                    effort="high",
                ),
            )
        )

        task = collector.get_state()["trae_tasks"][0]

        self.assertEqual(task["id"], "db278aeb083e3771")
        self.assertEqual(task["model"], "Task Model")
        self.assertEqual(task["effort"], "high")
        self.assertNotIn("session_id", task)
        self.assertNotIn("task_id", task)

    def _assert_terminal_metadata_is_excluded(self, raw_state):
        for existing in (False, True):
            with self.subTest(raw_state=raw_state, existing=existing):
                self.write_sandbox(
                    {
                        "session_id": "session-a",
                        "task_id": "task-a",
                        "project_path": "/work/Project",
                        "task_title": "Terminal task",
                        "state": raw_state,
                    }
                )
                collector = self.collector(online=True)
                collector._tracker = FakeTracker(
                    tasks=(
                        TraeTask(
                            "session-a",
                            "task-a",
                            "Project",
                            "Terminal task",
                            "",
                            "running",
                            9.0,
                        ),
                    )
                    if existing
                    else ()
                )

                state = collector.get_state()

                self.assertEqual(state["trae_pending_count"], 0)
                self.assertEqual(state["trae_running_count"], 0)
                self.assertEqual(state["trae_tasks"], [])

    def test_completed_metadata_retires_synthesized_and_existing_tasks(self):
        self._assert_terminal_metadata_is_excluded("completed")

    def test_cancelled_metadata_retires_synthesized_and_existing_tasks(self):
        self._assert_terminal_metadata_is_excluded("cancelled")

    def test_failed_metadata_retires_synthesized_and_existing_tasks(self):
        self._assert_terminal_metadata_is_excluded("failed")

    def test_two_project_task_telemetry_is_persisted_and_newest_metadata_wins(self):
        self.paths.ledger.parent.mkdir(parents=True, exist_ok=True)
        self.paths.ledger.write_text(
            json.dumps(
                {
                    "version": 2,
                    "model": "Path Ordered Model",
                    "effort": "low",
                    "revision": 1,
                    "tasks": [
                        {
                            "session_id": "session-a",
                            "task_id": "task-a",
                            "project": "Project A",
                            "title": "Task A",
                            "detail": "",
                            "state": "running",
                            "updated_at": 20.0,
                            "model": "",
                            "effort": "",
                        },
                    ],
                }
            ),
            encoding="utf-8",
        )
        metadata_a = self.write_sandbox(
            {
                "session_id": "session-a",
                "task_id": "task-a",
                "selected_model": "Model A",
                "selected_effort": "medium",
                "project_path": "/work/Project A",
                "task_title": "Task A",
                "state": "running",
            },
            name="project-a.json",
        )
        os.utime(metadata_a, ns=(100, 100))
        self.collector(online=True).get_state()
        metadata_b = self.write_sandbox(
            {
                "session_id": "session-b",
                "task_id": "task-b",
                "selected_model": "Model B",
                "selected_effort": "high",
                "project_path": "/work/Project B",
                "task_title": "Task B",
                "state": "running",
            },
            name="project-b.json",
        )
        os.utime(metadata_b, ns=(200, 200))

        state = self.collector(online=True).get_state()
        by_title = {item["title"]: item for item in state["trae_tasks"]}

        self.assertEqual(state["trae_model"], "Model B")
        self.assertEqual(state["trae_effort"], "high")
        self.assertEqual(by_title["Task A"]["model"], "Model A")
        self.assertEqual(by_title["Task A"]["effort"], "medium")
        self.assertEqual(by_title["Task B"]["model"], "Model B")
        self.assertEqual(by_title["Task B"]["effort"], "high")

    def test_cold_start_synthesizes_every_active_project_metadata_record(self):
        metadata_a = self.write_sandbox(
            {
                "session_id": "session-a",
                "task_id": "task-a",
                "project_path": "/work/Project A",
                "task_title": "Task A",
                "state": "pending",
            },
            name="project-a.json",
        )
        metadata_b = self.write_sandbox(
            {
                "session_id": "session-b",
                "task_id": "task-b",
                "project_path": "/work/Project B",
                "task_title": "Task B",
                "state": "running",
            },
            name="project-b.json",
        )
        os.utime(metadata_a, ns=(100, 100))
        os.utime(metadata_b, ns=(200, 200))

        state = self.collector(online=True).get_state()

        self.assertEqual(state["trae_pending_count"], 1)
        self.assertEqual(state["trae_running_count"], 1)
        self.assertEqual(
            {item["project"] for item in state["trae_tasks"]},
            {"Project A", "Project B"},
        )

    def test_older_terminal_metadata_retires_task_while_newer_project_runs(self):
        for terminal_state in ("failed", "cancelled"):
            with self.subTest(terminal_state=terminal_state):
                metadata_b = self.write_sandbox(
                    {
                        "session_id": "session-b",
                        "task_id": "task-b",
                        "project_path": "/work/Project B",
                        "task_title": "Task B",
                        "state": terminal_state,
                    },
                    name="project-b.json",
                )
                metadata_a = self.write_sandbox(
                    {
                        "session_id": "session-a",
                        "task_id": "task-a",
                        "project_path": "/work/Project A",
                        "task_title": "Task A",
                        "state": "running",
                    },
                    name="project-a.json",
                )
                os.utime(
                    metadata_b,
                    ns=(10_000_000_000, 10_000_000_000),
                )
                os.utime(
                    metadata_a,
                    ns=(20_000_000_000, 20_000_000_000),
                )
                collector = self.collector(online=True)
                collector._tracker = FakeTracker(
                    tasks=(
                        TraeTask(
                            "session-b",
                            "task-b",
                            "Project B",
                            "Task B",
                            "",
                            "running",
                            5.0,
                        ),
                    )
                )

                state = collector.get_state()

                self.assertEqual(state["trae_running_count"], 1)
                self.assertEqual(
                    [item["project"] for item in state["trae_tasks"]],
                    ["Project A"],
                )

    def test_older_workspace_model_does_not_override_newer_log_model(self):
        self.write_log(
            "2026-07-24 10:00:05 model_info: CustomModel { "
            'display_model_name: Some("New Log Model"), '
            'reasoning_effort: Some("high") }'
        )
        metadata = self.write_sandbox(
            {
                "session_id": "session-a",
                "task_id": "task-a",
                "selected_model": "Old Workspace Model",
                "selected_effort": "low",
                "state": "running",
            }
        )
        os.utime(metadata, ns=(1_000_000_000, 1_000_000_000))

        state = self.collector(online=True).get_state()

        self.assertEqual(state["trae_model"], "New Log Model")
        self.assertEqual(state["trae_effort"], "high")

    def test_newer_workspace_model_overrides_older_log_model(self):
        self.write_log(
            "2020-01-01 10:00:05 model_info: CustomModel { "
            'display_model_name: Some("Old Log Model"), '
            'reasoning_effort: Some("low") }'
        )
        metadata = self.write_sandbox(
            {
                "session_id": "session-a",
                "task_id": "task-a",
                "selected_model": "New Workspace Model",
                "selected_effort": "high",
                "state": "running",
            }
        )
        os.utime(
            metadata,
            ns=(2_000_000_000_000_000_000, 2_000_000_000_000_000_000),
        )

        state = self.collector(online=True).get_state()

        self.assertEqual(state["trae_model"], "New Workspace Model")
        self.assertEqual(state["trae_effort"], "high")

    def _assert_same_task_metadata_merge(
        self,
        task_state,
        task_updated_at,
        metadata_state,
        metadata_updated_at,
        expected_state,
        expected_updated_at,
    ):
        task = TraeTask(
            "session-a",
            "task-a",
            "Original Project",
            "Original title",
            "Original detail",
            task_state,
            task_updated_at,
            model="Task Model",
            effort="high",
        )
        metadata = TraeStateCollector._normalize_metadata(
            {
                "session_id": "session-a",
                "task_id": "task-a",
                "project_path": "/work/Metadata Project",
                "task_title": "Metadata title",
                "task_detail": "Metadata detail",
                "selected_model": "Metadata Model",
                "selected_effort": "low",
                "state": metadata_state,
            }
        )

        merged = TraeStateCollector._enrich_tasks(
            (task,),
            metadata,
            "Fallback Project",
            metadata_updated_at,
        )[0]

        self.assertEqual(merged.state, expected_state)
        self.assertEqual(merged.updated_at, expected_updated_at)
        self.assertEqual(merged.project, "Metadata Project")
        self.assertEqual(merged.title, "Metadata title")
        self.assertEqual(merged.detail, "Metadata detail")
        self.assertEqual(merged.model, "Task Model")
        self.assertEqual(merged.effort, "high")

    def test_stale_terminal_metadata_does_not_hide_newer_log_restart(self):
        self._assert_same_task_metadata_merge(
            task_state="running",
            task_updated_at=200.0,
            metadata_state="failed",
            metadata_updated_at=100.0,
            expected_state="running",
            expected_updated_at=200.0,
        )

    def test_newer_terminal_metadata_retires_older_active_log_task(self):
        self._assert_same_task_metadata_merge(
            task_state="running",
            task_updated_at=50.0,
            metadata_state="cancelled",
            metadata_updated_at=100.0,
            expected_state="completed",
            expected_updated_at=100.0,
        )

    def test_newer_active_metadata_revives_older_completed_log_task(self):
        self._assert_same_task_metadata_merge(
            task_state="completed",
            task_updated_at=50.0,
            metadata_state="running",
            metadata_updated_at=100.0,
            expected_state="running",
            expected_updated_at=100.0,
        )

    def test_stale_active_metadata_does_not_revive_newer_completed_log_task(self):
        self._assert_same_task_metadata_merge(
            task_state="completed",
            task_updated_at=200.0,
            metadata_state="running",
            metadata_updated_at=100.0,
            expected_state="completed",
            expected_updated_at=200.0,
        )


if __name__ == "__main__":
    unittest.main()
