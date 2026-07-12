# Chat regression tests

The `bot_chat_tests` executable validates critical `botlib` chat behaviors
without booting the full game. The suite is intentionally narrow so failures
immediately highlight regressions in the translator and parser code paths that
feed the chat system.

## Build and wiring

- `tests/chat/CMakeLists.txt` enables the target only when `BUILD_TESTING` is
  set. The executable is compiled as `bot_chat_tests` with the C11 feature set
  and inherits the global `${PROJECT_SOURCE_DIR}/src` include path so the tests
  can include headers directly from the botlib sources.
- The test harness statically links the concrete implementations for
  `ai_chat.c`, `l_precomp.c`, and `l_script.c` because their helpers are not
  exposed through a dedicated library yet. `test_bot_chat_stubs.c` replaces the
  engine-facing hooks (logging, memory allocation, and `bot_import_t` plumbing)
  with minimal doubles so the chat code can run in isolation.
- `BOT_ASSET_ROOT` is injected via `target_compile_definitions` and defaults to
  `${PROJECT_SOURCE_DIR}/dev_tools/assets`. The tests must be executed from a
  checkout that includes the Gladiator assets under that path. At a minimum the
  following files are required:
- `dev_tools/assets/rchat.c` for reply-table coverage.
- `dev_tools/assets/match.c` for join-context coverage.
- `dev_tools/assets/rnd.c` for retail random-string table expansion.
- `dev_tools/assets/syn.c` and `dev_tools/assets/syn.h` for synonym context
  coverage.
- `dev_tools/assets/bots/babe_t.c` and `dev_tools/assets/ichat.h` for named
  initial-chat block coverage.
- `dev_tools/assets/unit_test_chat.c` for deterministic success/failure chat templates and
  random-string validation contexts.

## Test layout (`test_bot_chat.c`)

`main` runs a collection of focused assertions. Each helper allocates an
isolated `bot_chatstate_t`, loads the relevant chat file, and frees the state
when finished so resource leaks are easy to spot. The existing coverage includes:

