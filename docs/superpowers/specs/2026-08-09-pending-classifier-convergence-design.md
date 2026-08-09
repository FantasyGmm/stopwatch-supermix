# Pending Classifier Convergence Design

## Status

Approved approach: A — bounded, high-recall deterministic classifier.

## Problem

The response-wide classifier fixed the original missed Pending alert, but later
review cycles expanded it into a hand-written natural-language parser. The
classifier grew from 143 lines to more than 3,500 lines and began modeling
English tense, voice, subject ownership, referents, confirmation polarity, and
reversal grammar. Natural-language cases cannot be exhausted, so this growth
does not provide proportional coverage or reliability.

The product goal is narrower: detect most real Codex handoffs efficiently,
prefer a controllable false positive over a missed Pending alert, and clear the
alert naturally when the user replies.

## Goals

- Cover the dominant handoff categories: direct questions, explicit required
  actions, contextual choices, structured input requests, and unfinished plans.
- Inspect the complete final response rather than a fixed tail window.
- Keep classification deterministic, local, explainable, and inexpensive.
- Treat ambiguous directed handoffs as Pending.
- Preserve rollout replay, classifier-version migration, notification
  idempotence, privacy, BLE behavior, and watch behavior.
- Make future rule additions category-driven rather than wording-driven.

## Non-Goals

- Exhaustive natural-language understanding.
- Parsing arbitrary tense, voice, subjects, ownership, coreference, or nested
  confirmation/reversal grammar.
- Proving correctness for every paraphrase of a handoff or resolution.
- Adding an LLM, network service, response upload, firmware change, or BLE
  schema change.

## Architecture

`classify_completion()` remains a pure function with the existing outcomes and
stable reasons. It uses a small ordered pipeline:

1. Structured input requests and unfinished plans remain authoritative.
2. Normalize the whole response by removing citations, directives, code, link
   targets, URLs, headings, and quoted history.
3. Detect direct prose questions using supported question punctuation. A
   same-block explanatory question is non-blocking only when a clear answer cue
   follows it and no second-person or action cue is present.
4. Detect a bounded Chinese/English vocabulary of high-frequency required
   actions: confirm/approve, choose/select, provide, reply, review, and retry.
5. Treat an option list as a choice only when it has at least two unique labels
   and nearby question, choice, or recommendation context.
6. Recognize only a bounded set of strong resolution categories: no reply, no
   confirmation, no selection, or no user action is required; or the offered
   choice was explicitly applied.
7. Reduce evidence by source order using coarse scopes. A later strong
   resolution may clear its compatible earlier scope, while later waiting
   evidence sets Pending again. No subject, tense, ownership, or referent parser
   is used.

If a phrase cannot be classified reliably by these bounded categories, it does
not create a new grammar rule. Directed questions and explicit action cues fail
open to Pending; ambiguous resolution text fails closed and cannot clear
Pending.

## Complexity Guardrails

- Keep `completion_classifier.py` below 600 lines unless a separately approved
  design changes this limit.
- Keep top-level cue patterns and token sets bounded; do not add a parser state
  machine for natural-language grammar.
- Add rules only for a new high-frequency semantic category, not for a single
  paraphrase or adversarial counterexample.
- Prefer table-driven tests representing categories. Do not add large families
  of near-identical wording tests.
- Performance must remain linear in response length without correctness
  windows that silently stop scanning after a fixed number of characters.

## Replay and Migration

Keep the shared structured-turn reducer and streaming JSONL replay. On a
classifier-version mismatch, reclassify only the latest unsuperseded completion
for each known conversation and update only its matching latest alert.

- A newly Pending alert remains unnotified until the existing summary-chime
  path succeeds.
- An already Pending alert preserves its notification state.
- A later user message or task start prevents resurrection.
- Missing, unreadable, malformed, or truncated rollout data preserves the
  existing alert. Any failure that may become readable later remains eligible
  for retry instead of permanently advancing that conversation's migration.
- Restart remains idempotent and does not repeat a successful notification.

## Testing and Acceptance

Tests are organized by behavior category:

- the original question → options → recommendation regression;
- questions with long trailing guidance;
- Chinese, English, Arabic, and inverted question punctuation;
- clear action requests with and without question punctuation;
- contextual choices versus procedures;
- self-answered explanations and non-actionable quoted/code/URL content;
- strong resolution before and after waiting evidence;
- structured-input and unfinished-plan priority;
- latest-completion replay, supersession, migration retry, and chime
  idempotence.

Before installation, run a bounded recent local-corpus comparison without
printing response content. Inspect outcome changes by category, not by trying
to encode every observed sentence. Acceptance requires the original missed
conversation to become Pending, normal completion noise to stay controlled,
all focused tests to pass, and the implementation to satisfy the complexity
guardrails.

## Delivery Scope

1. Replace the current hand-written grammar with the bounded classifier.
2. Reduce the classifier tests to category-level coverage.
3. Finish the already-started classifier-version migration against the bounded
   classifier, including retry preservation.
4. Package the shared replay module, update documentation, and perform bounded
   corpus and end-to-end watch acceptance.

