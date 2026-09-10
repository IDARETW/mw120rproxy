# Numeric developer/error-code map - newer game-test source with 1.20 replay cross-check

This is the corrected report. The primary result is the actual numeric code-to-description mapping, ordered by evidence strength for the newer game-test target. The previous 81-row `Sys_Error` list is retained below only as a caller/sink appendix; those rows do not contain the numerical IDs.

## Evidence boundary

- Game-test extracted source: `E:\Users\Austin\Documents\mw124\IDA_Extracts\dev_db\extracted_raw_code\1-game_test_exe\_runtime_cpp` (52,439 exported `.cpp` files).
- Game-test analysis DB: `E:\Users\Austin\Documents\mw124\IDA_Extracts\dev_db\1-game_test.exe.analysis.db` (functions rows: 0; file-info rows: 1). The exported source tree is therefore the usable game-test evidence.
- Replay analysis DB: `E:\IW8\Binaries\1.20-replay\game_dx12_ship_replay.exe.sqlite` (82,967 functions, 558,472 constants, 6,041,905 instructions, 387,035 callgraph rows). This is an older build than the game-test executable and is used only as a historical/cross-check source.
- No raw `game_dx12_ship_replay.exe` was present in `E:\IW8\Binaries\1.20-replay`; replay results are from the existing SQLite/IDA-derived export, not a new raw-binary scan.
- The replay generic sink is `sub_1413FF7A0` (RVA `0x13FF7A0`), whose literals are `"Sys_Error: %s"` and `"DEV ERROR"`. The numeric values are supplied by callers or by the error-reporting path, not by this sink itself. The older replay DB does not retain a complete `DEV ERROR %u` caller table.

## Executive result

- The newer game-test source export contains 0 literal `DEV ERROR` strings, 0 `DevAbortVerify` references, 0 fastfile-description literals, and no direct numeric `Com_Error_impl`/`Sys_Error` callsite bodies. Its exported error functions are routing thunks; the only exported `Scr_Error` call forwards a variable `ComErrorCode uniqueErrorCode`.
- The older replay DB directly exposes five numeric fastfile/DB error IDs, `6031` through `6035`, including their exact descriptions and the originating functions/RVAs. Their helper’s recovered text is `DB ERROR`, so they are retained as historical cross-checks, not treated as newer game-test proof and not silently claimed as generic `DEV ERROR %u` records.
- ZeroProxy contributes a separate source-level `dev_error_list` for its IW8 client. It adds 33 previously missing generic `DEV ERROR` IDs (`2276`–`2281`, `2538`–`2540`, `2545`–`2551`, `2554`–`2559`, `2560`–`2566`, and `2568`–`2571`) with exact replacement text, and independently lists the fastfile family through `6042`.
- The ZeroProxy source map and the replay assembly disagree on the labels for `6031` and `6032`; both records are retained below with the conflict called out. The replay assembly is the stronger build-local evidence for the `1.20-replay` binary.
- The companion IW8 static traces add exact fastfile IDs `6036`, `6037`, `6039`, `6040`, `6041`, and `6042`, plus exact non-fastfile IDs such as `393`, `401`, `5433`, `5536`, `5612`, and `5617`. Those are marked as cross-build/companion evidence because their callsites are not present in the current replay SQLite export.
- The old 81-row sink-callsite appendix remains useful for locating dynamic error descriptions, but it must not be read as an ID map.

## Newer game-test source audit

The extracted source tree contains 52,439 `.cpp` files. A source-level sweep for the numeric developer-error path found no literal `DEV ERROR`, no `DevAbortVerify`, no `Fastfile is ...`/`XFILE_...` error-description strings, and no direct numeric call to the exported `Com_Error_impl`, `Sys_Error`, or `Com_ErrorAbort` thunks. The relevant exported bodies are wrappers such as `00009246_Com_Error_impl.cpp`, `00009D82_sub_79C76.cpp`, and `000034E5_Com_ErrorAbort.cpp`.

The only direct script-error call in the export is `000046F8_sub_37600.cpp`, which has the signature `sub_37600(ComErrorCode uniqueErrorCode, scrContext_t *, const char *)` and forwards that variable to `Scr_Error`. This proves the newer source has the typed error-code ABI, but the export does not contain the enum values or the callsite descriptions. Therefore the report does not invent newer-build numeric mappings from the older replay DB: rows marked replay or companion are explicitly labeled below, and the game-test source itself is recorded as unresolved for numeric enumeration.

## Older 1.20 replay numeric DB/fastfile mappings

These are exact mappings for the older replay build only: its pseudocode and assembly contain the literal numeric arguments to `sub_1411A8320`, plus the corresponding message format. They are useful cross-checks, but they are not newer game-test-source mappings.

| Code | Description | Origin | Evidence |
| ---: | --- | --- | --- |
| 6031 | `Fastfile is out of date (XFILE_FF_HEADER_VERSION %d, expecting %d)` | `sub_1411A7750`, RVA `0x11A7750`; call expression `6032 - (v5 < 0xB)` when `v5 < 0xB` | Replay SQLite function 32153 pseudocode and assembly |
| 6032 | `Fastfile is newer than client executable (XFILE_FF_HEADER_VERSION %d, expecting %d)` | `sub_1411A7750`, RVA `0x11A7750`; call expression `6032 - (v5 < 0xB)` when `v5 >= 0xB` | Replay SQLite function 32153 pseudocode and assembly |
| 6033 | `FF magic was invalid` | `sub_1411A7750`, RVA `0x11A7750` | Replay SQLite function 32153 pseudocode and assembly; immediate `0x1791` |
| 6034 | `%s is out of date (XFILE_VERSION %d, expecting %d)` | `sub_1411A7840`, RVA `0x11A7840` | Replay SQLite function 32154 pseudocode and assembly; immediate `0x1792` |
| 6035 | `%s is newer than client executable (XFILE_VERSION %d, expecting %d)` | `sub_1411A7840`, RVA `0x11A7840` | Replay SQLite function 32154 pseudocode and assembly; immediate `0x1793` |

