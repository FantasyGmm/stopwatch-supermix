import pathlib
import sys
import time
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from completion_classifier import classify_completion


class CompletionClassifierTests(unittest.TestCase):
    def test_question_action_group_referents_scale_near_linearly(self):
        def measure(count):
            actions = " and ".join(
                f"approve target{index}" for index in range(count)
            )
            elapsed = []
            result = None
            for _ in range(2):
                started = time.monotonic()
                result = classify_completion(f"Can you {actions}?")
                elapsed.append(time.monotonic() - started)
            return min(elapsed), result

        one, _ = measure(1_000)
        two, _ = measure(2_000)
        four, result = measure(4_000)
        self.assertEqual(
            (result.outcome, result.reason), ("waiting", "text_direct_question")
        )
        self.assertLess(four, 2.0)
        self.assertLess(four, two * 3 + 0.08)
        self.assertLess(two, one * 3 + 0.05)

    def test_empty_question_action_groups_scale_near_linearly(self):
        def measure(count):
            actions = ", then ".join("approve" for _ in range(count))
            elapsed = []
            result = None
            for _ in range(2):
                started = time.monotonic()
                result = classify_completion(f"Can you {actions}?")
                elapsed.append(time.monotonic() - started)
            return min(elapsed), result

        sixteen, _ = measure(16_000)
        thirty_two, _ = measure(32_000)
        sixty_four, result = measure(64_000)
        self.assertEqual(
            (result.outcome, result.reason), ("waiting", "text_direct_question")
        )
        self.assertLess(sixty_four, 2.0)
        self.assertLess(sixty_four, thirty_two * 3 + 0.1)
        self.assertLess(thirty_two, sixteen * 3 + 0.08)

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

    def test_same_block_option_body_cues_scale_near_linearly(self):
        def measure(count):
            message = "\n".join(
                f"A. Choose parser {index}\nB. Existing parser {index}"
                for index in range(count)
            )
            started = time.monotonic()
            result = classify_completion(message)
            return time.monotonic() - started, result

        small_elapsed, _ = measure(250)
        large_elapsed, result = measure(4_000)

        self.assertEqual(
            (result.outcome, result.reason),
            ("completed", "completion_only"),
        )
        self.assertLess(large_elapsed, 1.5)
        self.assertLess(large_elapsed, small_elapsed * 24 + 0.05)

    def test_indented_option_lists_scale_near_linearly(self):
        def measure(count):
            message = "\n".join(
                f"  {'A' if index % 2 == 0 else 'B'}. "
                f"{'Choose' if index % 2 == 0 else 'Existing'} parser {index}"
                for index in range(count)
            )
            started = time.monotonic()
            result = classify_completion(message)
            return time.monotonic() - started, result

        small_elapsed, _ = measure(800)
        large_elapsed, result = measure(3_200)

        self.assertEqual(
            (result.outcome, result.reason),
            ("completed", "completion_only"),
        )
        self.assertLess(large_elapsed, 1.5)
        self.assertLess(large_elapsed, small_elapsed * 7 + 0.05)

    def test_unpunctuated_action_parsing_scales_near_linearly(self):
        def measure(count):
            message = "please provide token " * count
            started = time.monotonic()
            result = classify_completion(message)
            return time.monotonic() - started, result

        small_elapsed, _ = measure(100)
        large_elapsed, result = measure(800)

        self.assertEqual(
            (result.outcome, result.reason),
            ("waiting", "text_required_action"),
        )
        self.assertLess(large_elapsed, 1.0)
        self.assertLess(large_elapsed, small_elapsed * 14 + 0.03)

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

    def test_prefix_attribution_does_not_resolve_live_requests(self):
        messages = (
            "Please confirm production. According to the docs, "
            "no confirmation is required for production.",
            "Please confirm production. Per the documentation, "
            "no confirmation is required for production.",
            "请确认生产环境。根据文档，生产环境无需确认。",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_flexible_attribution_does_not_resolve_live_requests(self):
        reported_resolutions = (
            "According to our docs, no confirmation is required.",
            "The docs clearly say no confirmation is required.",
            "According  to the docs, no confirmation is required.",
        )

        for reported_resolution in reported_resolutions:
            with self.subTest(reported_resolution=reported_resolution):
                result = classify_completion(
                    f"Please confirm production. {reported_resolution}"
                )
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_modified_attribution_nouns_do_not_resolve_live_requests(self):
        reported_resolutions = (
            "According to project documentation, no confirmation is required.",
            "According to the project docs, no confirmation is required.",
            "Per internal docs, no confirmation is required.",
            "根据项目文档，生产环境无需确认。",
        )

        for reported_resolution in reported_resolutions:
            with self.subTest(reported_resolution=reported_resolution):
                result = classify_completion(
                    f"Please confirm production. {reported_resolution}"
                )
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_future_marker_on_assistant_continuation_does_not_void_resolution(self):
        result = classify_completion("需要我继续吗？无需回复，之后我会继续。")

        self.assertEqual(
            (result.outcome, result.reason),
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

    def test_confirmation_and_resolution_qualifiers_match_precisely(self):
        cases = (
            (
                "Please reply yes or no. No confirmation is required.",
                "completed",
                "text_explicitly_resolved",
            ),
            (
                "Should I deploy staging? No confirmation is required for staging.",
                "completed",
                "text_explicitly_resolved",
            ),
            (
                "Please provide token A. "
                "No reply is required for token A because I supplied it.",
                "completed",
                "text_explicitly_resolved",
            ),
            (
                "Please provide the missing token. "
                "No action is required from the CI runner.",
                "waiting",
                "text_required_action",
            ),
            (
                "请确认生产环境。预发环境无需确认。",
                "waiting",
                "text_required_action",
            ),
        )

        for message, outcome, reason in cases:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual((result.outcome, result.reason), (outcome, reason))

    def test_compatible_scope_and_referent_forms_resolve(self):
        messages = (
            "Do you prefer A or B? No choice is required.",
            "I need your confirmation. No confirmation is required.",
            "I need your choice. No choice is required.",
            "Should I deploy to staging? "
            "No confirmation is required for staging.",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "text_explicitly_resolved"),
                )

    def test_referent_normalization_strips_preposition_and_article(self):
        result = classify_completion(
            "Should I deploy to the staging environment? "
            "No confirmation is required for the staging environment."
        )

        self.assertEqual(
            (result.outcome, result.reason),
            ("completed", "text_explicitly_resolved"),
        )

    def test_plain_conjunction_question_is_not_choice_scope(self):
        result = classify_completion(
            "Does the report cover A and B? No choice is required."
        )

        self.assertEqual(
            (result.outcome, result.reason),
            ("waiting", "text_direct_question"),
        )

    def test_selection_verbs_scope_prose_alternatives(self):
        resolved = classify_completion(
            "Do you prefer production or staging? No choice is required."
        )
        factual = classify_completion(
            "Does the report cover A or B? No choice is required."
        )

        self.assertEqual(
            (resolved.outcome, resolved.reason),
            ("completed", "text_explicitly_resolved"),
        )
        self.assertEqual(
            (factual.outcome, factual.reason),
            ("waiting", "text_direct_question"),
        )

    def test_choice_scope_distinguishes_handoffs_from_descriptive_questions(self):
        descriptive_questions = (
            "Did you use production or staging? No choice is required.",
            "Why did you use production or staging? No choice is required.",
            "Does the parser use A or B internally? No choice is required.",
        )
        resolved_handoffs = (
            "生产环境还是预发环境？无需选择。",
            "你要 A 还是 B？无需选择。",
        )

        for message in descriptive_questions:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_direct_question"),
                )
        for message in resolved_handoffs:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "text_explicitly_resolved"),
                )

    def test_use_and_chinese_alternatives_require_prospective_choice_grammar(self):
        informational_questions = (
            "Do you use production or staging? No choice is required.",
            "Can you use production or staging? No choice is required.",
            "报告使用生产环境还是预发环境？无需选择。",
            "它还是失败了吗？无需选择。",
            "测试还是没有通过吗？无需选择。",
        )
        resolved_handoffs = (
            "生产环境还是预发环境？无需选择。",
            "你要 A 还是 B？无需选择。",
        )

        for message in informational_questions:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_direct_question"),
                )
        for message in resolved_handoffs:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "text_explicitly_resolved"),
                )

    def test_prospective_use_and_compact_parallel_alternatives_are_choice_scope(self):
        messages = (
            "Would you use production or staging? No choice is required.",
            "May I use production or staging? No choice is required.",
            "Could I use production or staging? No choice is required.",
            "甲还是乙？无需选择。",
            "新版还是旧版？无需选择。",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "text_explicitly_resolved"),
                )

    def test_bounded_compact_chinese_alternatives_are_choice_scope(self):
        messages = (
            "红色还是蓝色？无需选择。",
            "生产还是预发？无需选择。",
            "继续还是停止？无需选择。",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "text_explicitly_resolved"),
                )

    def test_common_mutually_exclusive_chinese_actions_are_choice_scope(self):
        messages = (
            "上线还是回滚？已经决定，无需选择。",
            "开启还是关闭？已经决定，无需选择。",
            "保留还是删除？已经决定，无需选择。",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "text_explicitly_resolved"),
                )

    def test_chinese_action_alternatives_allow_copula_and_commit_pairs(self):
        completed_messages = (
            "是上线还是回滚？已经决定，无需选择。",
            "提交还是撤回？已经决定，无需选择。",
        )

        for message in completed_messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "text_explicitly_resolved"),
                )

        non_action_question = classify_completion("猫还是狗？无需选择。")
        self.assertEqual(
            (non_action_question.outcome, non_action_question.reason),
            ("waiting", "text_direct_question"),
        )

    def test_bounded_single_character_action_pair_is_choice_scope(self):
        action_choice = classify_completion("开还是关？无需选择。")
        self.assertEqual(
            (action_choice.outcome, action_choice.reason),
            ("completed", "text_explicitly_resolved"),
        )

        state_question = classify_completion("它还是坏？无需选择。")
        self.assertEqual(
            (state_question.outcome, state_question.reason),
            ("waiting", "text_direct_question"),
        )

    def test_compact_chinese_state_questions_are_not_choice_scope(self):
        messages = (
            "系统还是失败？无需选择。",
            "构建还是报错？无需选择。",
            "测试还是失败？",
            "服务还是离线？",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_direct_question"),
                )

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

    def test_all_supported_bare_imperatives_require_action(self):
        messages = (
            "Provide the missing token.",
            "Retry the build.",
            "Review the diff.",
            "确认部署。",
            "提供缺失令牌。",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_action_formatting_and_subclause_imperatives_require_action(self):
        messages = (
            "Please  provide the missing token.",
            "Please\treview the diff.",
            "The build is ready; review the diff.",
            "构建完成；复核差异。",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_report_nouns_after_boundaries_are_not_imperatives(self):
        messages = (
            "Completed phases: review, test, deploy.",
            "The report contains two sections: review and deployment.",
            "The build lifecycle has two phases; review is handled automatically.",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "completion_only"),
                )

    def test_imperative_chains_and_report_headings_are_distinct(self):
        waiting_messages = (
            "The build is ready; review, then reply.",
            "The build is ready; review, then provide the token.",
            "构建完成；复核，然后回复。",
        )
        completed_messages = (
            "Status: review complete.",
            "Phase — review complete.",
        )

        for message in waiting_messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )
        for message in completed_messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "completion_only"),
                )

    def test_action_list_prefixes_are_distinct_from_report_headings(self):
        waiting_messages = (
            "Next steps: review, test, and deploy.",
            "Action required: review, test, and deploy.",
        )
        completed_messages = (
            "Status: review successful.",
            "Phase — review finished.",
            "The summary: review pending internally.",
        )

        for message in waiting_messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )
        for message in completed_messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "completion_only"),
                )

    def test_only_positive_action_list_grammar_activates_colon_and_dash_lists(self):
        action_headings = (
            "Required actions:",
            "To do:",
            "Remaining steps:",
            "Next action:",
            "The next steps are:",
        )
        report_headings = (
            "Results: review successful.",
            "Outcome: review successful.",
            "Completed work: review successful.",
            "Report — review successful.",
        )

        for heading in action_headings:
            message = f"{heading} review, test, and deploy."
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )
        for message in report_headings:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "completion_only"),
                )

    def test_clear_objects_and_additional_action_headings_activate_imperatives(self):
        direct_imperatives = (
            "The build is ready: review the diff.",
            "The build is ready — review the diff.",
            "构建完成：复核差异。",
            "Please complete these steps: review, test, and deploy.",
        )
        action_headings = (
            "Steps to complete:",
            "Tasks remaining:",
            "Action items:",
        )

        for message in direct_imperatives:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )
        for heading in action_headings:
            message = f"{heading} review, test, and deploy."
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_bounded_imperatives_are_distinct_from_report_continuations(self):
        action_messages = (
            "The build is ready: review code.",
            "The build is ready — retry build.",
            "The build is ready: provide token.",
            "The build is ready — confirm deployment.",
            "构建完成：提供令牌。",
        )
        report_messages = (
            "Results: review the diff was successful.",
            "Report — review the code passed.",
        )

        for message in action_messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )
        for message in report_messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "completion_only"),
                )

    def test_modified_objects_and_review_of_reports_are_distinct(self):
        action_messages = (
            "The build is ready: review failed tests.",
            "The build is ready: retry failed deployment.",
            "Before merging — confirm pending changes.",
        )
        report_messages = (
            "Results: review of the diff was successful.",
            "Report — review of the code passed.",
        )

        for message in action_messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )
        for message in report_messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "completion_only"),
                )

    def test_report_status_words_can_modify_imperative_objects(self):
        messages = (
            "The build is ready: review finished work.",
            "The build is ready: review successful deployments.",
            "The build is ready: review passed tests.",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_results_terminal_status_needs_no_trailing_punctuation(self):
        report = classify_completion("Results: review passed")
        self.assertEqual(
            (report.outcome, report.reason),
            ("completed", "completion_only"),
        )

        command = classify_completion("review passed tests")
        self.assertEqual(
            (command.outcome, command.reason),
            ("waiting", "text_required_action"),
        )

    def test_report_status_predicates_allow_bounded_without_qualifiers(self):
        report = classify_completion("Results: review finished without issues.")
        self.assertEqual(
            (report.outcome, report.reason),
            ("completed", "completion_only"),
        )

        action = classify_completion("The build is ready: review finished work.")
        self.assertEqual(
            (action.outcome, action.reason),
            ("waiting", "text_required_action"),
        )

    def test_results_status_predicates_allow_bounded_warning_and_issue_qualifiers(self):
        report_messages = (
            "Results: review passed without warnings.",
            "Results: review completed with no issues.",
        )
        action_messages = (
            "The build is ready: review completed migration.",
        )

        for message in report_messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "completion_only"),
                )
        for message in action_messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_direct_action_patterns_accept_horizontal_whitespace(self):
        messages = (
            "Can\tyou confirm deployment.",
            "Need\tyour confirmation.",
            "Waiting\tfor your reply.",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_approval_verification_and_obligation_grammar_requires_action(self):
        messages = (
            "Please approve it.",
            "Device verification is still required.",
            "Blocked pending hardware verification.",
            "Waiting for your approval.",
            "Please authorize the deployment.",
            "You must review the diff.",
            "You need to provide the token.",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_adjacent_security_requirement_modifiers_and_ownership_require_action(self):
        messages = (
            "Approval is also required.",
            "Authorization is also required.",
            "Verification is also required.",
            "需要您的批准才能继续。",
            "需要您的授权才能继续。",
            "需要您的验证才能继续。",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_approval_requests_resolutions_and_attribution_are_distinct(self):
        waiting_messages = (
            "I need your approval before I continue.",
            "We need your authorization.",
            "I need you to verify the device.",
            "请批准部署。",
            "According to the docs, verification is required, "
            "but I need your approval.",
        )
        completed_messages = (
            ("No approval is required.", "text_explicitly_resolved"),
            (
                "The docs say device verification is still required.",
                "completion_only",
            ),
        )

        for message in waiting_messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )
        for message, reason in completed_messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual((result.outcome, result.reason), ("completed", reason))

    def test_approval_polarity_and_attribution_are_bounded(self):
        completed_messages = (
            "No additional approval is required.",
            "It is not true that approval is required.",
            "Do not assume approval is required.",
            "The incident report states approval is required in the old workflow.",
            "Approval is required according to the logs.",
            "Authorization is required per the incident report.",
            "Verification is required according to the docs.",
            "Approval is required for production according to the logs.",
            "Authorization is required now per the incident report.",
        )

        for message in completed_messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "completion_only"),
                )

        live_requirements = (
            "The docs say approval is required, "
            "but approval is required for this deployment.",
            "Approval is not required but authorization is required.",
            "No additional approval is required but "
            "authorization is required now.",
            "Approval is required according to the logs, but approval is "
            "required.",
            "Authorization is required per the incident report, but verification is "
            "required.",
            "Verification is required according to the docs, but approval is "
            "required.",
            "Approval is required according to policy rather than logs.",
        )

        for message in live_requirements:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_incident_report_attribution_is_symmetric_for_actions_and_resolutions(self):
        cases = (
            (
                "According to the incident report, approval is required "
                "in the old workflow.",
                ("completed", "completion_only"),
            ),
            (
                "Please approve deployment. The incident report says "
                "no approval is required in the old workflow.",
                ("waiting", "text_required_action"),
            ),
        )

        for message, expected in cases:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual((result.outcome, result.reason), expected)

    def test_attribution_covers_coordinated_security_nouns_until_an_action_break(self):
        historical = classify_completion(
            "The incident report says approval and authorization required "
            "in the old workflow."
        )
        self.assertEqual(
            (historical.outcome, historical.reason),
            ("completed", "completion_only"),
        )

        natural_historical = classify_completion(
            "The incident report says approval is required and authorization "
            "is required in the old workflow."
        )
        self.assertEqual(
            (natural_historical.outcome, natural_historical.reason),
            ("completed", "completion_only"),
        )

        live_action = classify_completion(
            "The incident report says approval and authorization required "
            "in the old workflow; please verify deployment."
        )
        self.assertEqual(
            (live_action.outcome, live_action.reason),
            ("waiting", "text_required_action"),
        )

    def test_attribution_scope_reopens_for_general_explicit_actions(self):
        messages = (
            "The docs say confirmation is required in the old workflow; "
            "please confirm deployment.",
            "The docs say input is required in the old workflow; "
            "please provide deployment input.",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_security_actions_restart_after_bounded_scope_breaks(self):
        messages = (
            "No approval is required and authorization is required.",
            "No approval is required although authorization is required.",
            "The docs say approval is required; please verify deployment.",
            "No approval is required; verification is required.",
            "Not only approval is required but authorization is required. "
            "No authorization is required.",
            "According to the incident report, approval is required "
            "in the old workflow; authorization is required now.",
            "The docs say approval is required and authorization is required.",
            "The docs say approval is required although verification is required.",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_security_actions_restart_after_because_polarity_break(self):
        result = classify_completion(
            "No approval is required because authorization is required."
        )
        self.assertEqual(
            (result.outcome, result.reason),
            ("waiting", "text_required_action"),
        )

    def test_natural_not_only_security_requirement_keeps_each_scope(self):
        result = classify_completion(
            "Approval is not only required, but authorization is also required. "
            "No authorization is required."
        )
        self.assertEqual(
            (result.outcome, result.reason),
            ("waiting", "text_required_action"),
        )

    def test_approval_and_verification_are_not_confirmation_scope(self):
        messages = (
            "Waiting for your approval. No confirmation is required.",
            "Device verification is still required. "
            "No confirmation is required.",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_security_questions_use_their_matching_resolution_scope(self):
        cases = (
            "Can you approve deployment? No approval is required.",
            "Can you authorize deployment? No authorization is required.",
            "Can you verify deployment? No verification is required.",
        )

        for message in cases:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "text_explicitly_resolved"),
                )

        coordinated = classify_completion(
            "Can you approve and authorize deployment? No approval is required."
        )
        self.assertEqual(
            (coordinated.outcome, coordinated.reason),
            ("waiting", "text_direct_question"),
        )

    def test_security_question_coordination_allows_distinct_objects(self):
        messages = (
            "Can you approve deployment and authorize access? "
            "No approval is required.",
            "Could you verify the build and authorize release? "
            "No verification is required.",
            "你能验证构建并授权发布吗？无需验证。",
        )
        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_direct_question"),
                )

    def test_security_nouns_do_not_override_the_question_action_scope(self):
        messages = (
            "Can you confirm the approval status? No confirmation is required.",
            "Can you confirm that we should approve deployment? "
            "No confirmation is required.",
            'Can you confirm the phrase "review and authorize deployment"? '
            "No confirmation is required.",
        )
        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "text_explicitly_resolved"),
                )

    def test_chinese_security_question_objects_do_not_add_action_scopes(self):
        messages = (
            "你能验证批准状态吗？无需验证。",
            "你能批准授权请求吗？无需批准。",
        )
        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "text_explicitly_resolved"),
                )

    def test_security_question_referents_match_scoped_resolutions(self):
        completed = classify_completion(
            "Can you approve production? No approval is required for production."
        )
        self.assertEqual(
            (completed.outcome, completed.reason),
            ("completed", "text_explicitly_resolved"),
        )

        different_target = classify_completion(
            "Can you approve production? No approval is required for staging."
        )
        self.assertEqual(
            (different_target.outcome, different_target.reason),
            ("waiting", "text_direct_question"),
        )

    def test_multi_scope_questions_share_a_trailing_referent(self):
        messages = (
            "Can you approve or authorize production? "
            "No approval is required for production. "
            "No authorization is required for production.",
            "你能批准并授权部署吗？部署无需批准。部署无需授权。",
        )
        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "text_explicitly_resolved"),
                )

    def test_repeated_question_scopes_keep_distinct_action_referents(self):
        messages = (
            "Can you approve production and approve staging? "
            "No approval is required for production.",
            "你能批准生产并批准预发吗？生产无需批准。",
        )
        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_direct_question"),
                )

    def test_question_complement_and_chinese_particles_normalize_referents(self):
        messages = (
            "Can you verify whether approval is required for production? "
            "No verification is required for production.",
            "你能验证批准状态吗？无需验证批准状态。",
        )
        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "text_explicitly_resolved"),
                )

    def test_conditional_if_does_not_end_question_action_coordination(self):
        result = classify_completion(
            "Can you approve staging if tests pass and authorize production? "
            "No approval is required."
        )
        self.assertEqual(
            (result.outcome, result.reason),
            ("waiting", "text_direct_question"),
        )

    def test_condition_clauses_do_not_contaminate_action_referents(self):
        messages = (
            "Can you approve staging if tests pass? "
            "No approval is required for staging.",
            "Please approve staging if tests pass. "
            "No approval is required for staging.",
        )
        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "text_explicitly_resolved"),
                )

    def test_complement_ranges_support_general_outer_actions(self):
        inner = classify_completion(
            "Can you confirm whether I should approve, authorize, or verify? "
            "No confirmation is required."
        )
        self.assertEqual(
            (inner.outcome, inner.reason),
            ("completed", "text_explicitly_resolved"),
        )
        inner_general = classify_completion(
            "Can you confirm whether I should review and provide input? "
            "No confirmation is required."
        )
        self.assertEqual(
            (inner_general.outcome, inner_general.reason),
            ("completed", "text_explicitly_resolved"),
        )

        outer_messages = (
            "Can you confirm whether approval is required, then please review? "
            "No confirmation is required.",
            "Can you confirm whether approval is required, and please provide input? "
            "No confirmation is required.",
            "你能确认是否需要批准，然后请提供输入吗？无需确认。",
            "Can you confirm whether approval is required, and provide input? "
            "No confirmation is required.",
            "Can you confirm whether approval is required, then review the plan? "
            "No confirmation is required.",
            "你能确认是否需要批准，然后提供输入吗？无需确认。",
        )
        for message in outer_messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(result.outcome, "waiting")

    def test_complement_structure_distinguishes_inner_and_outer_actions(self):
        inner_messages = (
            "Can you confirm whether I should review, provide input, and reply "
            "with details? No confirmation is required.",
            "Can you confirm whether I should review and then provide input? "
            "No confirmation is required.",
            "你能确认我是否完成复核并提供输入了吗？无需确认。",
            "你能确认他是否完成复核并提供输入了吗？无需确认。",
            "你能确认团队是否完成复核并提供输入了吗？无需确认。",
            "Can you confirm whether deployment is ready and review is complete? "
            "No confirmation is required.",
            "Can you confirm whether I should wait for approval and then authorize "
            "it? No confirmation is required.",
            "Can you confirm whether we should wait for approval and then authorize "
            "it? No confirmation is required.",
            "Can you confirm whether deployment is ready and review passed? "
            "No confirmation is required.",
            "Can you confirm whether deployment is ready and review failed? "
            "No confirmation is required.",
            "Can you confirm whether deployment is ready and review succeeded? "
            "No confirmation is required.",
            "Can you confirm whether deployment is ready and review has completed? "
            "No confirmation is required.",
            "Can you confirm whether I should wait until deployment ready and then "
            "authorize it? No confirmation is required.",
            "Can you confirm whether we should wait until deployment ready and then "
            "authorize it? No confirmation is required.",
        )
        for message in inner_messages:
            with self.subTest(kind="inner", message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "text_explicitly_resolved"),
                )

        outer_messages = (
            "Can you confirm whether approval is required and provide input? "
            "No confirmation is required.",
            "Can you confirm whether approval is required, then please authorize "
            "release? No confirmation is required.",
            "Can you confirm whether approval is required and please approve "
            "release? No confirmation is required.",
            "Can you confirm whether approval is required and can you verify "
            "release? No confirmation is required.",
            "Can you confirm whether the build passed and provide input? "
            "No confirmation is required.",
            "Can you confirm whether deployment is ready and authorize release? "
            "No confirmation is required.",
            "你能确认构建是否通过并提供输入吗？无需确认。",
            "你能确认是否需要批准，并且请授权发布吗？无需确认。",
            "你能确认是否需要批准，并且你能验证发布吗？无需确认。",
            "Can you confirm whether tests pass and also provide input? "
            "No confirmation is required.",
            "Can you confirm whether tests pass and provide input? "
            "No confirmation is required.",
            "Can you confirm whether deployment is ready and then please authorize "
            "it? No confirmation is required.",
            "你能确认构建是否通过并提供输入了吗？无需确认。",
            "你能确认构建是否通过并提供输入过吗？无需确认。",
        )
        for message in outer_messages:
            with self.subTest(kind="outer", message=message):
                result = classify_completion(message)
                self.assertEqual(result.outcome, "waiting")

    def test_three_scope_and_chinese_coordinated_resolutions(self):
        messages = (
            "Please approve, authorize, and verify deployment. "
            "No verification, approval, or authorization is required.",
            "请批准并授权部署。无需批准或授权。",
            "生产需要您的批准，测试需要您的授权。"
            "生产无需批准，测试也无需授权。",
            "Please approve, authorize, and verify deployment. "
            "No approval, authorization, or verification are required.",
            "请批准并授权及验证部署。无需批准、授权或验证。",
            "请批准并授权部署。无需批准和授权。",
            "Please approve and authorize deployment. "
            "Neither approval nor authorization is required.",
            "Please approve, authorize, and verify deployment. "
            "Neither approval, authorization, nor verification is required.",
            "请批准并授权部署。无需批准与授权。",
            "请批准并授权部署。无需批准以及授权。",
        )
        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "text_explicitly_resolved"),
                )

    def test_nearest_attribution_wins_across_multiple_sources(self):
        historical_messages = (
            "The docs say approval is required, but the logs say authorization "
            "is required in the old workflow.",
            "The docs say approval is required, but according to the logs, "
            "authorization is required in the old workflow.",
            "The docs say approval is required, but per the logs, authorization "
            "is required in the old workflow.",
        )
        for message in historical_messages:
            with self.subTest(message=message):
                historical = classify_completion(message)
                self.assertEqual(
                    (historical.outcome, historical.reason),
                    ("completed", "completion_only"),
                )

        resolved = classify_completion(
            "The docs say approval is required, but authorization is required. "
            "The logs say authorization is required in the old workflow. "
            "No authorization is required."
        )
        self.assertEqual(
            (resolved.outcome, resolved.reason),
            ("completed", "text_explicitly_resolved"),
        )

    def test_question_action_groups_share_only_local_trailing_referents(self):
        result = classify_completion(
            "Can you approve and authorize production, then verify staging? "
            "No approval is required for production. "
            "No authorization is required for production. "
            "No verification is required for staging."
        )
        self.assertEqual(
            (result.outcome, result.reason),
            ("completed", "text_explicitly_resolved"),
        )
        non_question = classify_completion(
            "Please approve and authorize production, then verify staging. "
            "No approval is required for production. "
            "No authorization is required for production. "
            "No verification is required for staging."
        )
        self.assertEqual(
            (non_question.outcome, non_question.reason),
            ("completed", "text_explicitly_resolved"),
        )
        comma_group = classify_completion(
            "Please approve and authorize production, verify staging. "
            "No approval is required for production. "
            "No authorization is required for production. "
            "No verification is required for staging."
        )
        self.assertEqual(
            (comma_group.outcome, comma_group.reason),
            ("completed", "text_explicitly_resolved"),
        )

    def test_complement_ranges_exclude_inner_actions_but_restore_outer_actions(self):
        cases = (
            (
                "Can you confirm whether approval is required, then authorize release? "
                "No confirmation is required.",
                ("waiting", "text_direct_question"),
            ),
            (
                "Can you confirm if I should approve or authorize deployment? "
                "No confirmation is required.",
                ("completed", "text_explicitly_resolved"),
            ),
            (
                "Can you verify if approval is required? No verification is required.",
                ("completed", "text_explicitly_resolved"),
            ),
            (
                "你能验证生产是否需要批准，然后授权发布吗？无需验证。",
                ("waiting", "text_direct_question"),
            ),
            (
                "Can you confirm whether you approve and authorize deployment? "
                "No confirmation is required.",
                ("completed", "text_explicitly_resolved"),
            ),
            (
                "你能确认生产是否需要批准，并授权发布吗？无需确认。",
                ("waiting", "text_direct_question"),
            ),
            (
                "Can you approve production, then verify whether staging "
                "requires authorization? No approval is required for production. "
                "No verification is required for staging.",
                ("completed", "text_explicitly_resolved"),
            ),
        )
        for message, expected in cases:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual((result.outcome, result.reason), expected)

    def test_coordinated_security_resolutions_clear_each_scope(self):
        messages = (
            "Please approve and authorize deployment. "
            "No approval or authorization is required.",
            "请批准并授权部署。部署无需批准，也无需授权。",
            "Please approve and authorize deployment. "
            "No authorization or approval is required.",
        )
        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "text_explicitly_resolved"),
                )

    def test_non_question_or_security_actions_keep_each_scope(self):
        messages = (
            "Approval or authorization is required. No approval is required.",
            "需要您的批准或授权。无需批准。",
        )
        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

        resolved = classify_completion(
            "Approval or authorization is required for production. "
            "No approval is required for production. "
            "No authorization is required for production."
        )
        self.assertEqual(
            (resolved.outcome, resolved.reason),
            ("completed", "text_explicitly_resolved"),
        )

    def test_nearest_attribution_source_controls_each_requirement(self):
        historical = classify_completion(
            "The docs say approval is required, but the logs say authorization "
            "is required in the old workflow."
        )
        self.assertEqual(
            (historical.outcome, historical.reason),
            ("completed", "completion_only"),
        )

        reopened = classify_completion(
            "The docs say approval is required. The logs say authorization is "
            "required in the old workflow, but please verify deployment."
        )
        self.assertEqual(
            (reopened.outcome, reopened.reason),
            ("waiting", "text_required_action"),
        )

    def test_question_referents_ignore_requirement_complements_and_status_nouns(self):
        messages = (
            "Can you verify whether production requires approval? "
            "No verification is required for production.",
            "你能验证生产部署是否需要批准吗？生产部署无需验证。",
            "你能验证批准和授权状态吗？无需验证批准和授权状态。",
        )
        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "text_explicitly_resolved"),
                )

    def test_chinese_question_object_actions_only_coordinate_at_boundaries(self):
        result = classify_completion("你能验证批准状态并授权发布吗？无需验证。")
        self.assertEqual(
            (result.outcome, result.reason),
            ("waiting", "text_direct_question"),
        )

    def test_additional_chinese_security_coordination_marks_independent_scopes(self):
        messages = (
            "请批准，授权部署。无需批准。",
            "请批准及授权部署。无需批准。",
        )
        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_security_question_governing_forms_share_resolution_scopes(self):
        messages = (
            "Do you approve deployment? No approval is required.",
            "你能否批准部署？无需批准。",
        )
        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "text_explicitly_resolved"),
                )

    def test_or_coordinated_security_questions_keep_each_scope(self):
        messages = (
            "Can you approve or authorize deployment? No approval is required.",
            "你能批准或授权部署吗？无需批准。",
        )
        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_direct_question"),
                )

    def test_security_question_complement_clauses_do_not_add_scopes(self):
        completed = classify_completion(
            "Can you verify whether approval is required? "
            "No verification is required."
        )
        self.assertEqual(
            (completed.outcome, completed.reason),
            ("completed", "text_explicitly_resolved"),
        )

        unresolved = classify_completion(
            "Can you verify whether approval is required? No approval is required."
        )
        self.assertEqual(
            (unresolved.outcome, unresolved.reason),
            ("waiting", "text_direct_question"),
        )

    def test_attribution_scope_reopens_after_then_explicit_action(self):
        result = classify_completion(
            "The docs say approval is required in the old workflow, "
            "then please review the diff."
        )
        self.assertEqual(
            (result.outcome, result.reason),
            ("waiting", "text_required_action"),
        )

    def test_chinese_security_resolutions_accept_symmetric_grammar(self):
        messages = (
            "生产部署需要您的批准。生产部署无需批准即可继续了。",
            "生产部署需要您的批准。生产部署不需要您的批准。",
        )
        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "text_explicitly_resolved"),
                )

    def test_results_terminal_status_allows_unpunctuated_test_summary(self):
        report = classify_completion("Results: review passed all tests")
        self.assertEqual(
            (report.outcome, report.reason),
            ("completed", "completion_only"),
        )

        command = classify_completion("review passed all tests")
        self.assertEqual(
            (command.outcome, command.reason),
            ("waiting", "text_required_action"),
        )

    def test_security_requirement_referents_normalize_for_symmetrically(self):
        messages = (
            "Approval is required for production. "
            "No approval is required for production.",
            "Authorization is required for production. "
            "No authorization is required for production.",
            "Verification is required for production. "
            "No verification is required for production.",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "text_explicitly_resolved"),
                )

    def test_chinese_security_referents_distinguish_action_objects(self):
        cases = (
            (
                "请批准生产部署。无需批准测试部署。",
                ("waiting", "text_required_action"),
            ),
            (
                "请批准生产部署。无需批准生产部署。",
                ("completed", "text_explicitly_resolved"),
            ),
            (
                "生产部署需要您的批准。生产部署无需批准。",
                ("completed", "text_explicitly_resolved"),
            ),
            (
                "生产部署需要您的批准。无需批准生产部署。",
                ("completed", "text_explicitly_resolved"),
            ),
            (
                "生产部署需要您的批准。测试部署无需批准。",
                ("waiting", "text_required_action"),
            ),
        )

        for message, expected in cases:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual((result.outcome, result.reason), expected)

    def test_chinese_resolution_referents_ignore_trailing_completion_grammar(self):
        cases = (
            (
                "生产部署需要您的批准。生产部署无需批准即可继续。",
                ("completed", "text_explicitly_resolved"),
            ),
            (
                "生产部署需要您的批准。生产部署不需要批准了。",
                ("completed", "text_explicitly_resolved"),
            ),
            (
                "生产部署需要您的批准。无需批准生产部署即可继续。",
                ("completed", "text_explicitly_resolved"),
            ),
            (
                "生产部署需要您的批准。测试部署无需批准即可继续。",
                ("waiting", "text_required_action"),
            ),
        )

        for message, expected in cases:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual((result.outcome, result.reason), expected)

    def test_security_actions_emit_independent_evidence(self):
        messages = (
            "Required actions:\n- Approve deployment.",
            "Please approve and authorize deployment. "
            "No approval is required.",
            "Waiting for your approval and authorization. "
            "No approval is required.",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_comma_and_chinese_compound_security_actions_emit_independent_evidence(self):
        messages = (
            "Please approve deployment, authorize access. "
            "No approval is required.",
            "请批准并授权部署。无需批准。",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_chinese_conjunction_security_actions_emit_independent_evidence(self):
        messages = (
            "请批准和授权部署。无需批准。",
            "请批准、授权部署。无需批准。",
            "请批准并授权部署。无需批准。",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_security_resolutions_are_symmetric_in_english_and_chinese(self):
        messages = (
            "请批准部署。无需批准。",
            "请授权部署。无需授权。",
            "请验证部署。无需验证。",
            "Please approve deployment. Approval is no longer required.",
            "Please authorize deployment. Authorization is no longer required.",
            "Please verify deployment. Verification is no longer required.",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "text_explicitly_resolved"),
                )

    def test_not_required_resolutions_are_symmetric_and_attribution_safe(self):
        resolved_messages = (
            "Please approve deployment. Approval is not required.",
            "Please authorize deployment. Authorization is not required.",
            "Please verify deployment. Verification is not required.",
        )

        for message in resolved_messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "text_explicitly_resolved"),
                )

        historical_resolution = classify_completion(
            "Please authorize deployment. The incident report says "
            "authorization is not required in the old workflow."
        )
        self.assertEqual(
            (historical_resolution.outcome, historical_resolution.reason),
            ("waiting", "text_required_action"),
        )

    def test_postposed_plural_security_resolutions_clear_each_scope(self):
        resolved = classify_completion(
            "Please approve, authorize, and verify deployment. "
            "Approval, authorization, and verification are not required."
        )
        self.assertEqual(
            (resolved.outcome, resolved.reason),
            ("completed", "text_explicitly_resolved"),
        )

        negative_neighbors = (
            "Please approve, authorize, and verify deployment. The docs say "
            "approval, authorization, and verification are not required.",
            "Please approve, authorize, and verify deployment. Approval, "
            "authorization, and verification are not required after migration.",
            "请批准并授权及验证部署。文档说无需批准、授权或验证。",
            "Please approve, authorize, and verify deployment. The logs indicate "
            "approval, authorization, and verification are not required.",
            "Please approve, authorize, and verify deployment. We expect approval, "
            "authorization, and verification are not required.",
            "Please approve, authorize, and verify deployment. Approval, "
            "authorization, and verification are not required to be documented.",
        )
        for message in negative_neighbors:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_plural_no_longer_resolutions_and_context_guards(self):
        resolved_messages = (
            "Please approve and authorize deployment. Approval and authorization "
            "are no longer required.",
            "请批准并授权部署。批准和授权不再需要。",
            "请批准并授权部署。批准和授权已不再需要。",
            "Please approve deployment. We do not expect tests to pass, but "
            "approval is not required.",
            "Please approve deployment. The docs say tests failed, but approval "
            "is not required.",
            "Please approve deployment. Contrary to expectations, approval is not "
            "required.",
            "Please approve deployment. Despite our expectation, approval is not "
            "required.",
            "Please approve deployment. We expected delays, and approval is not "
            "required.",
            "Please approve deployment. Our earlier expectation was wrong: approval "
            "is not required.",
            "Please approve deployment. Contrary to what we expected, approval is "
            "not required.",
            "Please approve deployment. Although we expected approval to be required, "
            "approval is not required.",
            "Please approve deployment. What we expected was wrong: approval is not "
            "required.",
            "Please approve deployment. Even though we expected approval to be "
            "required, approval is not required.",
            "Please approve deployment. Despite what we expected, approval is not "
            "required.",
            "Please approve deployment. We expected delays, so approval is not "
            "required.",
            "Please approve deployment. We expected not only delays but outages, but "
            "approval is not required.",
            "Please approve deployment. We expected, but confirmed, that approval is "
            "not required.",
            "Please approve deployment. We expected, then confirmed that approval is "
            "not required.",
            "Please approve deployment. We did confirm that approval is not required.",
            "Please approve deployment. We have confirmed that approval is not "
            "required.",
            "Please approve deployment. We had confirmed that approval is not required.",
            "Please approve deployment. It had been confirmed that approval is not "
            "required.",
            "Please approve deployment. The team had confirmed that approval is not "
            "required.",
            "Please approve deployment. It has been confirmed that approval is not "
            "required.",
            "Please approve deployment. It is confirmed that approval is not required.",
            "Please approve deployment. It was confirmed that approval is not required.",
            "Please approve deployment. We can confirm that approval is not required.",
            "Please approve deployment. We can not only confirm that approval is not "
            "required but also document it.",
            "Please approve deployment. The team confirms that approval is not required.",
            "Please approve deployment. We expected but did not confirm, then "
            "confirmed that approval is not required.",
            "Please approve deployment. We did not confirm that authorization is not "
            "required, but we confirmed that approval is not required.",
            "Please approve deployment. We cannot confirm that authorization is not "
            "required; we confirmed that approval is not required.",
            "Please approve deployment. We cannot confirm that the tests passed; we had "
            "confirmed that approval is not required.",
            "Please approve deployment. We cannot confirm that authorization is not "
            "required, then we confirmed that approval is not required.",
            "Please approve deployment. We cannot confirm that authorization is not "
            "required, but approval is not required.",
            "Please approve deployment. We cannot confirm that authorization is not "
            "required, but the team confirmed that approval is not required.",
            "Please approve deployment. We cannot confirm that authorization is not "
            "required but we confirmed that approval is not required.",
            "Please approve deployment. We denied that authorization is not required, "
            "then confirmed that approval is not required.",
            "Please approve deployment. We cannot confirm that authorization is not "
            "required, but later confirmed that approval is not required.",
            "Please approve deployment. We cannot confirm that the tests passed, but "
            "we confirmed that approval is not required.",
            "Please approve deployment. We cannot confirm that the tests passed, then "
            "we confirmed that approval is not required.",
            "Please approve deployment. We cannot confirm that the tests passed but "
            "we confirmed that approval is not required.",
            "Please approve deployment. We cannot confirm that the tests passed, but "
            "later we confirmed that approval is not required.",
            "Please approve deployment. We cannot confirm that the tests passed, but "
            "it is confirmed that approval is not required.",
            "Please approve deployment. We cannot confirm that the tests passed, then "
            "subsequently we confirmed that approval is not required.",
            "Please approve deployment. We cannot confirm that the tests passed, but "
            "later confirmed that approval is not required.",
            "Please approve deployment. We cannot confirm that the tests passed, then "
            "confirmed that approval is not required.",
            "Please approve deployment. We cannot confirm that the tests passed, but "
            "approval is not required.",
            "Please approve and authorize deployment. We cannot confirm that the tests "
            "passed, but approval is not required, and authorization is not required.",
            "Please approve and authorize deployment. We cannot confirm that the tests "
            "passed, but authorization is not required, and approval is not required.",
            "Please approve deployment. We cannot confirm that the tests passed, "
            "however, approval is not required.",
            "Please approve deployment. We cannot confirm that the tests passed, "
            "however, we confirmed that approval is not required.",
            "Please approve deployment. We cannot confirm that the tests passed, "
            "however, later confirmed that approval is not required.",
        )
        for message in resolved_messages:
            with self.subTest(kind="resolved", message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "text_explicitly_resolved"),
                )

        guarded_messages = (
            "Please approve deployment. It is not true that we confirmed authorization "
            "is not required, and we later confirmed that approval is not required.",
            "Please approve deployment. We cannot confirm that the team confirmed that "
            "approval is not required. We later confirmed that approval is not "
            "required.",
            "Please approve deployment. We expected and confirmed that approval is "
            "not required.",
            "Please approve deployment. The incident report suggests and confirms "
            "that approval is not required.",
            "Please approve deployment. The team expected and has confirmed that "
            "approval is not required.",
            "Please approve deployment. We expected and did confirm that approval is "
            "not required.",
            "Please approve deployment. We expected and can confirm that approval is "
            "not required.",
            "Please approve deployment. It is not surprising that we confirmed that "
            "approval is not required.",
            "Please approve deployment. It is not only confirmed that approval is not "
            "required, but documented too.",
            "Please approve deployment. It is not simply confirmed that approval is not "
            "required, but documented too.",
            "Please approve deployment. It is not simply definitively confirmed that "
            "approval is not required, but documented too.",
            "Please approve deployment. We did not hesitate and confirmed that approval "
            "is not required.",
            "Please approve deployment. We did not merely confirm that approval is not "
            "required; we documented it too.",
            "Please approve deployment. We did not just confirm that approval is not "
            "required; we documented it too.",
            "Please approve deployment. We did not merely definitively confirm that "
            "approval is not required; we documented it too.",
            "Please approve deployment. We may retry and we confirmed that approval is "
            "not required.",
            "Please approve deployment. We expected and it has been confirmed that "
            "approval is not required.",
            "Please approve and authorize deployment. It is expected that approval "
            "and authorization are not required.",
            "Please approve and authorize deployment. We do not expect that approval "
            "and authorization are not required.",
            "Please approve and authorize deployment. We don't expect that approval "
            "and authorization are not required.",
            "Please approve and authorize deployment. It isn't expected that approval "
            "and authorization are not required.",
            "Please approve deployment. It was expected that approval is not "
            "required.",
            "Please approve deployment. We did not expect that approval is not "
            "required.",
            "Please approve deployment. We expected that approval is not required.",
            "Please approve deployment. We were expecting that approval is not "
            "required.",
            "Please approve deployment. We are expecting that approval is not "
            "required.",
            "Please approve deployment. We have been expecting that approval is not "
            "required.",
            "Please approve deployment. We will expect that approval is not required.",
            "Please approve deployment. We're expecting that approval is not "
            "required.",
            "Please approve deployment. We weren't expecting that approval is not "
            "required.",
            "Please approve deployment. We hadn't expected that approval is not "
            "required.",
            "Please approve deployment. We'll expect that approval is not required.",
            "Please approve deployment. It had not been expected that approval is "
            "not required.",
            "Please approve deployment. We wouldn't expect that approval is not "
            "required.",
            "Please approve deployment. We shouldn't expect that approval is not "
            "required.",
            "Please approve deployment. It won't be expected that approval is not "
            "required.",
            "Please approve deployment. It's been expected that approval is not "
            "required.",
            "Please approve deployment. We haven't yet expected that approval is not "
            "required.",
            "Please approve deployment. We expect, however, that approval is not "
            "required.",
            "Please approve deployment. It should not be expected that approval is "
            "not required.",
            "Please approve deployment. It could not be expected that approval is "
            "not required.",
            "Please approve deployment. It's not been expected that approval is not "
            "required.",
            "Please approve deployment. It'll not be expected that approval is not "
            "required.",
            "Please approve deployment. It's expected that approval is not required.",
            "Please approve deployment. It'd be expected that approval is not "
            "required.",
            "Please approve deployment. We would have expected that approval is not "
            "required.",
            "Please approve deployment. We wouldn't have expected that approval is "
            "not required.",
            "Please approve deployment. We will have expected that approval is not "
            "required.",
            "Please approve deployment. We would have been expecting that approval "
            "is not required.",
            "Please approve deployment. It would have been expected that approval is "
            "not required.",
            "Please approve deployment. It wouldn't have been expected that approval "
            "is not required.",
            "Please approve deployment. Our expectation is that approval is not "
            "required.",
            "Please approve deployment. The expectation is that approval is not "
            "required.",
            "Please approve deployment. Our expectation had been that approval is "
            "not required.",
            "Please approve deployment. Our expectation has been that approval is "
            "not required.",
            "Please approve deployment. Our expectation will be that approval is "
            "not required.",
            "Please approve deployment. Our expectation would be that approval is "
            "not required.",
            "Please approve deployment. Our expectation will have been that approval "
            "is not required.",
            "Please approve deployment. Our expectation would have been that approval "
            "is not required.",
            "Please approve deployment. Our expectation would still have been that "
            "approval is not required.",
            "Please approve deployment. Our expectation was not that approval is not "
            "required.",
            "Please approve deployment. Our expectation had not been that approval "
            "is not required.",
            "Please approve deployment. Our expectation will not be that approval is "
            "not required.",
            "Please approve deployment. Our expectation wasn't that approval is not "
            "required.",
            "Please approve deployment. Our expectation hasn't been that approval is "
            "not required.",
            "Please approve deployment. Our expectation won't be that approval is not "
            "required.",
            "Please approve deployment. We expected, but did not confirm, that "
            "approval is not required.",
            "Please approve deployment. We expected, but didn't confirm, that "
            "approval is not required.",
            "Please approve deployment. We expected but did not confirm that approval "
            "is not required.",
            "Please approve deployment. The incident report suggests, but does not "
            "confirm, that approval is not required.",
            "Please approve deployment. We expect but don't confirm that approval is "
            "not required.",
            "Please approve deployment. The incident report suggests but doesn't "
            "confirm that approval is not required.",
            "Please approve deployment. We expected but didn't confirm that approval "
            "is not required.",
            "Please approve deployment. We expected and confirmed, but did not "
            "confirm that approval is not required.",
            "Please approve deployment. We confirmed, but later denied that approval "
            "is not required.",
            "Please approve deployment. It is not true that we confirmed that "
            "approval is not required.",
            "Please approve deployment. We did not actually confirm that approval is "
            "not required.",
            "Please approve deployment. We have not yet confirmed that approval is "
            "not required.",
            "Please approve deployment. We cannot confirm that approval is not "
            "required.",
            "Please approve deployment. We cannot merely confirm that approval is not "
            "required.",
            "Please approve deployment. We can perhaps confirm that approval is not "
            "required.",
            "Please approve deployment. We can possibly confirm that approval is not "
            "required.",
            "Please approve deployment. We can probably confirm that approval is not "
            "required.",
            "Please approve deployment. Later we can confirm that approval is not "
            "required.",
            "Please approve deployment. Eventually we can confirm that approval is not "
            "required.",
            "Please approve deployment. Explicitly we can confirm that approval is not "
            "required.",
            "Please approve deployment. They are not confirming that approval is not "
            "required.",
            "Please approve deployment. They are still confirming that approval is not "
            "required.",
            "Please approve deployment. It is unconfirmed that approval is not required.",
            "Please approve deployment. It remains unconfirmed that approval is not "
            "required.",
            "Please approve deployment. We reconfirmed that approval is not required.",
            "Please approve deployment. It was reconfirmed that approval is not required.",
            "Please approve deployment. We reconfirm that approval is not required.",
            "Please approve deployment. The team reconfirms that approval is not required.",
            "Please approve deployment. We are reconfirming that approval is not required.",
            "Please approve deployment. We disconfirmed that approval is not required.",
            "Please approve deployment. It was disconfirmed that approval is not required.",
            "Please approve deployment. We disconfirm that approval is not required.",
            "Please approve deployment. The team disconfirms that approval is not required.",
            "Please approve deployment. We are disconfirming that approval is not required.",
            "Please approve deployment. It has not, despite the logs, been confirmed "
            "that approval is not required.",
            "Please approve deployment. It cannot at this time, according to the logs, "
            "be confirmed that approval is not required.",
            "Please approve deployment. Our expectation remains that approval is not "
            "required.",
            "Please approve deployment. We can definitively confirm that approval is "
            "not required.",
            "Please approve deployment. We can clearly confirm that approval is not "
            "required.",
            "Please approve deployment. We clearly confirmed that approval is not "
            "required.",
            "Please approve deployment. We did clearly confirm that approval is not "
            "required.",
            "Please approve deployment. We have clearly confirmed that approval is not "
            "required.",
            "Please approve deployment. It has clearly been confirmed that approval is "
            "not required.",
            "Please approve deployment. It was definitively confirmed that approval is "
            "not required.",
            "Please approve deployment. It is definitively confirmed that approval is "
            "not required.",
            "Please approve deployment. We did not merely promise to confirm that "
            "approval is not required.",
            "Please approve deployment. We do not only plan to confirm that approval is "
            "not required.",
            "Please approve deployment. It is not simply expected to be confirmed that "
            "approval is not required.",
            "Please approve deployment. We can not confirm that approval is not "
            "required.",
            "Please approve deployment. We may not confirm that approval is not "
            "required.",
            "Please approve deployment. We might not confirm that approval is not "
            "required.",
            "Please approve deployment. We must not confirm that approval is not "
            "required.",
            "Please approve deployment. We may confirm that approval is not required.",
            "Please approve deployment. We might confirm that approval is not "
            "required.",
            "Please approve deployment. We must confirm that approval is not "
            "required.",
            "Please approve deployment. We will confirm that approval is not "
            "required.",
            "Please approve deployment. We might later confirm that approval is not "
            "required.",
            "Please approve deployment. We may explicitly confirm that approval is "
            "not required.",
            "Please approve deployment. We will eventually confirm that approval is "
            "not required.",
            "Please approve deployment. We must first confirm that approval is not "
            "required.",
            "Please approve deployment. We should confirm that approval is not "
            "required.",
            "Please approve deployment. We would confirm that approval is not "
            "required.",
            "Please approve deployment. We have yet to confirm that approval is not "
            "required.",
            "Please approve deployment. It has not been confirmed that approval is not "
            "required.",
            "Please approve deployment. It cannot be confirmed that approval is not "
            "required.",
            "Please approve deployment. We may not have confirmed that approval is "
            "not required.",
            "Please approve deployment. It hasn't been confirmed that approval is not "
            "required.",
            "Please approve deployment. It hasn't yet been confirmed that approval "
            "is not required.",
            "Please approve deployment. It hasn't definitively been confirmed that "
            "approval is not required.",
            "Please approve deployment. It has not already been confirmed that approval "
            "is not required.",
            "Please approve deployment. It has still not been confirmed that approval "
            "is not required.",
            "Please approve deployment. We shouldn't have confirmed that approval is "
            "not required.",
            "Please approve deployment. We couldn't have confirmed that approval is "
            "not required.",
            "Please approve deployment. We wouldn't have confirmed that approval is "
            "not required.",
            "Please approve deployment. We may have confirmed that approval is not "
            "required.",
            "Please approve deployment. We might have confirmed that approval is not "
            "required.",
            "Please approve deployment. We should have confirmed that approval is not "
            "required.",
            "Please approve deployment. We would have confirmed that approval is not "
            "required.",
            "Please approve deployment. We will have confirmed that approval is not "
            "required.",
            "Please approve deployment. We might've confirmed that approval is not "
            "required.",
            "Please approve deployment. We should've confirmed that approval is not "
            "required.",
            "Please approve deployment. We would've confirmed that approval is not "
            "required.",
            "Please approve deployment. We could've confirmed that approval is not "
            "required.",
            "Please approve deployment. We could have confirmed that approval is not "
            "required.",
            "Please approve deployment. We must have confirmed that approval is not "
            "required.",
            "Please approve deployment. It isn't confirmed that approval is not "
            "required.",
            "Please approve deployment. It can't have confirmed that approval is not "
            "required.",
            "Please approve deployment. It cannot have confirmed that approval is not "
            "required.",
            "Please approve deployment. It has not yet been confirmed that approval is "
            "not required.",
            "Please approve deployment. It may not be confirmed that approval is not "
            "required.",
            "Please approve deployment. It will not be confirmed that approval is not "
            "required.",
            "Please approve deployment. It should not be confirmed that approval is "
            "not required.",
            "Please approve deployment. It may have been confirmed that approval is "
            "not required.",
            "Please approve deployment. It might've been confirmed that approval is "
            "not required.",
            "Please approve deployment. It mightn't have been confirmed that approval "
            "is not required.",
            "Please approve deployment. We can later confirm that approval is not "
            "required.",
            "Please approve deployment. We can eventually confirm that approval is "
            "not required.",
            "Please approve deployment. We can later clearly confirm that approval is "
            "not required.",
            "Please approve deployment. We can clearly eventually confirm that approval "
            "is not required.",
            "Please approve deployment. It won't be confirmed that approval is not "
            "required.",
            "Please approve deployment. It won't yet be confirmed that approval is "
            "not required.",
            "Please approve deployment. It cannot yet be confirmed that approval is "
            "not required.",
            "Please approve deployment. It won't have already been confirmed that "
            "approval is not required.",
            "Please approve deployment. It cannot have already been confirmed that "
            "approval is not required.",
            "Please approve deployment. It cannot have been already confirmed that "
            "approval is not required.",
            "Please approve deployment. It shouldn't be confirmed that approval is "
            "not required.",
            "Please approve deployment. It couldn't be confirmed that approval is not "
            "required.",
            "Please approve deployment. It can't have been confirmed that approval is "
            "not required.",
            "Please approve deployment. It cannot have been confirmed that approval is "
            "not required.",
            "Please approve deployment. It can have been confirmed that approval is "
            "not required.",
            "Please approve deployment. It can already have been confirmed that approval "
            "is not required.",
            "Please approve deployment. It may be confirmed that approval is not "
            "required.",
            "Please approve deployment. It may be already confirmed that approval is "
            "not required.",
            "Please approve deployment. It may still not have been confirmed that "
            "approval is not required.",
            "Please approve deployment. It might have confirmed that approval is not "
            "required.",
            "Please approve deployment. It will have been confirmed that approval is "
            "not required.",
            "Please approve deployment. It would be confirmed that approval is not "
            "required.",
            "Please approve deployment. It could have been confirmed that approval is "
            "not required.",
            "Please approve deployment. It should not have been confirmed that approval "
            "is not required.",
            "Please approve deployment. It may already have been confirmed that approval "
            "is not required.",
            "Please approve deployment. It may have already been confirmed that approval "
            "is not required.",
            "Please approve deployment. It wasn't confirmed that approval is not "
            "required.",
            "Please approve deployment. It has never been confirmed that approval is "
            "not required.",
            "Please approve deployment. We cannot confirm that we confirmed that "
            "approval is not required.",
            "Please approve deployment. We did not confirm that we confirmed that "
            "approval is not required.",
            "Please approve deployment. We cannot confirm that the team then confirmed "
            "that approval is not required.",
            "Please approve deployment. We cannot confirm that the team discussed it "
            "but later confirmed that approval is not required.",
            "Please approve deployment. We cannot confirm that the team, however, "
            "confirmed that approval is not required.",
            "Please approve deployment. We cannot confirm that Alice and the team "
            "confirmed that approval is not required.",
            "Please approve deployment. We cannot confirm that the tests passed, but "
            "we discussed whether approval is not required.",
            "Please approve deployment. We cannot confirm that the tests passed, and "
            "we confirmed that approval is not required.",
            "Please approve deployment. We cannot confirm that the tests passed but "
            "later confirmed that approval is not required.",
            "Please approve deployment. We cannot confirm that the tests passed, but "
            "later we will confirm that approval is not required.",
            "Please approve deployment. We cannot confirm that the tests passed, but "
            "it is being definitively confirmed that approval is not required.",
            "Please approve deployment. We cannot confirm that the tests passed, then "
            "later we may confirm that approval is not required.",
            "Please approve deployment. We cannot confirm that the tests passed, "
            "however, later will confirm that approval is not required.",
            "Please approve deployment. We cannot confirm that the tests passed, "
            "however, later we can confirm that approval is not required.",
            "Please approve deployment. We cannot confirm that the tests passed, and "
            "later we confirmed that approval is not required.",
            "Please approve deployment. We cannot confirm that verification is not "
            "required, authorization is not required, and approval is not required.",
            "Please approve deployment. We cannot confirm that verification is not "
            "required, but authorization is not required, and approval is not required.",
            "Please approve deployment. We expected, but did not confirm, that "
            "authorization is not required, and approval is not required.",
            "Please approve deployment. We expect authorization is not required, and "
            "approval is not required.",
            "Please approve deployment. Although we expect, however, that approval is "
            "not required, deployment remains blocked.",
            "Please approve deployment. Even though we expect authorization is not "
            "required, and approval is not required, deployment remains blocked.",
            "Please approve deployment. We expect not only that authorization is not "
            "required but that approval is not required.",
            "Please approve and authorize deployment. Approval and authorization "
            "are not required according to the logs.",
            "Please approve and authorize deployment. Approval and authorization "
            "are not required per the incident report.",
            "Please approve and authorize deployment. Approval and authorization "
            "are not required according to the docs.",
            "Please approve and authorize deployment. Approval and authorization "
            "are not required according to the latest logs.",
            "Please approve and authorize deployment. Approval and authorization "
            "are no longer required per the latest incident report.",
            "Please approve deployment. Approval is not required according to the "
            "most recent CI logs.",
            "Please approve deployment. Approval is not required per the most recent "
            "CI incident report.",
            "Please approve deployment. Approval is not required yet.",
        )
        for message in guarded_messages:
            with self.subTest(kind="guarded", message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_bounded_security_ownership_requests_require_action(self):
        messages = (
            "我需要你的批准才能继续。",
            "我需要你的授权才能继续。",
            "我需要你的验证才能继续。",
            "We require your approval before proceeding.",
            "We require your authorization before proceeding.",
            "We require your verification before proceeding.",
            "Awaiting your approval.",
            "Awaiting your authorization.",
            "Awaiting your verification.",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_security_requirements_exclude_bounded_non_user_ownership(self):
        excluded = classify_completion(
            "Authorization is required, but not from you."
        )
        self.assertEqual(
            (excluded.outcome, excluded.reason),
            ("completed", "completion_only"),
        )

        external_owner = classify_completion(
            "Authorization is required, but not from the CI runner."
        )
        self.assertEqual(
            (external_owner.outcome, external_owner.reason),
            ("waiting", "text_required_action"),
        )

    def test_non_user_ownership_exclusion_is_exact(self):
        qualified_owners = (
            "Authorization is required, but not from you alone.",
            "Authorization is required, but not from you personally.",
            "Authorization is required, but not from you right now.",
            "Authorization is required, but not from you or your team.",
        )
        for message in qualified_owners:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

        exact_exclusion = classify_completion(
            "Authorization is required, but not from you."
        )
        self.assertEqual(
            (exact_exclusion.outcome, exact_exclusion.reason),
            ("completed", "completion_only"),
        )

    def test_all_multiword_cues_accept_horizontal_whitespace(self):
        messages = (
            "Cannot  continue without the token.",
            "Cannot\tcontinue without the token.",
            "Let  me know which deployment to use.",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_bounded_coordinated_actions_survive_confirmation_resolution(self):
        messages = (
            "Please confirm deployment and also provide token. "
            "No confirmation is required.",
            "请确认部署并在准备好后再提供缺失令牌。无需确认。",
            "请确认部署然后再提供缺失令牌。无需确认。",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_coordinated_noun_requirements_emit_independent_evidence(self):
        messages = (
            "I need your confirmation and input. No confirmation is required.",
            "Waiting for your confirmation and input. "
            "No confirmation is required.",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_bounded_requirement_lists_emit_independent_evidence(self):
        requirements = (
            "confirmation and also input",
            "confirmation and your input",
            "confirmation, choice, and input",
        )

        for prefix in ("I need your ", "Waiting for your "):
            for requirement in requirements:
                message = (
                    f"{prefix}{requirement}. No confirmation is required."
                )
                with self.subTest(message=message):
                    result = classify_completion(message)
                    self.assertEqual(
                        (result.outcome, result.reason),
                        ("waiting", "text_required_action"),
                    )

    def test_coordinated_nouns_belong_only_to_requirement_phrases(self):
        messages = (
            "Please confirm the choice and input. No confirmation is required.",
            "Please confirm deployment, input included. "
            "No confirmation is required.",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "text_explicitly_resolved"),
                )

    def test_owned_requirement_lists_accept_repeated_possessives(self):
        result = classify_completion(
            "I need your confirmation, your choice, and your input. "
            "No confirmation is required. No reply is required."
        )

        self.assertEqual(
            (result.outcome, result.reason),
            ("completed", "text_explicitly_resolved"),
        )

    def test_unqualified_no_reply_resolves_communicative_choices_only(self):
        resolved_messages = (
            "Please reply with A or B. No reply is required.",
            "Which should I use? No reply is required.",
            "Options:\nA. Production\nB. Staging\n"
            "Which should I use? No reply is required.",
            "I need your confirmation, your choice, and your input. "
            "No confirmation is required. No reply is required.",
            "I need your choice. No reply is required.",
            "Waiting for your choice. No reply is required.",
        )
        unresolved_messages = (
            "No reply is required. Which should I use?",
            "Please reply with A or B. No reply is required for C or D.",
        )

        for message in resolved_messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("completed", "text_explicitly_resolved"),
                )
        for message in unresolved_messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(result.outcome, "waiting")

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

    def test_resolved_option_group_does_not_suppress_later_choice_action(self):
        result = classify_completion(
            "Options:\n"
            "A. production\n"
            "B. staging\n"
            "No choice is required.\n"
            "Please choose deployment A or B."
        )

        self.assertEqual(
            (result.outcome, result.reason),
            ("waiting", "text_required_action"),
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

    def test_option_groups_allow_indented_continuation_lines(self):
        result = classify_completion(
            "Options:\n"
            "A. Production\n"
            "   Uses the release cluster.\n"
            "B. Staging\n"
            "   Uses the validation cluster.\n"
            "I recommend A."
        )

        self.assertEqual(
            (result.outcome, result.reason),
            ("waiting", "text_choice_handoff"),
        )

    def test_cross_block_option_body_cue_does_not_activate_procedure_group(self):
        result = classify_completion(
            "A. Choose one parser implementation.\n"
            "   This note belongs to the option body.\n\n"
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

    def test_general_option_headings_activate_their_group(self):
        headings = (
            "Deployment options:",
            "Available options:",
            "选项：",
            "方案：",
        )

        for heading in headings:
            with self.subTest(heading=heading):
                result = classify_completion(f"{heading}\nA. one\nB. two")
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_choice_handoff"),
                )

    def test_flexible_option_headings_activate_their_group(self):
        headings = (
            "Here are the options:",
            "Two options:",
            "可选方案：",
            "以下是方案：",
        )

        for heading in headings:
            with self.subTest(heading=heading):
                result = classify_completion(f"{heading}\nA. one\nB. two")
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_choice_handoff"),
                )

    def test_natural_option_headings_activate_their_group(self):
        headings = (
            "3 options:",
            "Your options are:",
            "Options available:",
            "可选的 2 个方案：",
        )

        for heading in headings:
            with self.subTest(heading=heading):
                result = classify_completion(f"{heading}\nA. one\nB. two")
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_choice_handoff"),
                )

    def test_next_block_choice_question_anchors_preceding_option_group(self):
        result = classify_completion(
            "A. Production\n"
            "B. Staging\n\n"
            "Which should I use?"
        )

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

    def test_new_imperative_ends_courtesy_coordination(self):
        messages = (
            "If you'd like more detail, let me know, and please confirm deployment.",
            "如果你想了解更多，请告诉我，并请确认部署环境。",
        )

        for message in messages:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual(
                    (result.outcome, result.reason),
                    ("waiting", "text_required_action"),
                )

    def test_courtesy_contraction_does_not_hide_later_mandatory_action(self):
        cases = (
            (
                "Let me know if you'd like more help.",
                "completed",
                "completion_only",
            ),
            (
                "Let me know if you'd like more help; please confirm deployment.",
                "waiting",
                "text_required_action",
            ),
        )

        for message, outcome, reason in cases:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual((result.outcome, result.reason), (outcome, reason))

    def test_courtesy_contraction_accepts_internal_horizontal_whitespace(self):
        cases = (
            (
                "Let me know if you'd like  more help.",
                "completed",
                "completion_only",
            ),
            (
                "Let me know if you'd like\tmore help; please confirm deployment.",
                "waiting",
                "text_required_action",
            ),
        )

        for message, outcome, reason in cases:
            with self.subTest(message=message):
                result = classify_completion(message)
                self.assertEqual((result.outcome, result.reason), (outcome, reason))

    def test_typographic_courtesy_contraction_is_optional(self):
        result = classify_completion("Let me know if you’d like more help.")

        self.assertEqual(
            (result.outcome, result.reason),
            ("completed", "completion_only"),
        )

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

    def test_choice_question_after_options_uses_choice_handoff_reason(self):
        result = classify_completion(
            "A. Production\n"
            "B. Staging\n"
            "Which should I use?"
        )

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