| Test | Purpose |
| --- | --- |
| `test_retail_initial_chat_block_drives_enter_event` | Loads a retail bot personality and verifies its `enter_game` templates drive `BotEnterChat`. |
| `test_retail_initial_chat_counts_raw_type_buckets` | Confirms raw initial-chat buckets such as `exit_game`, `start_level`, and `end_level` survive parsing and are visible through `BotNumInitialChats`, including Q3 successor aliases. |
| `test_retail_initial_chat_constructs_from_alias` | Exercises `BotInitialChat` through the `game_exit` alias and verifies a constructed message is queued without raw variable escapes. |
| `test_retail_initial_chat_missing_name_is_rejected` | Preserves the HLIL-observed missing-chat diagnostic for named bot chat blocks. |
| `test_reply_chat_death_context` | Confirms context `1` can construct a death-context response from the loaded retail chat assets. |
| `test_reply_chat_falls_back_to_reply_table` | Verifies that unmatched contexts fall back to the raw reply table (context `5`). |
| `test_reply_chat_matches_synonym_pattern` | Ensures inline `CONTEXT_*` synonym blocks are not mistaken for reply tables and can match text. |
| `test_reply_chat_without_pattern_falls_back_to_reply_table` | Exercises the fallback reply path when no match template accepts the input. |
| `test_enter_chat_enqueues_message` | Exercises the context queue and asserts the join message is emitted with type `2`. |
| `test_enter_chat_cooldown_blocks_repeated_messages` | Validates the cooldown tracking path by forcing the log line `"context 2 blocked by cooldown"`. |
| `test_reply_chat_unmatched_rule_returns_false_quietly` | Uses `BotLib_TestResetLastMessage` / `BotLib_TestGetLastMessage` from the stubs to ensure unmatched reply text returns false without inventing a `"no rchats"` diagnostic. |
| `test_reply_chat_string_key_uses_q3_word_separators` | Pins Q3 `StringContainsWord` separators for plain reply keys so punctuation such as `?` and `-` does not terminate a reply word. |
| `test_synonym_lookup_contains_nearbyitem_entries` | Spot-checks that the synonym tables expose expected phrases. |
| `test_known_template_is_registered` | Asserts the sibling `match.c` obituary templates are registered when `rchat.c` loads. |
| `test_reply_chat_known_random_string_context_enqueues_message` | Confirms templates referencing built-in random string tables enqueue deterministic console entries. |
| `test_reply_chat_expands_named_random_table` | Verifies parsed random-string tables can be referenced by reply templates and matched back through retail `ESCAPE_CHAR` construction markers. |
| `test_reply_chat_expands_nested_random_table` | Verifies constructor expansion repeats when a random-table entry produces another random reference. |
| `test_initial_chat_weighted_synonym_runs_each_expansion_pass` | Pins Q3 `BotExpandChatMessage` timing so weighted synonyms run before the next random expansion pass. |
| `test_initial_chat_applies_weighted_synonym_context` | Covers the Q3 post-construction weighted synonym pass for initial chats with a `CONTEXT_*` mask. |
| `test_initial_chat_rejects_float_message_component` | Pins Q3 `BotLoadChatMessage` integer-only numeric message components for named initial chat blocks. |
| `test_initial_chat_rejects_missing_message_component_comma` | Ensures initial-chat message components require Q3 comma delimiters before the semicolon. |
| `test_reply_chat_rejects_float_message_component` | Ensures reply response templates reject float numeric components instead of truncating them into `\vN\` references. |
| `test_reply_chat_rejects_missing_message_component_comma` | Ensures reply response templates require Q3 comma delimiters between message components. |
| `test_reply_chat_rejects_float_pattern_variable` | Ensures parenthesized reply-key variables follow Q3 `BotLoadMatchPieces` integer-token requirements. |
| `test_match_template_rejects_float_variable` | Ensures `match.c` template variables reject non-integer numeric pieces during load. |
| `test_match_template_rejects_float_message_type` | Ensures match-template message type metadata rejects float numeric tokens. |
| `test_match_template_rejects_float_message_subtype` | Ensures match-template subtype metadata rejects float numeric tokens. |
| `test_match_template_requires_message_subtype` | Ensures match-template metadata always supplies the retail comma-delimited subtype. |
| `test_match_template_requires_metadata_closing_tokens` | Ensures match-template metadata closes with exactly `);` and rejects extra trailing tokens. |
| `test_match_template_rejects_float_context_label` | Ensures numeric match context block labels remain integer-only. |
| `test_match_template_rejects_out_of_range_variable` | Ensures `match.c` template variables reject indices outside the capture table. |
| `test_match_template_rejects_adjacent_variables` | Ensures `match.c` template variables cannot appear adjacent without an intervening non-empty string. |
| `test_match_template_rejects_missing_piece_comma` | Ensures `match.c` template pieces require Q3 comma delimiters before `=`. |
| `test_reply_chat_rejects_out_of_range_pattern_variable` | Ensures parenthesized reply-key variables reject indices outside the capture table. |
| `test_reply_chat_rejects_adjacent_pattern_variables` | Ensures parenthesized reply-key variables follow Q3 adjacent-variable rejection. |
| `test_reply_chat_rejects_missing_pattern_piece_comma` | Ensures parenthesized reply-key match pieces require Q3 comma delimiters. |
| `test_reply_chat_rejects_empty_pattern_key` | Ensures an immediately closed parenthesized reply key is rejected like Q3 `BotLoadMatchPieces`. |
| `test_reply_chat_rejects_empty_alternative_between_variables` | Pins Q3's empty-string alternative handling, where an optional string piece does not clear adjacent-variable rejection. |
| `test_reply_chat_captures_key_variable_after_string_alternative` | Exercises Q3 `BotLoadMatchPieces` string alternatives inside parenthesized reply keys and verifies the chosen alternative still feeds captured response variables. |
| `test_reply_chat_empty_string_piece_closes_variable_capture` | Ensures a single empty string match piece closes a preceding variable capture instead of being dropped as an absent literal. |
| `test_reply_chat_split_vcontext_uses_q3_word_separators` | Pins reply-variable synonym replacement to Q3 `StringContainsWord` separators. |
| `test_reply_chat_rejects_empty_key_list` | Ensures reply blocks require at least one key before the closing bracket. |
| `test_reply_chat_accepts_missing_key_commas` | Pins Q3's optional comma handling between adjacent reply keys. |
| `test_reply_chat_rejects_unquoted_plain_key` | Ensures non-special reply keys must be quoted string tokens. |
| `test_reply_chat_rejects_unquoted_botname_list_entry` | Ensures `<...>` bot-name key lists require quoted string entries. |
| `test_setup_chat_ai_match_string_alternatives_capture_variables` | Verifies setup-loaded `match.c` string alternatives flow through `BotFindMatch` and preserve public match-variable spans. |
| `test_reply_chat_unknown_random_string_context_logs_error` | Validates that unknown random string identifiers surface a `BotConstructChat` error and leave the console queue untouched. |
| `test_include_path_too_long_is_rejected` | Bypasses the chat layer and exercises the precompiler diagnostics for oversized `#include` fragments. |

A small helper, `drain_console`, clears any queued chat messages between steps so
subsequent checks read only the newly generated entries.

## Stub helpers (`test_bot_chat_stubs.c`)

The stub translation unit provides the bare minimum implementation surface area
expected by the linked botlib sources:

- `BotLib_Print`, `BotLib_LogWrite`, and `BotLib_Error` record messages to an
  in-memory buffer and mirror them to `stderr`. The getters allow tests to assert
  on the latest log entry without depending on stdout capture.
- On Windows, the harness routes debug CRT asserts to `stderr` so failing tests
  terminate through CTest instead of opening a modal runtime dialog.
- Memory helpers (`GetMemory`, `GetClearedMemory`, `FreeMemory`) forward to the
  C runtime. The chat code allocates short-lived buffers via these hooks, so any
  future replacements must remain compatible with malloc-style semantics.
- Engine-facing shims such as `BotInterface_GetImportTable`, `LibVarValue`, and
  `BotLib_LocateAssetRoot` provide deterministic defaults for the isolated
  chat loader. `BotLib_LocateAssetRoot` returns `BOT_ASSET_ROOT` when available
  so retail `#include` directives resolve exactly as they do from the asset
  tree.

## Conventions for new tests

- Use `BotAllocChatState` / `BotFreeChatState` for each scenario. Sharing a
  single chat state across cases introduces order dependence and makes failures
  difficult to diagnose.
- Prefer direct `assert` checks for clarity. When logging is required, reset the
  stub recorder before invoking the code under test so your assertions are
  deterministic.
- Keep asset usage explicit: hardcode relative paths based on `BOT_ASSET_ROOT`
  and document any additional files required inside this README to simplify CI
  configuration.
- New helpers or fixtures should live in `test_bot_chat_stubs.c` when they are
  expected to be shared. That keeps `test_bot_chat.c` focused on behavior rather
  than plumbing.