The common helper is `sub_1411A8320` (RVA `0x11A8320` in this replay image). Its exported body formats TLS state and emits `DB ERROR: File: '%s', Text: '%s'`; the five IDs above are passed into that reporting path by the two loader validators.

## ZeroProxy exact generic `DEV ERROR` map

`E:\Users\Austin\Documents\mw124\zeroproxy-new\src\client-iw8\component\dev_errors.cpp:11-55` contains an explicit `std::unordered_map<int, std::wstring>` and hooks `MessageBoxW`, extracting `DEV ERROR (\\d+)` from the game text before replacing it with `[code] description`. The IW8 game setup in the same checkout explicitly supports `1.20.4` and `1.20.4-replay`, so this is relevant companion source evidence rather than a generic error-code list from another title. It is not the newer game-test source export itself, so it is retained as corroboration/coverage and cannot establish that the newer executable has the identical table.

The following entries are new numeric mappings supplied by that source map. Gaps in the number ranges are intentional: no description was present for them in the saved map.

The saved generic map leaves `2541`–`2544`, `2552`–`2553`, and `2567` unassigned. The fastfile family leaves `6038` unassigned; no description or verified callsite for it was found in the newer source export, ZeroProxy map, or older replay evidence.

| Code | Exact description | Evidence |
| ---: | --- | --- |
| 2276 | `Must be called on a client entity.` | ZeroProxy `dev_errors.cpp` |
| 2277 | `Non local players are not supported for this call.` | ZeroProxy `dev_errors.cpp` |
| 2278 | `Must be called on a client entity.` | ZeroProxy `dev_errors.cpp` |
| 2279 | `Non local players are not supported for this call.` | ZeroProxy `dev_errors.cpp` |
| 2280 | `Must be called on a client entity.` | ZeroProxy `dev_errors.cpp` |
| 2281 | `Non local players are not supported for this call.` | ZeroProxy `dev_errors.cpp` |
| 2538 | `divide by 0` | ZeroProxy `dev_errors.cpp` |
| 2539 | `%g out of range` | ZeroProxy `dev_errors.cpp` |
| 2540 | `%g out of range` | ZeroProxy `dev_errors.cpp` |
| 2545 | `wrong number of arguments to vectornormalize!` | ZeroProxy `dev_errors.cpp` |
| 2546 | `Wrong number of arguments to GenerateAxisFromForwardVector` | ZeroProxy `dev_errors.cpp` |
| 2547 | `Wrong number of arguments to GenerateAxisFromUpVector` | ZeroProxy `dev_errors.cpp` |
| 2548 | `wrong number of arguments to vectortoyaw!` | ZeroProxy `dev_errors.cpp` |
| 2549 | `wrong number of arguments to vectortopitch!` | ZeroProxy `dev_errors.cpp` |
| 2550 | `wrong number of arguments to vectorlerp` | ZeroProxy `dev_errors.cpp` |
| 2551 | `Zero length vector was passed as first parameter!` | ZeroProxy `dev_errors.cpp` |
| 2554 | `Incorrect number of parameters for ProjectileIntercept.` | ZeroProxy `dev_errors.cpp` |
| 2555 | `wrong number of arguments to anglestoup!` | ZeroProxy `dev_errors.cpp` |
| 2556 | `Wrong number of arguments to AngleLerpQuat( <startAngles>, <end>, <maxAngleDelta> )` | ZeroProxy `dev_errors.cpp` |
| 2557 | `Wrong number of arguments to AngleLerpQuatFrac( <startAngles>, <end>, <fraction> )` | ZeroProxy `dev_errors.cpp` |
| 2558 | `wrong number of arguments to axistoangles` | ZeroProxy `dev_errors.cpp` |
| 2559 | `wrong number of arguments to vectortoangle!` | ZeroProxy `dev_errors.cpp` |
| 2560 | `string too long` | ZeroProxy `dev_errors.cpp` |
| 2561 | `string too long` | ZeroProxy `dev_errors.cpp` |
| 2562 | `string too long` | ZeroProxy `dev_errors.cpp` |
| 2563 | `assert fail` | ZeroProxy `dev_errors.cpp` |
| 2564 | `assert fail: %s` | ZeroProxy `dev_errors.cpp` |
| 2565 | `assert fail: %s` | ZeroProxy `dev_errors.cpp` |
| 2566 | `USAGE: tableLookup( filename, searchColumnNum, searchValue, returnValueColumnNum )\\n` | ZeroProxy `dev_errors.cpp` |
| 2568 | `USAGE: tableLookupByRow( filename, rowNum, returnValueColumnNum )\\n` | ZeroProxy `dev_errors.cpp` |
| 2569 | `USAGE: tableLookup( filename, searchColumnNum, searchValue, returnValueColumnNum )\\n` | ZeroProxy `dev_errors.cpp` |
| 2570 | `USAGE: tableLookupByRow( filename, rowNum, returnValueColumnNum )\\n` | ZeroProxy `dev_errors.cpp` |
| 2571 | `USAGE: tableLookupRowNum( filename, searchColumnNum, searchValue )\\n` | ZeroProxy `dev_errors.cpp` |

The same ZeroProxy map includes these fastfile entries already represented elsewhere in this report:

