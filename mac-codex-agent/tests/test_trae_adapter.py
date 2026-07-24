import json
import pathlib
import sqlite3
import sys
import tempfile
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from trae_adapter import TraePaths, TraeStateCollector


def create_state_db(path, key, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    connection = sqlite3.connect(path)
    connection.execute("CREATE TABLE ItemTable (key TEXT PRIMARY KEY, value BLOB)")
    connection.execute(
        "INSERT INTO ItemTable VALUES (?, ?)",
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


if __name__ == "__main__":
    unittest.main()
