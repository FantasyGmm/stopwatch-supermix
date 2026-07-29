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
        self.assertEqual(result.reason, "text_direct_question")

    def test_confirmation_question_before_trailing_guidance_is_waiting(self):
        result = classify_completion(
            "测试待确认场景：\n\n"
            "我准备删除旧版固件缓存并重新生成发布包。"
            "此操作可能需要几分钟，是否确认继续？\n\n"
            "请先不要回复，观察手表上的 Pending 动画。"
        )

        self.assertEqual(result.outcome, "waiting")
        self.assertEqual(result.reason, "text_direct_question")

    def test_question_before_options_and_recommendation_is_waiting(self):
        result = classify_completion(
            "可以共享一个手表主密钥，但仍需独立识别 Agent。\n\n"
            "首次添加新 Agent 时，你希望如何确认？\n\n"
            "1. 手表和 Mac 显示相同验证码。\n"
            "2. 手表开放六十秒配对窗口。\n"
            "3. 只依赖系统蓝牙配对。\n\n"
            "我推荐第 1 种，安全性和可理解性最好。"
        )
        self.assertEqual(
            (result.outcome, result.reason),
            ("waiting", "text_choice_handoff"),
        )

    def test_arabic_question_punctuation_is_waiting(self):
        result = classify_completion("هل تريد المتابعة؟\n\nRecommendation follows.")

        self.assertEqual(result.outcome, "waiting")
        self.assertEqual(result.reason, "text_direct_question")

    def test_quoted_arabic_historical_question_is_completed(self):
        result = classify_completion(
            "The earlier log asked “هل تريد المتابعة؟”.\n\n"
            "The repair is complete and no action remains."
        )

        self.assertEqual(result.outcome, "completed")
        self.assertEqual(result.reason, "text_explicitly_resolved")

    def test_inverted_question_punctuation_is_waiting(self):
        result = classify_completion("¿Desea continuar.")

        self.assertEqual(result.outcome, "waiting")
        self.assertEqual(result.reason, "text_direct_question")

    def test_self_answered_explanation_is_completed(self):
        result = classify_completion(
            "为什么构建失败？原因是缓存中的 Pillow 只有 arm64 架构。\n\n"
            "修复和测试已经完成。"
        )

        self.assertEqual(result.outcome, "completed")
        self.assertEqual(result.reason, "completion_only")

    def test_directed_question_is_not_mistaken_for_self_answer(self):
        result = classify_completion(
            "你希望为什么选择方案 1？原因可以在确认后补充。"
        )

        self.assertEqual(result.outcome, "waiting")
        self.assertEqual(result.reason, "text_direct_question")

    def test_possessive_self_answer_cue_is_not_mistaken_for_directed_question(self):
        result = classify_completion(
            "Which deployment should run? The answer is yours."
        )

        self.assertEqual(result.outcome, "waiting")
        self.assertEqual(result.reason, "text_direct_question")

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
        self.assertEqual(result.reason, "text_choice_handoff")

    def test_english_numeric_choice_group_is_waiting(self):
        result = classify_completion(
            "1. Keep the current layout\n"
            "2. Use the compact layout\n"
            "3. Emphasize alerts\n\n"
            "Reply with 1, 2, or 3."
        )

        self.assertEqual(result.outcome, "waiting")
        self.assertEqual(result.reason, "text_choice_handoff")

    def test_ordered_text_evidence_outcomes(self):
        cases = (
            ("请选择部署环境。", "waiting", "text_required_action"),
            ("Please provide the missing token.", "waiting", "text_required_action"),
            ("如需更多帮助请告诉我。", "completed", "completion_only"),
            (
                "Let me know if you would like more help.",
                "completed",
                "completion_only",
            ),
            ("需要我继续吗？", "waiting", "text_direct_question"),
            (
                "应该继续吗？原因是缓存仍然过期。无需你回复。",
                "completed",
                "text_explicitly_resolved",
            ),
            (
                "无需回复，构建已经完成。\n\n你希望部署到哪个环境？",
                "waiting",
                "text_direct_question",
            ),
            (
                "你希望使用 A 还是 B？\n\n已经按 A 实施，不需要再选择。",
                "completed",
                "text_explicitly_resolved",
            ),
        )

        for message, outcome, reason in cases:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual((result.outcome, result.reason), (outcome, reason))

    def test_contextual_choice_handoff_is_waiting(self):
        result = classify_completion(
            "可用方案：\n\n"
            "A. 同码配对\n"
            "B. 限时开放\n"
            "C. 系统配对\n\n"
            "我推荐 A。"
        )

        self.assertEqual(
            (result.outcome, result.reason),
            ("waiting", "text_choice_handoff"),
        )

    def test_numbered_procedure_is_not_a_choice_handoff(self):
        result = classify_completion(
            "安装步骤：\n\n"
            "1. 下载依赖。\n"
            "2. 运行测试。\n"
            "3. 启动应用。\n\n"
            "所有步骤已经执行完成。"
        )

        self.assertEqual(
            (result.outcome, result.reason),
            ("completed", "completion_only"),
        )

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
        self.assertEqual(result.reason, "text_explicitly_resolved")

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