| Code | ZeroProxy source-map text | Relation to replay evidence |
| ---: | --- | --- |
| 6031 | `Fastfile is newer than client executable (XFILE_FF_HEADER_VERSION %d, expecting %d)` | Conflicts with the replay assembly, which passes `6031` with the `out of date` text |
| 6032 | `Fastfile is out of date (XFILE_FF_HEADER_VERSION %d, expecting %d)` | Conflicts with the replay assembly, which passes `6032` with the `newer` text |
| 6033 | `FF magic was invalid` | Agrees with replay function `sub_1411A7750` |
| 6034 | `%s is out of date (XFILE_VERSION %d, expecting %d)` | Agrees with replay function `sub_1411A7840` |
| 6035 | `%s is newer than client executable (XFILE_VERSION %d, expecting %d)` | Agrees with replay function `sub_1411A7840` |
| 6036 | `Unable to open` | Supplies the short exact text behind the companion `unable to open fastfile` description |
| 6037 | `Unable to read header` | Supplies the short exact text behind the companion base-header description |
| 6039 | `XFILE_FD_HEADER_VERSION mismatch. Has '%d', wanted '%d'` | Exact source-map wording for the companion header check |
| 6040 | `XFILE_DIFF_VERSION version mismatch. Has '%d', wanted '%d'` | Exact source-map wording for the companion diff check |
| 6041 | `Patch header data mismatch between current and prior patch` | Exact source-map wording for the companion patch check |
| 6042 | `Patch header data mismatch between base FF '%s' and current patch` | Exact source-map wording for the companion patch check |

The `6031`/`6032` conflict is not silently normalized: the replay function’s assembly has `6032 - (version < 0xB)`, with the out-of-date string selected when the fastfile version is lower and the newer string selected when it is greater/equal. ZeroProxy’s saved replacement table has those two numeric keys reversed. `6033`–`6035` match exactly. `6038` remains unassigned in both inventories.

## ZeroProxy saved diagnostic dvars

`E:\Users\Austin\Documents\mw124\zeroproxy-new\src\client-iw8\resources\dvar_list.json` contains 6,743 saved IW8 dvars. A targeted error/assert/crash/validation/replay scan matched 90 names, of which 42 have descriptions. These are diagnostic controls and state variables, not numeric `DEV ERROR` mappings; no numeric code should be inferred from a dvar name or its obfuscated token.

The highest-value error-path entries are:

| Dvar | Saved obfuscated token | Description |
| --- | --- | --- |
| `LUI_MemErrorsFatal` | `LQKQOLMONS` | Out of memory errors cause drops when true, reinits the UI system if false |
| `LUI_WorkerCmdGC` | `MTSTKTSKQ` | Dev-only flag to enable/disable LUI workerCmd GC thread |
| `OER_BB_errorcode_send_counter_reset_counter` | `NMLMSLRNPN` | The time in milliseconds after which all the error code send counters will be reset |
| `QuitOnError` | `MRSKLLMNKP` | No description saved |
| `com_enableCrashReportOnSysError` | `NRQOOOOTT` | Create a crash dump when the game triggers a SysError |
| `com_errorMessage` | `OMSSQKRMLQ` | Most recent error message |
| `com_errorTitle` | `NSNNTPQPNT` | Title of the most recent error message |
| `con_errormessagetime` | `LLQROLOMQM` | Onscreen time for error messages in seconds |
| `db_discErrorSystemDialog` | `LOOOMOTSSS` | Display a system error dialog when we ecounter a disc read error |
| `db_discErrorVerbose` | `NLKNQNSLTL` | Killswitch for verbose reporting of filesystem errors |
| `db_errorOnMissingTechsetZones` | `QLSTMRPTQ` | When enabled, the game will error when attempting to load a missing techset zone. |
| `db_forcelogprints` | `MPMPPLMQSR` | A number of DB prints don't hit the logfile, on 'logfile 2' runs. This forces them to be logged. |
| `data_validation_allow_assert` | `MTLNMNPSNK` | No description saved |
| `data_validation_allow_drop` | `MPMRQMSRML` | No description saved |
| `data_validation_debug_force_log` | `MSNPRSSQPO` | No description saved |
| `data_validation_debug_force_validate_fail` | `LQNSORQPOP` | No description saved |
| `data_validation_debug_log` | `PPNPOSRSO` | No description saved |
| `stream_decompressErrorMode` | `OMQONNMQRS` | Determines how the game responds when decompression of xpak data fails for whatever reason. |
| `stream_readLog` | `LMSLNRROMS` | No description saved |
| `suppressLoadErrors` | `NTOSQTRQNN` | No description saved |
| `sv_error_on_baseline_failure` | `NSOTRTRKNS` | Throw an error if the const baseline data is invalid. |
| `replay_assert_script_callstack` | `QQLPPRPSO` | No description saved |
| `replay_asserts` | `QKKNSLRMK` | No description saved |
| `replay_autosaveOnError` | `NSLRNLQNQM` | No description saved |
| `replay_loadAssertData` | `MOPPMQMSP` | No description saved |
| `retail_dev` | `MSNOPOLSTO` | No description saved |
| `ui_missingMapName` | `LNKPRSTSOL` | Name of map to show in missing content error |
| `unattendedAllowDropErrors` | `NLOOKNQTMQ` | No description saved |
| `useBattleNetInDevmap` | `MOKLKTPOTT` | If true, will do auth with Battle.Net while devmapping. |

Other saved dvars in the targeted set cover online error reporting, DObj/assert behavior, crash-report archival, render validation, and replay autosave. They are retained in the source JSON for later cross-referencing, but they do not add a code-to-description row until a numeric callsite or enum is found.

## ZeroProxy numeric values that are not `DEV ERROR <decimal>` IDs

ZeroProxy also has two crash-handler exception values labeled `Manually triggered dev error`:

| Hex | Decimal | Description | Classification |
| ---: | ---: | --- | --- |
| `0x1337` | 4919 | `Manually triggered dev error` | ZeroProxy crash-handler exception code; not a game `DEV ERROR 4919` mapping |
| `0x1338` | 4920 | `Manually triggered dev error` | ZeroProxy crash-handler exception code; not a game `DEV ERROR 4920` mapping |

They are kept separate from the map because the source uses them as Windows exception/termination values, not as the decimal ID parsed by the `DEV ERROR (\\d+)` replacement hook.

## Companion `DEV ERROR %u` numeric observations, kept separate from newer game-test proof

