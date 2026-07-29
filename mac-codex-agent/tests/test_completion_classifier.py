import pathlib
import sys
import time
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from completion_classifier import classify_completion


class CompletionClassifierTests(unittest.TestCase):
    def test_resolution_matching_is_bounded_for_repeated_prefixes(self):
        message = "已经按" * 10_000 + "仍在处理中。"
        started = time.monotonic()
        result = classify_completion(message)
        elapsed = time.monotonic() - started

        self.assertEqual((result.outcome, result.reason), ("completed", "completion_only"))
        self.assertLess(elapsed, 1.0)

    def test_question_parsing_scales_near_linearly(self):
        def measure(count):
            message = " ".join("Can I continue?" for _ in range(count))
            started = time.monotonic()
            result = classify_completion(message)
            return time.monotonic() - started, result

        small_elapsed, _ = measure(500)
        large_elapsed, result = measure(2_000)

        self.assertEqual(
            (result.outcome, result.reason),
            ("waiting", "text_direct_question"),
        )
        self.assertLess(large_elapsed, 1.0)
        self.assertLess(large_elapsed, small_elapsed * 8 + 0.05)

    def test_question_option_group_parsing_scales_near_linearly(self):
        def measure(count):
            group = "How should this deploy?\n\nA. Production\nB. Staging"
            message = "\n\n".join(group for _ in range(count))
            started = time.monotonic()
            result = classify_completion(message)
            return time.monotonic() - started, result

        small_elapsed, _ = measure(800)
        large_elapsed, result = measure(3_200)

        self.assertEqual(
            (result.outcome, result.reason),
            ("waiting", "text_choice_handoff"),
        )
        self.assertLess(large_elapsed, 1.5)
        self.assertLess(large_elapsed, small_elapsed * 7 + 0.02)

    def test_arabic_question_parsing_scales_near_linearly(self):
        def measure(count):
            message = " ".join("هل أتابع؟" for _ in range(count))
            started = time.monotonic()
            result = classify_completion(message)
            return time.monotonic() - started, result

        small_elapsed, _ = measure(500)
        large_elapsed, result = measure(2_000)

        self.assertEqual(
            (result.outcome, result.reason),
            ("waiting", "text_direct_question"),
        )
        self.assertLess(large_elapsed, 1.2)
        self.assertLess(large_elapsed, small_elapsed * 7 + 0.02)

    def test_dense_action_resolution_parsing_scales_near_linearly(self):
        def measure(count):
            phrase = "Please provide token, no confirmation is required, "
            message = phrase * count + "work continues"
            started = time.monotonic()
            result = classify_completion(message)
            return time.monotonic() - started, result

        small_elapsed, _ = measure(200)
        large_elapsed, result = measure(800)

        self.assertEqual(
            (result.outcome, result.reason),
            ("waiting", "text_required_action"),
        )
        self.assertLess(large_elapsed, 1.5)
        self.assertLess(large_elapsed, small_elapsed * 7 + 0.03)

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

    def test_quote_context_distinguishes_prompts_from_historical_reports(self):
        cases = (
            (
                "You don't want to proceed? That's okay.",
                "waiting",
                "text_direct_question",
            ),
            (
                "Please answer “Continue?”",
                "waiting",
                "text_direct_question",
            ),
            (
                "The earlier log asked “Continue?”. The repair is complete.",
                "completed",
                "completion_only",
            ),
        )

        for message, outcome, reason in cases:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual((result.outcome, result.reason), (outcome, reason))

    def test_historical_quote_context_can_flow_to_the_next_block(self):
        cases = (
            "The log asked “Continue?”\n\nThe repair is complete.",
            "The earlier log asked:\n\n“Continue?”\n\nThe repair is complete.",
        )

        for message in cases:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "completion_only"),
                )

        live_prompt = classify_completion("Please answer “Continue?”")
        self.assertEqual(
            (live_prompt.outcome, live_prompt.reason),
            ("waiting", "text_direct_question"),
        )

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

    def test_answer_cue_only_suppresses_one_preceding_question(self):
        result = classify_completion(
            "Why did the build fail? Should I deploy now? "
            "Because the cache was stale."
        )

        self.assertEqual(
            (result.outcome, result.reason),
            ("waiting", "text_direct_question"),
        )

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

    def test_direct_requests_and_conditional_courtesies(self):
        cases = (
            (
                "Let me know which deployment to use.",
                "waiting",
                "text_required_action",
            ),
            ("告诉我应该部署哪个环境。", "waiting", "text_required_action"),
            (
                "If you'd like more detail, please reply with a topic.",
                "completed",
                "completion_only",
            ),
            (
                "如果你想了解更多，请回复希望展开的主题。",
                "completed",
                "completion_only",
            ),
            (
                "如需更多细节，请告诉我。",
                "completed",
                "completion_only",
            ),
        )

        for message, outcome, reason in cases:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual((result.outcome, result.reason), (outcome, reason))

    def test_courtesy_composition_only_suppresses_governed_actions(self):
        cases = (
            (
                "- If you'd like more detail, please reply with a topic.",
                "completed",
                "completion_only",
            ),
            (
                "Why did it fail? Because the cache was stale. "
                "If you'd like more detail, please reply.",
                "completed",
                "completion_only",
            ),
            (
                "If you'd like more detail, let me know; please confirm deployment.",
                "waiting",
                "text_required_action",
            ),
            (
                "如果你想了解更多，请告诉我；但是请确认部署环境。",
                "waiting",
                "text_required_action",
            ),
        )

        for message, outcome, reason in cases:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual((result.outcome, result.reason), (outcome, reason))

    def test_resolution_only_clears_compatible_waiting_evidence(self):
        cases = (
            (
                "Please provide the missing token.\n\nNo choice is required.",
                "waiting",
                "text_required_action",
            ),
            (
                "Please confirm the deployment.\n\nNo confirmation is required.",
                "completed",
                "text_explicitly_resolved",
            ),
            (
                "Please choose A or B.\n\nNo selection is required.",
                "completed",
                "text_explicitly_resolved",
            ),
            (
                "Please provide the missing token.\n\nNo reply is required.",
                "completed",
                "text_explicitly_resolved",
            ),
        )

        for message, outcome, reason in cases:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual((result.outcome, result.reason), (outcome, reason))

    def test_clause_local_scopes_preserve_unresolved_compound_actions(self):
        cases = (
            (
                "Can you provide the missing token? Which deployment should I run?\n\n"
                "No choice is required.",
                "waiting",
                "text_required_action",
            ),
            (
                "Please choose A and provide the missing token. "
                "No choice is required.",
                "waiting",
                "text_required_action",
            ),
            (
                "Please reply with A or B. No selection is required.",
                "completed",
                "text_explicitly_resolved",
            ),
            (
                "Please retry the build and review the result. "
                "No reply is required.",
                "waiting",
                "text_required_action",
            ),
            (
                "Cannot continue without local access. No reply is required.",
                "waiting",
                "text_required_action",
            ),
        )

        for message, outcome, reason in cases:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual((result.outcome, result.reason), (outcome, reason))

    def test_strong_resolution_vocabulary_clears_prior_evidence(self):
        cases = (
            (
                "A. one\nB. two\n\nI recommend A.\n\n"
                "The previously offered choice has already been applied.",
                "completed",
                "text_explicitly_resolved",
            ),
            (
                "Please choose A or B.\n\nNo selection is required.",
                "completed",
                "text_explicitly_resolved",
            ),
            (
                "Please provide the missing token.\n\nNo action is required.",
                "completed",
                "text_explicitly_resolved",
            ),
        )

        for message, outcome, reason in cases:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual((result.outcome, result.reason), (outcome, reason))

    def test_resolution_context_rejects_negated_reported_and_future_claims(self):
        cases = (
            "Please confirm deployment. "
            "It is not true that no confirmation is required.",
            "Please confirm deployment. "
            "The docs say “no confirmation is required” for dry runs.",
            "Please provide the token. After that, no action is required.",
            "Please provide the token. "
            "No action is required after you provide it.",
            "Please confirm deployment. "
            "No confirmation is required according to the docs.",
        )

        for message in cases:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_resolution_validation_uses_bounded_prefix_context(self):
        waiting_cases = (
            "Please confirm deployment. "
            "It isn't true that no confirmation is required.",
            "Please confirm deployment. "
            "Do not assume no confirmation is required.",
            "Please confirm deployment. "
            "The log says no confirmation is required for dry runs.",
            "Please provide the token. Then no action is required.",
        )

        for message in waiting_cases:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

        resolved = classify_completion(
            "Please confirm deployment. "
            "No confirmation is required; I will handle deployment."
        )
        self.assertEqual(
            (resolved.outcome, resolved.reason),
            ("completed", "text_explicitly_resolved"),
        )

    def test_bounded_historical_modifier_can_resolve_a_question(self):
        for modifier in ("earlier", "previous", "prior"):
            with self.subTest(modifier=modifier):
                result = classify_completion(
                    f"Should I continue? The {modifier} question has been resolved."
                )
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "text_explicitly_resolved"),
                )

    def test_resolution_referents_and_confirmation_questions_are_precise(self):
        cases = (
            (
                "Please confirm production. No confirmation is required for staging.",
                "waiting",
                "text_required_action",
            ),
            (
                "Please provide token A. No reply is required for token B.",
                "waiting",
                "text_required_action",
            ),
            (
                "Can I proceed? No confirmation is required.",
                "completed",
                "text_explicitly_resolved",
            ),
        )

        for message, outcome, reason in cases:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual((result.outcome, result.reason), (outcome, reason))

    def test_direct_imperatives_and_chinese_coordination_require_action(self):
        messages = (
            "Choose production or staging.",
            "Reply with A or B.",
            "Confirm deployment.",
            "选择 A 或 B。",
            "请确认部署并提供缺失令牌。无需确认。",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_choice_groups_are_isolated_within_a_block(self):
        result = classify_completion(
            "A. one\n"
            "B. two\n"
            "No choice is required.\n"
            "A. alpha\n"
            "B. beta\n"
            "I recommend A."
        )

        self.assertEqual(
            (result.outcome, result.reason),
            ("waiting", "text_choice_handoff"),
        )

    def test_option_body_cue_does_not_activate_adjacent_procedure_group(self):
        result = classify_completion(
            "A. Choose one parser implementation.\n"
            "B. Use the existing parser.\n"
            "1. Download dependencies.\n"
            "2. Run tests."
        )

        self.assertEqual(
            (result.outcome, result.reason),
            ("completed", "completion_only"),
        )

    def test_exact_option_headings_activate_their_group(self):
        messages = (
            "Options:\nA. one\nB. two",
            "可用方案：\nA. 甲\nB. 乙",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_choice_handoff"),
                )

    def test_courtesy_subclauses_do_not_hide_required_actions(self):
        cases = (
            (
                "Deployment is complete; if you'd like more detail, let me know.",
                "completed",
                "completion_only",
            ),
            (
                "Please confirm; if you'd like, let me know. No reply is required.",
                "completed",
                "text_explicitly_resolved",
            ),
            (
                "Please confirm deployment; if you'd like, let me know. "
                "No reply is required for details.",
                "waiting",
                "text_required_action",
            ),
        )

        for message, outcome, reason in cases:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual((result.outcome, result.reason), (outcome, reason))

    def test_historical_quote_context_applies_to_one_adjacent_span(self):
        cases = (
            (
                'The log asked:\n\n“Continue?” Please answer “Deploy?”',
                "waiting",
                "text_direct_question",
            ),
            (
                'The earlier log asked. “Continue?” The repair is complete.',
                "completed",
                "completion_only",
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

    def test_ordinary_choice_handoff_context_is_waiting(self):
        cases = (
            "For deployment, choose one:\n\nA. Production\nB. Staging",
            "从以下方案中选择一项：\n\nA. 生产环境\nB. 预发环境",
            "A. Production\nB. Staging\n\nI recommend option A.",
            "How should this be deployed?\n\nA. Production\nB. Staging",
        )

        for message in cases:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_choice_handoff"),
                )

    def test_same_block_choice_context_after_options_is_waiting(self):
        cases = (
            "A. Preserve the current layout.\n"
            "B. Use the compact layout.\n"
            "I recommend A.",
            "1. Preserve the current layout.\n"
            "2. Use the compact layout.\n"
            "Choose 1 or 2.",
        )

        for message in cases:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_choice_handoff"),
                )

    def test_later_choice_handoff_after_resolution_is_waiting(self):
        result = classify_completion(
            "A. one\n"
            "B. two\n\n"
            "I recommend A.\n\n"
            "No choice is required.\n\n"
            "1. alpha\n"
            "2. beta\n\n"
            "I recommend 1."
        )

        self.assertEqual(
            (result.outcome, result.reason),
            ("waiting", "text_choice_handoff"),
        )

    def test_separate_singleton_procedure_labels_are_not_choice_options(self):
        result = classify_completion(
            "1. Download dependencies.\n\n"
            "The installation then configures the workspace.\n\n"
            "2. Run tests.\n\n"
            "Choose the defaults for future installations."
        )

        self.assertEqual(
            (result.outcome, result.reason),
            ("completed", "completion_only"),
        )

    def test_self_answered_question_with_decision_context_is_waiting(self):
        result = classify_completion("为什么失败？原因是缓存过期。方案稍后再定。")

        self.assertEqual(
            (result.outcome, result.reason),
            ("waiting", "text_direct_question"),
        )

    def test_self_answered_question_with_required_action_is_waiting(self):
        result = classify_completion("为什么失败？原因是缓存过期。请确认后继续。")

        self.assertEqual(
            (result.outcome, result.reason),
            ("waiting", "text_required_action"),
        )

    def test_topical_decision_words_do_not_create_waiting_evidence(self):
        cases = (
            "Why did the build fail? Because the cache expired. "
            "I recommend clearing it next time.",
            (
                "1. Document the 方案 payload.\n"
                "2. Parse option and reply payload nouns.\n\n"
                "All procedure steps are complete."
            ),
            (
                "A. 修复筛选器。\n"
                "B. 更新结果列表。\n\n"
                "两项维护工作均已完成。"
            ),
            (
                "A. 修复搜索栏。\n"
                "B. 更新结果列表。\n\n"
                "你已修复筛选器，两项维护工作均已完成。"
            ),
            (
                "1. Clear the cache.\n"
                "2. Run the tests.\n\n"
                "I recommend clearing it earlier next time."
            ),
            (
                "A. Preserve the current layout.\n"
                "B. Use the compact layout.\n\n"
                "Recommendation archived for reference."
            ),
        )

        for message in cases:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "completion_only"),
                )

    def test_whitespace_blank_lines_keep_singleton_steps_separate(self):
        cases = (
            (
                "1. Download dependencies.\n \t\n"
                "The workspace is configured next.\n\t\n"
                "2. Run tests.\n \n"
                "Choose the defaults for future installations."
            ),
            (
                "1. Download dependencies.\r\n \t\r\n"
                "The workspace is configured next.\r\n\t\r\n"
                "2. Run tests.\r\n \r\n"
                "Choose the defaults for future installations."
            ),
        )

        for message in cases:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "completion_only"),
                )

    def test_resolution_blocks_later_context_for_an_earlier_choice_group(self):
        result = classify_completion(
            "A. one\n"
            "B. two\n\n"
            "No choice is required. I recommend A."
        )

        self.assertEqual(
            (result.outcome, result.reason),
            ("completed", "text_explicitly_resolved"),
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
