import pathlib
import sys
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from combined_collector import CompositeStateCollector


class FakeSource:
    def __init__(self, values):
        self.values = list(values)
        self.pending_calls = 0
        self.marked = []

    def get_state(self):
        value = self.values.pop(0) if len(self.values) > 1 else self.values[0]
        if isinstance(value, Exception):
            raise value
        return dict(value)

    def pending_watch_notification_ids(self):
        self.pending_calls += 1
        return ("codex-alert",)

    def mark_watch_notified(self, alert_ids):
        self.marked.append(tuple(alert_ids))


class Clock:
    def __init__(self, *values):
        self.values = list(values)

    def __call__(self):
        return self.values.pop(0)


class CompositeStateCollectorTests(unittest.TestCase):
    def test_trae_exception_preserves_newest_codex_and_last_trae_values(self):
        codex = FakeSource(
            [{"codex_model": "gpt-old"}, {"codex_model": "gpt-new"}]
        )
        trae = FakeSource(
            [{"trae_model": "Model A"}, RuntimeError("private Trae detail")]
        )
        collector = CompositeStateCollector(
            codex,
            trae,
            now=Clock(10.0, 20.0, 30.0, 40.0),
        )
        collector.get_state()

        state = collector.get_state()

        self.assertEqual(state["codex_model"], "gpt-new")
        self.assertEqual(state["trae_model"], "Model A")
        self.assertTrue(state["codex_fresh"])
        self.assertFalse(state["trae_fresh"])

    def test_codex_exception_preserves_newest_trae_and_last_codex_values(self):
        codex = FakeSource(
            [{"codex_model": "gpt-stable"}, RuntimeError("private Codex detail")]
        )
        trae = FakeSource(
            [{"trae_model": "Model A"}, {"trae_model": "Model B"}]
        )
        collector = CompositeStateCollector(
            codex,
            trae,
            now=Clock(10.0, 20.0, 30.0, 40.0),
        )
        collector.get_state()

        state = collector.get_state()

        self.assertEqual(state["codex_model"], "gpt-stable")
        self.assertEqual(state["trae_model"], "Model B")
        self.assertFalse(state["codex_fresh"])
        self.assertTrue(state["trae_fresh"])

    def test_each_source_keeps_an_independent_collection_timestamp(self):
        codex = FakeSource([{"codex_model": "gpt"}])
        trae = FakeSource([{"trae_model": "Model"}])
        collector = CompositeStateCollector(
            codex,
            trae,
            now=Clock(101.0, 202.0),
        )

        state = collector.get_state()

        self.assertEqual(state["codex_collected_at"], 101.0)
        self.assertEqual(state["trae_collected_at"], 202.0)

    def test_pending_notification_methods_delegate_only_to_codex(self):
        codex = FakeSource([{"codex_model": "gpt"}])
        trae = FakeSource([{"trae_model": "Model"}])
        collector = CompositeStateCollector(codex, trae)

        alert_ids = collector.pending_watch_notification_ids()
        collector.mark_watch_notified(alert_ids)

        self.assertEqual(alert_ids, ("codex-alert",))
        self.assertEqual(codex.pending_calls, 1)
        self.assertEqual(codex.marked, [("codex-alert",)])
        self.assertEqual(trae.pending_calls, 0)
        self.assertEqual(trae.marked, [])


if __name__ == "__main__":
    unittest.main()