The entries below are generic `DEV ERROR %u` codes or exact numeric error paths in the project’s companion IW8 loading/debugging records. They are not directly recoverable from the newer game-test source export or, in most cases, from the older replay SQLite function records. They are included because they are useful mappings, with their provenance, build scope, and limits shown explicitly.

| Code | Description / meaning | Origin or observation | Evidence status |
| ---: | --- | --- | --- |
| 16 | Benign map-load developer error observed before the fatal path; exact text/code-generation call not recovered | `iw8_zone_linker_research.md`: `MpPatch` print path `sub_140001F6D370` | Observed runtime note; description unresolved |
| 393 | Connection-state gate failure; tied to the `clientUIActives[0].connectionState` global | `codknowledge.md` connection-state notes | Project research note; not a current replay-DB callsite |
| 5413 | Omnvar loader/interner category-cap overflow; the notes identify an 80-per-category hard cap. In the recorded match-start case this was collateral after a readiness timeout | Omnvar loader `sub_14752A0` → interner `sub_1475610` | Observed runtime/minidump research note; exact replay-DB numeric callsite absent |
| 5433 | File-handle slot exhaustion in the 256-slot table | `sub_140001900550`; note says `Com_Error("DEV ERROR %u", 5433)` | Static/research note; address/base is not the current replay SQLite’s `sub_141...` image |
| 5612 | Fastfile `xfileVersion` does not match the expected value for `transientFileType` on the DCache path | `sub_140001E3FDB0`; expected type-0 value is `0x100C` | Static/live research note; address/base is not the current replay SQLite’s `sub_141...` image |
| 664 | Unsupported/unknown fastfile content-block compression type | `fastfile_research/cold-research/CONTENT-STREAM-FIX.md` and `evidence/R1-format.md` | Exact numeric fallback in companion retail decompressor trace; address/base is not the current replay SQLite’s `sub_141...` image |
| 394 | DDL state/value update failed | `ida_ddl_retail.txt`: failed DDL operation after `LiveStorage_InitializeDDLStateForStatsGroup` | Exact numeric call in companion IDA pseudocode; description is the failing operation, not a recovered user-facing string |
| 398 | DDL result/state was outside the accepted range | `ida_ddl_retail.txt`: `v9 > 3` after the DDL operation | Exact numeric call in companion IDA pseudocode; condition-derived description |
| 401 | Required MP DDL asset was missing/not mounted (`ddl/mp/playerdata.ddl`, `cpdata.ddl`, `commondata.ddl`, `rankedloadouts.ddl`, `privateloadouts.ddl`, or `nongamedata.ddl`) | `ida_ddl_dump.txt` / 1.24 Build notes | Exact numeric call in companion IDA pseudocode |
| 546 | DObj/model attachment slot capacity was exhausted (`*a12 >= a11`) | `ida_attach2.txt` | Exact numeric call in companion IDA pseudocode; condition-derived description |
| 5536 | DCache read failed during the fastfile content read | `fastfile_research/cold-research/evidence/CSR3-seam.md` | Exact numeric call in companion retail trace |
| 5617 (`0x15F1`) | Pending DB-zone queue exceeded its maximum of 1,928 entries | `fastfile_research/cold-research/evidence/R2-loadpath.md` | Exact numeric call/constant in companion retail trace |
| 6036 | `unable to open fastfile` | Fastfile-family match-start load, recorded for `ww_mp_test.ff` | Observed runtime note in `iw8_zone_linker_research.md`; exact numeric callsite absent from current replay SQLite |
| 6037 | Unable to read the base fastfile header | `DB_ReadFastfileHeaderData`: short/failing read of the base header | Exact numeric call in companion retail trace |
| 6039 | `XFILE_FD_HEADER_VERSION` mismatch (the diff/patch header version was not `6`) | `.fp`/`.fc` companion header validation | Exact numeric call/condition in companion retail trace |
| 6040 | `XFILE_DIFF_VERSION` mismatch (the diff version was not `4`) | `.fp`/`.fc` companion header validation | Exact numeric call/condition in companion retail trace |
| 6041 | Embedded patch/base header did not match the base fastfile header | `.fp`/`.fc` embedded-header comparison | Exact numeric call/condition in companion retail trace |
| 6042 | Embedded patch/new header did not match the base/top fastfile header | `.fp`/`.fc` embedded-header comparison | Exact numeric call/condition in companion retail trace |

The current replay DB still has no direct `6036`–`6042` callsite series; those rows are companion static/observed evidence. `6038` is not assigned because no verified call or description was found. Likewise, the `16xxx` observation in the notes is a truncated on-screen range, not a recoverable code.

## Numeric script `ComErrorCode` values are a separate class

The older replay DB also contains thousands of numeric arguments to `Scr_Error`/`sub_141325FB0`. Those are script `ComErrorCode` IDs, not automatically the same thing as the generic `DEV ERROR` sink’s code. For example, the table-lookup family exposes these exact script IDs:

| Code | Description | Origin |
| ---: | --- | --- |
| 6330 | `Table name for lookup cannot be empty` | `sub_140CE3620`, RVA `0xCE3620` |
| 6331 | `Table name for lookup cannot be empty` | `BGScr_TableLookupRowNum`, RVA `0xCE2800` |
| 6332 | `Table name for lookup cannot be empty` | `BGScr_TableLookupGetNumRows`, RVA `0xCE2960` |
| 6333 | `Table name for lookup cannot be empty` | `BGScr_TableLookupGetNumCols`, RVA `0xCE2A20` |
| 6334 | `Table name for lookup cannot be empty` | `BGScr_TableLookupRowNum_StartFromRow`, RVA `0xCE2AE0` |
| 6335 | `Table name for lookup cannot be empty` | `BGScr_TableLookupRowNum_ReverseFromRow`, RVA `0xCE2BE0` |
| 6336 | `Table name for lookup cannot be empty` | `BGScr_TableLookupInternal`, RVA `0xCE36C0` |

