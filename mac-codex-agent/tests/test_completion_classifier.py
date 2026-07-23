import pathlib
import sys
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from completion_classifier import classify_completion


class CompletionClassifierTests(unittest.TestCase):
    def test_unresolved_input_has_highest_priority(self):
        result = classify_completion(
            "The work is complete.",
            unfinished_plan=True,
            unresolved_input=True,
        )

        self.assertEqual(result.outcome, "waiting")
        self.assertEqual(result.reason, "structured_input_request")

    def test_unfinished_plan_is_waiting_without_text_cue(self):
        result = classify_completion(
            "The three designs are available.",
            unfinished_plan=True,
        )

        self.assertEqual(result.outcome, "waiting")
        self.assertEqual(result.reason, "structured_unfinished_plan")

    def test_question_followed_by_recommendation_is_waiting(self):
        result = classify_completion(
            "提醒区下移时，是否把分割线也一起下移？"
            "我建议一起移动，这样分组更完整。"
        )

        self.assertEqual(result.outcome, "waiting")
        self.assertEqual(result.reason, "text_question")

    def test_confirmation_question_before_trailing_guidance_is_waiting(self):
        result = classify_completion(
            "测试待确认场景：\n\n"
            "我准备删除旧版固件缓存并重新生成发布包。"
            "此操作可能需要几分钟，是否确认继续？\n\n"
            "请先不要回复，观察手表上的 Pending 动画。"
        )

        self.assertEqual(result.outcome, "waiting")
        self.assertEqual(result.reason, "text_question_before_epilogue")

    def test_question_mark_in_markdown_heading_is_not_waiting(self):
        result = classify_completion(
            "### Assessment - Ready to merge?\n\n"
            "Yes. All current-head software gates pass."
        )

        self.assertEqual(result.outcome, "completed")
        self.assertEqual(result.reason, "completion_only")

    def test_chinese_letter_choice_group_is_waiting(self):
        result = classify_completion(
            "- A：层级最均衡，我推荐。\n"
            "- B：整体更克制。\n"
            "- C：突出状态。\n\n"
            "三版都已打开，你选 A、B 或 C 即可。"
        )

        self.assertEqual(result.outcome, "waiting")
        self.assertEqual(result.reason, "text_choice")

    def test_english_numeric_choice_group_is_waiting(self):
        result = classify_completion(
            "1. Keep the current layout\n"
            "2. Use the compact layout\n"
            "3. Emphasize alerts\n\n"
            "Reply with 1, 2, or 3."
        )

        self.assertEqual(result.outcome, "waiting")
        self.assertEqual(result.reason, "text_choice")

    def test_url_query_string_is_not_a_question(self):
        result = classify_completion(
            "Opened https://example.test/preview?variant=A. The work is complete."
        )

        self.assertEqual(result.outcome, "completed")
        self.assertEqual(result.reason, "completion_only")

    def test_inline_and_fenced_code_questions_are_not_questions(self):
        result = classify_completion(
            "Implemented `isReady?()` and verified:\n\n"
            "```text\n"
            "continue?\n"
            "```\n\n"
            "All tests passed."
        )

        self.assertEqual(result.outcome, "completed")
        self.assertEqual(result.reason, "completion_only")

    def test_historical_question_before_completion_is_completed(self):
        result = classify_completion(
            "The earlier log asked “continue?”.\n\n"
            "The repair is complete and no action remains."
        )

        self.assertEqual(result.outcome, "completed")
        self.assertEqual(result.reason, "completion_only")

    def test_trailing_codex_directive_is_ignored(self):
        result = classify_completion(
            "规格已更新。\n\n"
            "请复核后回复。\n\n"
            '::git-commit{cwd="/workspace/stopwatch"}'
        )

        self.assertEqual(result.outcome, "waiting")
        self.assertEqual(result.reason, "text_required_action")

    def test_normal_completion_is_completed(self):
        result = classify_completion("Build and tests passed.")

        self.assertEqual(result.outcome, "completed")
        self.assertEqual(result.reason, "completion_only")

    def test_empty_or_non_text_completion_is_completed(self):
        for message in ("", None, {"message": "choose A"}):
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(result.outcome, "completed")
                self.assertEqual(result.reason, "completion_only")


if __name__ == "__main__":
    unittest.main()
