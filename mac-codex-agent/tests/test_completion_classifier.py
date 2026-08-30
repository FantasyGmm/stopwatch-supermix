import pathlib
import sys
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from completion_classifier import CLASSIFIER_VERSION, classify_completion


def classify(message, **kwargs):
    result = classify_completion(message, **kwargs)
    return result.outcome, result.reason


class CompletionClassifierTests(unittest.TestCase):
    def test_classifier_version_is_three(self):
        self.assertEqual(CLASSIFIER_VERSION, 3)

    def test_structured_input_has_highest_priority(self):
        self.assertEqual(
            classify(
                "No reply is required.",
                unfinished_plan=True,
                unresolved_input=True,
            ),
            ("waiting", "structured_input_request"),
        )

    def test_structured_plan_is_authoritative(self):
        self.assertEqual(
            classify("No reply is required.", unfinished_plan=True),
            ("waiting", "structured_unfinished_plan"),
        )

    def test_original_question_options_recommendation_is_waiting(self):
        message = (
            "可以共享一个手表主密钥，但仍需独立识别 Agent。\n\n"
            "首次添加 Agent 时，你希望如何确认？\n\n"
            "1. 手表和 Mac 显示相同验证码。\n"
            "2. 手表开放六十秒配对窗口。\n"
            "3. 只依赖系统蓝牙配对。\n\n"
            "我推荐第 1 种，安全性和可理解性最好。"
        )
        self.assertEqual(
            classify(message),
            ("waiting", "text_choice_handoff"),
        )

    def test_question_is_scanned_across_the_complete_response(self):
        message = "هل تريد المتابعة؟\n\n" + ("Long operational guidance. " * 200)
        self.assertEqual(
            classify(message),
            ("waiting", "text_direct_question"),
        )

    def test_question_punctuation_is_language_independent(self):
        cases = (
            "需要我继续吗？",
            "هل تريد المتابعة؟",
            "¿Desea continuar.",
            "Should I continue?",
        )
        for message in cases:
            with self.subTest(message=message):
                self.assertEqual(classify(message)[0], "waiting")

    def test_clear_action_requests_are_waiting(self):
        cases = (
            "请确认部署环境。",
            "请选择 A 或 B。",
            "请提供缺失的令牌。",
            "Please approve the deployment.",
            "Please provide the missing token.",
            "Waiting for your reply.",
        )
        for message in cases:
            with self.subTest(message=message):
                self.assertEqual(
                    classify(message),
                    ("waiting", "text_required_action"),
                )

    def test_confirmation_gated_first_person_future_action_is_waiting(self):
        cases = (
            "确认后我就直接修改并做七个方案的圆屏截图复核。",
            "确认后我会处理剩余问题。",
            "确认后我们再继续实现。",
            "确认后我将提交修改。",
        )
        for message in cases:
            with self.subTest(message=message):
                self.assertEqual(
                    classify(message),
                    ("waiting", "text_required_action"),
                )

    def test_confirmation_followed_by_completed_action_is_completed(self):
        cases = (
            "方案确认后已继续实现，当前工作已经完成。",
            "确认后已经完成部署。",
        )
        for message in cases:
            with self.subTest(message=message):
                self.assertEqual(
                    classify(message),
                    ("completed", "completion_only"),
                )

    def test_conditional_courtesy_offers_are_not_actions(self):
        cases = (
            "如需更多帮助请告诉我。",
            "Let me know if you would like more help.",
        )
        for message in cases:
            with self.subTest(message=message):
                self.assertEqual(
                    classify(message),
                    ("completed", "completion_only"),
                )

    def test_self_answered_explanation_is_completed(self):
        self.assertEqual(
            classify("为什么构建失败？原因是缓存过期。修复已经完成。"),
            ("completed", "completion_only"),
        )

    def test_directed_question_is_not_treated_as_self_answered(self):
        self.assertEqual(
            classify("你希望为什么选择方案 1？原因可以在确认后补充。"),
            ("waiting", "text_direct_question"),
        )

    def test_non_actionable_question_marks_are_ignored(self):
        cases = (
            "Opened https://example.test/preview?variant=A. Work is complete.",
            "Implemented `isReady?()` and all tests passed.",
            "Implemented:\n```text\ncontinue?\n```\nAll tests passed.",
            "### Assessment - Ready to merge?\n\nYes. All checks passed.",
            "The earlier log asked “continue?”. The repair is complete.",
        )
        for message in cases:
            with self.subTest(message=message):
                self.assertEqual(
                    classify(message),
                    ("completed", "completion_only"),
                )

    def test_contextual_choice_list_is_waiting(self):
        message = (
            "可用方案：\n\n"
            "A. 同码配对\n"
            "B. 限时开放\n"
            "C. 系统配对\n\n"
            "我推荐 A。"
        )
        self.assertEqual(
            classify(message),
            ("waiting", "text_choice_handoff"),
        )

    def test_numeric_choice_with_reply_cue_is_waiting(self):
        message = "1. Keep layout\n2. Compact layout\n3. Alerts\n\nReply with 1, 2, or 3."
        self.assertEqual(
            classify(message),
            ("waiting", "text_choice_handoff"),
        )

    def test_numbered_procedure_is_not_a_choice(self):
        message = (
            "安装步骤：\n\n"
            "1. 下载依赖。\n"
            "2. 运行测试。\n"
            "3. 启动应用。\n\n"
            "所有步骤已经执行完成。"
        )
        self.assertEqual(
            classify(message),
            ("completed", "completion_only"),
        )

    def test_strong_resolution_clears_compatible_evidence(self):
        cases = (
            (
                "请确认部署。无需确认。",
                ("completed", "text_explicitly_resolved"),
            ),
            (
                "你希望使用 A 还是 B？已经按 A 实施，不需要再选择。",
                ("completed", "text_explicitly_resolved"),
            ),
        )
        for message, expected in cases:
            with self.subTest(message=message):
                self.assertEqual(classify(message), expected)

    def test_resolution_does_not_clear_an_unrelated_scope(self):
        self.assertEqual(
            classify("Please confirm deployment. No choice is required."),
            ("waiting", "text_required_action"),
        )

    def test_later_waiting_evidence_wins(self):
        self.assertEqual(
            classify("无需回复，构建完成。你希望部署到哪个环境？"),
            ("waiting", "text_direct_question"),
        )

    def test_trailing_codex_directive_is_ignored(self):
        self.assertEqual(
            classify(
                "规格已更新。\n\n请复核后回复。\n\n"
                '::git-commit{cwd="/workspace/stopwatch"}'
            ),
            ("waiting", "text_required_action"),
        )

    def test_invalid_or_empty_completion_is_completed(self):
        for message in ("", None, {"message": "choose A"}):
            with self.subTest(message=message):
                self.assertEqual(
                    classify(message),
                    ("completed", "completion_only"),
                )

    def test_normal_completion_is_completed(self):
        self.assertEqual(
            classify("Build and tests passed."),
            ("completed", "completion_only"),
        )

    def test_classifier_source_stays_bounded(self):
        source = pathlib.Path(__file__).resolve().parents[1] / "completion_classifier.py"
        line_count = len(source.read_text(encoding="utf-8").splitlines())
        self.assertLessEqual(line_count, 600)


if __name__ == "__main__":
    unittest.main()