These are included to prevent a category mistake: they are genuine numerical error IDs in the replay binary, but the current evidence does not prove that every `Scr_Error` ID is rendered with the `DEV ERROR` prefix. They should not be merged into the fastfile `6031`–`6035` table without a runtime or richer error-enum mapping.

## Game-test extracted source

| Entry point / role | Exported source file(s) | Numeric mapping result |
| --- | --- | --- |
| Sys_Error sink thunk | `00009D82_sub_79C76.cpp` | Calls `Sys_Error(error)`; no numeric ID in the exported body |
| Com_Error_impl wrapper | `00009246_Com_Error_impl.cpp` | Forwards to the mangled implementation; no numeric ID in the exported body |
| Com_ErrorAbort wrapper | `000034E5_Com_ErrorAbort.cpp` | Forwards to the mangled implementation; no numeric ID in the exported body |
| CoreAssert wrappers | `00000276_j_CoreAssert_Handler_AssertTypeAssert.cpp`, `0000C0A4_CoreAssert_Handler.cpp` | Assertion routing only; no DEV ERROR number in the exported body |
| Live/LUI/Core_PrintError wrappers | `00007C96_Live_ThrowError.cpp`, `0000880F_Live_ThrowError_DontDeclineInvite.cpp`, `00000C15_Core_PrintError_Unchecked.cpp`, `00004B3A_LUI_ReportErrorWithInfo.cpp`, `00005D54_LUI_ReportError.cpp` | Error-routing ABI only; no numeric DEV ERROR table in the exported source subset |

## Replay literal DEV ERROR matches

These are sink/context records, not numeric mappings. They are retained here to show why the previous report’s 81 rows could not answer the numerical-ID question.

| Function | RVA | Recovered literals | Interpretation |
| --- | ---: | --- | --- |
| `sub_1413FF7A0` (ID 40487) | `0x13FF7A0` | `Sys_Error: %s`; `DEV ERROR` | Generic developer-error banner/sink. Numeric IDs arrive from callers or another reporter. |
| `sub_141B34D10` (ID 55673) | `0x1B34D10` | FavoriteFriends labels and `...dev error %s` | Online subsystem text; not a numeric DEV ERROR record. |
| `sub_141B35CF0` (ID 55687) | `0x1B35CF0` | FavoriteFriends labels and `...dev error %s` | Online subsystem text; not a numeric DEV ERROR record. |

## All replay Sys_Error callsites

This is a non-numeric sink appendix. The callgraph proves that these functions reach the generic `Sys_Error`/`DEV ERROR` sink, but the current export does not recover a numerical developer-error argument for each row. Do not treat the row number (`1` through `81`) as an error code.

Each row is a function that the SQLite callgraph marks as directly calling sub_1413FF7A0. The RVA is the function start RVA, not the exact call instruction RVA.

| # | Function / RVA | Recovered error description(s) | Unresolved or dynamic argument(s) | Evidence / resolution |
| ---: | --- | --- | --- | --- |
| 1 | sub_140CC9550; RVA 0xCC9550; function id 16241 | Failed to resume recipe save thread; Failed to create recipe save thread | - | 2 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 2 | ?SND_GetVfCurveById@@YAPEBUSndCurve@@I@Z_0; RVA 0xCCA5D0; function id 93336 | Failed to suspend recipe save thread | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 3 | BG_Omnvar_AddStringToStringBuffer; RVA 0xCD5490; function id 86067 | Reached omnvar string buffer limit. Omnvar names (or default values of string omnvars) are too long. | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 4 | sub_140CD5F40; RVA 0xCD5F40; function id 16336 | Related function literal(s), not proven as the exact argument: Registered %d omnvars from %s ( Error ). Checksum so far is 0x%x. (%i client, %i game, %i archived, %i pmove); Omnvar filename '%s' is not a valid asset name. Possibly bad game mode or game type?; Reached omnvar string buffer limit. Omnvar names (or default values of string omnvars) are too long. | &unk_1423C58D8 | 1 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 5 | sub_140CD6470; RVA 0xCD6470; function id 16337 | No literal description recovered from this function record. | &unk_1423C5248; &unk_1423C5270; &unk_1423C52B0; &unk_1423C52F0; &unk_1423C5330; &unk_1423C5390; &unk_1423C5400; &unk_1423C5460; &unk_1423C54A8 | 9 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 6 | Omnvar_RegisterFlagsFromStringTable; RVA 0xCD67C0; function id 89627 | Attempting to set the interpolate flag for an omnvar type which does not support it!; Attempting to set the PMOVE flag for an omnvar type that interpolate, this is not allowed.; Attempting to set the PMOVE flag for an omnvar type that is not float, this is not allowed.; Attempting to set the Network Time flag for an omnvar that is not either flagged notify lui or notify csc, this is not allowed.; Unrecognized scope for omnvar "%s", scope '%c'; Attempting to set the Client Spectate flag for an omnvar that is not flagged as client scope, this is not allowed since client spectate is a type of client scoped omnvar.; Attempting to set the Client Spectate flag for an omnvar that is flagged as archived, this is not allowed since it will already appear to a spectator. | - | 7 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 7 | sub_140D78F00; RVA 0xD78F00; function id 17898 | No literal description recovered from this function record. | byte_1423CED60; byte_1423CEDA0 | 2 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 8 | sub_140D86B70; RVA 0xD86B70; function id 17982 | No literal description recovered from this function record. | byte_1423D16E0 | 1 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 9 | sub_140D86F00; RVA 0xD86F00; function id 17986 | Related function literal(s), not proven as the exact argument: ERROR: Dirty disk: '%s' | byte_1423D17E8; byte_1423D1790 | 2 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 10 | sub_140D8B400; RVA 0xD8B400; function id 18071 | No literal description recovered from this function record. | byte_1423D2288 | 1 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 11 | sub_140D8B440; RVA 0xD8B440; function id 18072 | No literal description recovered from this function record. | byte_1423D22B0 | 1 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 12 | sub_140D8BA60; RVA 0xD8BA60; function id 18076 | Related function literal(s), not proven as the exact argument: tact_vfs_root_error; TACT: Failed to retrieve VFS root : %s (%d); Failed to retrieve TACT VFS root : %s (%d); Failed to start the TACT VFS read thread | call argument not rendered | 1 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 13 | sub_140E55AB0; RVA 0xE55AB0; function id 20923 | Stream_GetNextFileID: Too many open files (%d). | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 14 | sub_140EEEB60; RVA 0xEEEB60; function id 23157 | Exceeded the maximum number of compute descriptors. Increase g_computeDescriptorHeapSizes[%d] to fix.; Exceeded the maximum number of compute descriptors. Increase g_computeDescriptorHeapSizes[%d] to fix blorp. | - | 2 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 15 | sub_140F5DD30; RVA 0xF5DD30; function id 24546 | No literal description recovered from this function record. | byte_1423FBAF8; byte_1423FBB20 | 2 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 16 | sub_140F60180; RVA 0xF60180; function id 24649 | Exceeded limit of deferred released zones | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 17 | sub_140F62C40; RVA 0xF62C40; function id 24908 | DiskFS: Out of file handles (%d) | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 18 | sub_140F63110; RVA 0xF63110; function id 24916 | DiskFS: Out of read contexts (%d); Related function literal(s), not proven as the exact argument: Windows File error '%s' %08x; DiskFile::StartRead call to ReadFileEx error for '%s': %d | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 19 | sub_140F63B20; RVA 0xF63B20; function id 24931 | Related function literal(s), not proven as the exact argument: Ran out of XModelNodes in XModelLookupTable. Please bump NODE_COUNT. | pseudocode/assembly call text not rendered; callgraph marks a direct callee edge | direct callee edge present; call text omitted by export; string argument unresolved in SQLite export |
| 20 | sub_140F662F0; RVA 0xF662F0; function id 24974 | No literal description recovered from this function record. | byte_1423FDE60; byte_1423FDEC0; byte_1423FDF00; byte_1423FDF60; byte_1423FDFD0; byte_1423FE030 | 6 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 21 | sub_140F66790; RVA 0xF66790; function id 24981 | No literal description recovered from this function record. | pseudocode/assembly call text not rendered; callgraph marks a direct callee edge | direct callee edge present; call text omitted by export; string argument unresolved in SQLite export |
| 22 | sub_141036110; RVA 0x1036110; function id 27838 | Related function literal(s), not proven as the exact argument: Unable to allocate memory%s %s Allocator=%s Function=%s %s(%d) | byte_142415D30; off_142415AC0 | 1 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 23 | sub_1410E3590; RVA 0x10E3590; function id 29908 | No literal description recovered from this function record. | byte_142434250; byte_1424342D0 | 2 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 24 | sub_1410E3850; RVA 0x10E3850; function id 29913 | No literal description recovered from this function record. | byte_142433CC0; byte_142433D30; byte_142433FC0; byte_1424341A0 | 4 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 25 | sub_1410E3B00; RVA 0x10E3B00; function id 29914 | No literal description recovered from this function record. | byte_142433DA0; &byte_14273C838; byte_142433E10; byte_142433F70; byte_142433FC0 | 4 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 26 | sub_1410E3D80; RVA 0x10E3D80; function id 29915 | No literal description recovered from this function record. | byte_1424339D0; byte_142433A60; byte_142433B00; byte_142433B70 | 4 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 27 | sub_1410E4300; RVA 0x10E4300; function id 29923 | No literal description recovered from this function record. | byte_142433CC0; byte_142433D30 | 2 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 28 | sub_1411A7750; RVA 0x11A7750; function id 32153 | DCache version mismatch '%s' %d %d; Related function literal(s), not proven as the exact argument: Fastfile is out of date (XFILE_FF_HEADER_VERSION %d, expecting %d); FF magic was invalid | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 29 | sub_1411A7840; RVA 0x11A7840; function id 32154 | DCache version mismatch '%s' %d %d; Related function literal(s), not proven as the exact argument: %s is out of date (XFILE_VERSION %d, expecting %d) | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 30 | sub_1411ABED0; RVA 0x11ABED0; function id 32220 | Failed to create database thread; Related function literal(s), not proven as the exact argument: When enabled, the game will error when attempting to load a missing techset zone.; Display a system error dialog when we ecounter a disc read error; Killswitch for verbose reporting of filesystem errors | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 31 | sub_1411AFE20; RVA 0x11AFE20; function id 32292 | MAX_ATTEMPTED_ZONES exceeded | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 32 | sub_141287DA0; RVA 0x1287DA0; function id 35179 | No literal description recovered from this function record. | dynamic caller-provided message format | 1 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 33 | sub_141288690; RVA 0x1288690; function id 35188 | GAME/ERR_SAVEGAME_BAD | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 34 | sub_141294E10; RVA 0x1294E10; function id 35210 | No literal description recovered from this function record. | byte_1424873E0; byte_142487430 | 2 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 35 | sub_1412A66E0; RVA 0x12A66E0; function id 35528 | Failed initialization of DW content server. | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 36 | sub_1412AAC10; RVA 0x12AAC10; function id 35597 | No literal description recovered from this function record. | dynamic caller-provided message format | 1 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 37 | Com_Error_Internal; RVA 0x12AB1C0; function id 84705 | Com_Error(%d): %s; Related function literal(s), not proven as the exact argument: LastComError; error message | byte_14248A9B0; byte_14D38AB70 | 5 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 38 | sub_1412AF040; RVA 0x12AF040; function id 35620 | No literal description recovered from this function record. | dynamic caller-provided message format | 1 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 39 | sub_1412B5260; RVA 0x12B5260; function id 35666 | Failed init after: %s | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 40 | sub_1412B5D60; RVA 0x12B5D60; function id 35685 | No free DObjs (%d exhausted). %s | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 41 | protected: void BgVehicleComponentGoStraightTo::AdjustInputs(bool,bool); RVA 0x12B5E80; function id 90214 | Related function literal(s), not proven as the exact argument: No free DObjs (%d exhausted). %s | pseudocode/assembly call text not rendered; callgraph marks a direct callee edge | direct callee edge present; call text omitted by export; string argument unresolved in SQLite export |
| 42 | sub_1412B6460; RVA 0x12B6460; function id 35699 | No free DObjs (%d exhausted). %s | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 43 | sub_1412C1FA0; RVA 0x12C1FA0; function id 35974 | Failed initialize listener; Failed to create network thread | - | 2 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 44 | sub_141316BD0; RVA 0x1316BD0; function id 37411 | CANONICAL_STRING_BUFFER_SIZE exceeded; Related function literal(s), not proven as the exact argument: MAX_CANONICAL_STRING (%d) exceeded | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 45 | sub_141317930; RVA 0x1317930; function id 37439 | MT_AllocIndex; Related function literal(s), not proven as the exact argument: %s: failed memory allocation of %d bytes for script usage; failed memory allocation for script usage; MT_GetSize: max allocation exceeded | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 46 | MT_Error; RVA 0x1317D30; function id 88041 | Related function literal(s), not proven as the exact argument: %s: failed memory allocation of %d bytes for script usage; failed memory allocation for script usage | byte_1424A8730 | 1 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 47 | sub_1413180E0; RVA 0x13180E0; function id 37445 | No literal description recovered from this function record. | dynamic caller-provided message format | 1 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 48 | sub_141318160; RVA 0x1318160; function id 37446 | No literal description recovered from this function record. | dynamic caller-provided message format | 1 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 49 | IncInParam; RVA 0x1322280; function id 37661 | Internal script stack overflow | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 50 | sub_1413237B0; RVA 0x13237B0; function id 37693 | No literal description recovered from this function record. | dynamic caller-provided message format | 1 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 51 | sub_14132D370; RVA 0x132D370; function id 37802 | Failed to create server thread | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 52 | sub_1413AA780; RVA 0x13AA780; function id 39295 | Related function literal(s), not proven as the exact argument: StreamDecompressor::Error %u %u %u; StreamDecompressor::Job decompression failed with error: %u %u | byte_1424C1A60 | 1 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 53 | sub_1413B3C70; RVA 0x13B3C70; function id 39427 | Stream_FinishItemPartRead: xpak asset verification failed for [key:%I64u]; Stream_FinishItemPartRead: null stored asset key detected; Related function literal(s), not proven as the exact argument: ALLOC FAILED | - | 2 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 54 | sub_1413DA190; RVA 0x13DA190; function id 39806 | fileSysCheck.cfg | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 55 | sub_1413FFFB0; RVA 0x13FFFB0; function id 40498 | No literal description recovered from this function record. | *v4; *v3; *v2 | 3 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 56 | sub_141401030; RVA 0x1401030; function id 40520 | No literal description recovered from this function record. | &byte_14E5C2EA0 | 1 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 57 | sub_1414C87E0; RVA 0x14C87E0; function id 41080 | exceeded maximum number of anim info | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 58 | void FX_Dismemberment_Restore(enum LocalClientNum_t,struct MemoryFile *); RVA 0x150BC60; function id 90258 | GAME/ERR_SAVEGAME_BAD | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 59 | sub_14159D710; RVA 0x159D710; function id 42084 | xpak file does not contain a valid header: %s.; Related function literal(s), not proven as the exact argument: xpak is missing: %s; Error performing initial read from xpak file: %s. Readbytes is %d but expected %d.; xpak version mismatch, got %08x, expected %08x: %s. Ignoring...; no free xpak slots loading pak %s | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 60 | sub_1415A9ED0; RVA 0x15A9ED0; function id 42195 | TOO MANY ANIMATED BONES - check if the total game bone count can be reduced | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 61 | sub_1415C9AC0; RVA 0x15C9AC0; function id 42647 | TOO MANY ANIMATED BONES - check if the total game bone count can be reduced | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 62 | sub_1415C9B70; RVA 0x15C9B70; function id 42648 | TOO MANY ANIMATED BONES - check if the total game bone count can be reduced | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 63 | sub_1415F7BF0; RVA 0x15F7BF0; function id 43393 | No literal description recovered from this function record. | byte_1424F9458; byte_1424F9480 | 2 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 64 | sub_1415F8880; RVA 0x15F8880; function id 43400 | No literal description recovered from this function record. | byte_1424F9C00; qword_14EFBD478 | 1 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 65 | sub_1415F8A60; RVA 0x15F8A60; function id 43401 | No literal description recovered from this function record. | byte_1424F9C50; &unk_14EFBD470 | 1 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 66 | sub_141803550; RVA 0x1803550; function id 48483 | GAME/ERR_SAVEGAME_BAD | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 67 | sub_14184D190; RVA 0x184D190; function id 49053 | The save file has become corrupted (bad checksum.) | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 68 | sub_1418BE0C0; RVA 0x18BE0C0; function id 50420 | Exceeded the maximum number of compute descriptors. Increase g_computeDescriptorHeapSizes[%d] to fix. | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 69 | sub_1418C5CC0; RVA 0x18C5CC0; function id 50519 | Related function literal(s), not proven as the exact argument: Exceeded the maximum number of compute descriptors. Increase g_computeDescriptorHeapSizes to fix. | pseudocode/assembly call text not rendered; callgraph marks a direct callee edge | direct callee edge present; call text omitted by export; string argument unresolved in SQLite export |
| 70 | sub_1418C6900; RVA 0x18C6900; function id 50520 | Exceeded the maximum number of compute descriptors. Increase g_computeDescriptorHeapSizes to fix. | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 71 | sub_14193EE40; RVA 0x193EE40; function id 51456 | DXGI_ERROR_INVALID_CALL; E_FAIL; DXGI_ERROR_REMOTE_OUTOFMEMORY; E_INVALIDARG; DEVICE_REMOVED_UNKOWN; DEVICE_REMOVED_NULL; DX_ERROR_UNKOWN; DXGI_DDI_ERR_UNSUPPORTED; DXGI_DDI_ERR_NONEXCLUSIVE; D3D12_ERROR_ADAPTER_NOT_FOUND; D3D12_ERROR_DRIVER_VERSION_MISMATCH; DXGI_DDI_ERR_WASSTILLDRAWING; DXGI_ERROR_NOT_FOUND; DXGI_ERROR_MORE_DATA; DXGI_ERROR_UNSUPPORTED; DXGI_ERROR_DEVICE_REMOVED; DXGI_ERROR_DEVICE_HUNG; DXGI_ERROR_DEVICE_RESET; DXGI_ERROR_WAS_STILL_DRAWING; DXGI_ERROR_FRAME_STATISTICS_DISJOINT; DXGI_ERROR_GRAPHICS_VIDPN_SOURCE_IN_USE; DXGI_ERROR_DRIVER_INTERNAL_ERROR; DXGI_ERROR_NONEXCLUSIVE; DXGI_ERROR_NOT_CURRENTLY_AVAILABLE; DXGI_ERROR_REMOTE_CLIENT_DISCONNECTED; DXGI_ERROR_MODE_CHANGE_IN_PROGRESS; DXGI_ERROR_ACCESS_LOST; DXGI_ERROR_WAIT_TIMEOUT; DXGI_ERROR_SESSION_DISCONNECTED; DXGI_ERROR_RESTRICT_TO_OUTPUT_STALE; DXGI_ERROR_CANNOT_PROTECT_CONTENT; DXGI_ERROR_ACCESS_DENIED; DXGI_ERROR_NAME_ALREADY_EXISTS; DXGI_ERROR_SDK_COMPONENT_MISSING; DXGI_ERROR_HW_PROTECTION_OUTOFMEMORY; DXGI_ERROR_DYNAMIC_CODE_POLICY_VIOLATION; DXGI_ERROR_NON_COMPOSITED_UI; DXGI_ERROR_CACHE_CORRUPT; DXGI_ERROR_CACHE_FULL; DXGI_ERROR_CACHE_HASH_COLLISION; DXGI_ERROR_ALREADY_EXISTS; Related function literal(s), not proven as the exact argument: ********** a DirectX call failed **********; ********** Error information: %s; DirectX device removed error, reason: %s [0x%08X]; DirectX call failed with error: %s [0x%08X]; DXGI_ERROR_NOT_CURRENT | - | 43 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 72 | sub_14194D5A0; RVA 0x194D5A0; function id 51653 | No literal description recovered from this function record. | byte_142594F28 | 1 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 73 | sub_1419513E0; RVA 0x19513E0; function id 51690 | Exceeded the maximum number of compute descriptors. Increase g_computeDescriptorHeapSizes to fix. | - | 3 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 74 | sub_1419D1E70; RVA 0x19D1E70; function id 52951 | Related function literal(s), not proven as the exact argument: Error while running LUI Main():; Error while running LUI Main(): %s | pseudocode/assembly call text not rendered; callgraph marks a direct callee edge | direct callee edge present; call text omitted by export; string argument unresolved in SQLite export |
| 75 | void LUI_Interface_Panic(void); RVA 0x19D9A00; function id 85801 | LUI Panic during init; Related function literal(s), not proven as the exact argument: LUI: Panic | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 76 | sub_141B53320; RVA 0x1B53320; function id 56082 | No literal description recovered from this function record. | byte_1425CC530 | 1 direct sub_1413FF7A0 call(s) rendered; string argument unresolved in SQLite export |
| 77 | sub_141B5A2A0; RVA 0x1B5A2A0; function id 56227 | Related function literal(s), not proven as the exact argument: SD_Sync failed after %dms (start %d, end %d, current %d). Mixer thread hung? | pseudocode/assembly call text not rendered; callgraph marks a direct callee edge | direct callee edge present; call text omitted by export; string argument unresolved in SQLite export |
| 78 | sub_141B87DE0; RVA 0x1B87DE0; function id 56620 | SD_AD_SpawnThread failed: invalid threadName | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 79 | sub_141BB1BF0; RVA 0x1BB1BF0; function id 57055 | Invalid save file (FX_SYSTEM_VERSION mismatch) | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 80 | sub_141BD36B0; RVA 0x1BD36B0; function id 57366 | Failed to create splash thread | - | 1 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |
| 81 | sub_141BD4ED0; RVA 0x1BD4ED0; function id 57376 | Failed to create window thread; Failed to create input thread | - | 2 direct sub_1413FF7A0 call(s) rendered; exact literal(s) recovered |

## Reading the unresolved rows

For rows that show byte_... or unk_..., the decompilation passes a binary-resident string object to Sys_Error, but this SQLite export did not materialize the pointed-to string text in the function record. Those symbols are retained verbatim so they can be resolved later against the raw replay image or a richer IDA export. They are not replaced with inferred descriptions.

The direct-literal rows are the usable non-numeric description set from the older replay export. Several descriptions occur in more than one function; each callsite is retained because the caller context and RVA differ. They are not numerical developer-error mappings unless a separate caller argument or enum table proves the code.

## Verification limits

- This is static extraction only. No game launch, replay execution, or runtime error capture was performed.
- The numeric mapping above is limited to exact IDs recovered from literal call arguments or separately documented project evidence. The 81-row appendix is intentionally not promoted to a numeric map.
- No row is labeled as newer-game-test-confirmed unless the newer source export actually contains the numeric argument and description. The current game-test extraction does not; its numeric table must be recovered from a richer raw-binary/IDA export or runtime capture before it can be called complete for that build.
- Generic Com_Error_impl, CoreAssert_Handler, LUI, live-error, and online error labels are listed as routing/context evidence, not silently reclassified as numeric developer-error records.
