# Developer errors

| Code | Reason |
| ---: | --- |
| 0 | CREATEFX_MISSING_SOUND_BUFFER_LENGTH failed to allocate. / CREATEFX_MISSING_SOUND_COUNT_MAX failed to allocate. / BG_INFILS_MAX_COUNTS(%u,%u) hit / Omnvar '%s' not registered! / Map %s has visdistdata, but is not a MP map. Visdistdata isn't used by CP or SP. |
| 2 | Could not query rumble asset '%s' because it was not registered and loaded. |
| 3 | Couldn't register shell shock '%s' -- see console |
| 5 | Cmd_LoadNotificationList: NotifyCount %i out of bounds. |
| 6 | Cmd_SaveNotifications: Player Count %i out of bounds. |
| 7 | filepath '%s%s%s' is longer than %i characters |
| 9 | Info_ValueForKey: oversize key %zd |
| 10 | Info_ValueForKey: oversize value %zd |
| 11 | Info_RemoveKey: oversize infostring |
| 12 | Info_RemoveKey_Big: oversize infostring |
| 13 | Bad field type %i |
| 14 | NetConstStrings: Sync Error: %s. Config string index %i is constant, should not be able to be set to '%s'. |
| 15 | NetConstStrings: Load Error: %s. See console log. |
| 16 | Missing NetConstStrings asset '%s' for current map. Data version is likely out of sync with the game version. |
| 18 | Current transient set is invalid; see output above the call stack for current transients |
| 19 | Transient fastfile name '%s' is incorrect. Transient fastfiles should end with a '%s' suffix. |
| 20 | Not all preloaded zones were consumed with the level load; see output for the outstanding list |
| 21 | Transient sync stall occurred; ensure enough time is left between load and sync calls |
| 22 | Unknown loot item: %d |
| 23 | Too many animations specified in %s. Max:%d |
| 24 | Failed to add animation '%s' from '%s' to the animation list. It might be a repeated anim entry. |
| 25 | Couldn't find shock file [hold_breath.shock] |
| 39 | Too many PhysicsVolumes - max is %i |
| 40 | Too many PhysicsVolumes - max is %i |
| 41 | Too many devgui entries (more than %i) |
| 43 | CL_ParseSP_ParseServerMessage: read past end of server message |
| 44 | CL_ParseSP_ParseServerMessage: Illegible server message %d |
| 68 | Can't precache more assets of type '%s' (max is %d): '%s' |
| 70 | CG_EntityMP_Missile has a weapon that specifies camo %d, but does not have a world model |
| 72 | G_Spawner_SetScriptVariable provided a non vec3 vector: %s |
| 73 | Cloth Asset %s includes a user display buffer definition called %s - this is not currently supported |
| 74 | Cloth Asset %s includes a user static buffer definition called %s - this is not currently supported |
| 75 | Bone %s could not be found in DObj with model %s |
| 76 | Cloth Asset %s has a cloth instance that needs more than CLOTH_MAX_RUNTIME_MEMORY(%i) bytes of runtime memory. If you need more, talk to engineering, but this is usually indicative of an overly expensive asset |
| 77 | Unknown weapon type %i for %s |
| 78 | Unknown weapon type %i for %s |
| 79 | Failed to allocate from quad cache pool |
| 80 | ET_TURRET has valid DObj but has no obj->tree set. Report this with the console log. newObj=%i, freedObj=%s. |
| 81 | CG_EntityMP_Missile has a weapon that specifies camo %d, but does not have a world model |
| 95 | SV_LoadConfigstrings read/written mismatch - %i vs %i. First %i, Count %i. |
| 96 | G_Objectives_Load: objectivenum out of range (%i, MAX = %i) |
| 97 | SV_LoadHistoryState: entitynum out of range (%i, MAX = %i) |
| 98 | SV_LoadHistoryState: clientnum out of range |
| 99 | SV_LoadHistoryState: agentnum out of range |
| 100 | SV_LoadHistoryState: entnum out of range: %i |
| 101 | SV_LoadHistoryState: DObj expected for entnum %i |
| 105 | Weapon %s has noFullViewmodelAnimations specified, but it is the player's main weapon |
| 106 | Weapon %s is set to 'Multiple Reload Percentage Based' reload type but did not define the 'Multiple Reload Clip Percentage' value. |
| 107 | ScriptableSv_LinkReservedScriptable: Too many reserved scriptable parts to allocate '%s' see previous lines in console output for details |
| 108 | ScriptableSv_LinkReservedScriptable: Too many reserved replicated parts. |
| 109 | ScriptableSv_LinkReservedScriptable: Could not acquire a reserved scriptable - limit is %i - try increasing the number in the worldspawn entity in Radiant |
| 110 | ScriptableSv_LinkReservedScriptable: Could not allocate Stream Buffer of %i bytes |
| 117 | The default model %s bone %s has hit location information but that bone is not present in customization model %s. This will cause the server and client to disagree on hit detection. |
| 118 | The default model %s bone %s has hit location information but the bone with that index in customization model %s is %s. This will cause the server and client to disagree on hit detection. |
| 119 | The default model %s bone %s has hit location %d but the bone with that index in customization model %s named %s has hit location %d. This will cause the server and client to disagree on hit detection. |
| 122 | ScriptableCl_CreateCollision: multi-body detailCollision models are not supported |
| 124 | More than %i AABB nodes needed |
| 126 | Client Model '%s': could not find anim tree '%s' |
| 128 | DXGIXPresentArray failed: %s |
| 129 | Winding point limit (%i) exceeded on brush face |
| 130 | The effect name %s (%d characters) is longer than the max length of %d |
| 135 | ENT_HANDLE_COUNT exceeded - increase size |
| 136 | ENT_HANDLE_COUNT exceeded - increase size |
| 137 | ENT_HANDLE_COUNT exceeded - increase size |
| 138 | ENT_HANDLE_COUNT exceeded - increase size |
| 139 | ENT_HANDLE_COUNT exceeded - increase size |
| 140 | ENT_HANDLE_COUNT exceeded - increase size |
| 142 | Unknown compression type: %d |
| 143 | HavokPhysics: Initial Error: %s (%s) |
| 144 | Havok Physics: shape override index out of range. This can be triggered by non-synchronized mychanges/onlyents builds or iwmap/linker errors. Please check your data. |
| 145 | Havok Physics: shape override index out of range. This can be triggered by non-synchronized mychanges/onlyents builds or iwmap/linker errors. Please check your data. |
| 146 | Havok Physics: shape override index out of range. This can be triggered by non-synchronized mychanges/onlyents builds or iwmap/linker errors. Please check your data. |
| 147 | Havok Physics: shape override index out of range. This can be triggered by non-synchronized mychanges/onlyents builds or iwmap/linker errors. Please check your data. |
| 148 | attempted to play 3D alias '%s' while there is no active listener. Most likely this means you tried to play a 3D sound while not in a level. |
| 149 | SND_GetAliasWithOffset: could not find sound alias '%s' with offset %d |
| 150 | [TESTMONKEY] Failed to open %s |
| 155 | R_RegisterFont: Too many TTF fonts registered (%d). |
| 156 | HudOutlineEnableForClient() could not be applied because there are currently too many hud outline settings. Can we reduce color and depth test variations? |
| 158 | proc_node should be a public node in the atr |
| 159 | proc_node should be a public node in the atr |
| 160 | Com_EventLoop: bad event type %i |
| 162 | Testing drop error |
| 163 | Testing fatal error |
| 183 | Cannot open text file: "%s" |
| 184 | Text file "%s" size exceeded limit (%zu) |
| 185 | IndyFs: Failed to init IndyFs system |
| 186 | Too many blend space 2D assets loaded, maximum is %i |
| 187 | Scriptable '%s' has a state-change event that references a state that does not exist |
| 188 | ERROR: scriptable '%s' could not fix up part refs during load |
| 189 | Invalid vehicle type. Can't spawn a helicopter of vehicletype [%s] |
| 190 | Scriptable %s tried to run a state change event, the state %i was outside the range 0-%i |
| 191 | Scriptable %s tried to run a setModel event, but is a reserved scriptable and this isn't allowed |
| 192 | Scriptable %s tried to play a collision event using collmap %s, but it wasn't available. Does this map need a recompile? |
| 193 | ScriptableSv_RunStateEventCollision: multi-body detailCollision models not yet supported |
| 194 | Scriptable %s tried to play an animation, but had no dobj - could this be called too early in the frame? |
| 195 | Scriptable '%s' cannot play anim as dobj is using anim tree '%s' when tree '%s' was expected |
| 196 | No dobj for scriptable %s when processing hide/show bone event |
| 199 | Scriptable %s tried to disable sub-shape %s and there was no physics instance |
| 200 | Scriptable %s tried to disable sub-shape %s and the name could not be decoded |
| 201 | Scriptable %s tried to disable sub-shape %s and the decoded rigid body idx %i could not be found |
| 202 | Scriptable %s tried to stop disabling sub-shape %s and there was no physics instance |
| 203 | Scriptable %s tried to stop disabling sub-shape %s and the name could not be decoded |
| 204 | Scriptable %s tried to stop disabling sub-shape %s and the decoded rigid body idx %i could not be found |
| 205 | Scriptable %s tried to find bone %s in model %s and failed |
| 206 | Scriptable %s tried to find bone %s but had no model |
| 207 | Scriptable %s tried to damage a part that doesn't have any health states |
| 208 | Scriptable %s tried to add a model, but it is transient - this is not supported |
| 209 | Scriptable %s tried to add a model, with no model |
| 210 | Scriptable %s tried to add a model, but doesn't have a valid model itself |
| 211 | Scriptable %s tried to add a model %s to tag '%s', but it failed |
| 212 | loading too many navmeshes (%d) at once. |
| 213 | Attachment streaming flags mismatch for '%s'; see output above for details |
| 214 | Weapon specifies a default view model '%s' that is streamed. Default models must not be streamed. |
| 215 | Weapon specifies a default world model '%s' that is streamed. Default models must not be streamed. |
| 216 | Weapon specifies a left hand default world model '%s' that is streamed. Default models must not be streamed. |
| 217 | Weapon specifies a right hand default world model '%s' that is streamed. Default models must not be streamed. |
| 218 | Transient flags mismatch for weapon '%s'. Perhaps this weapon is used as a 'not-transient' weapon somewhere, and happens to share models or attachments with streamed weapons |
| 219 | Weapon %s has transient view models but does not specify a default view model. MP transient weapons must provide a default view model when the view model is transient. |
| 220 | Weapon %s has transient models but does not specify a default world model. MP transient weapons must provide a default world model when the weapon has transient model references. |
| 221 | Weapon %s specifies a transient stow offset model %s. This not supported. |
| 222 | ScriptableClSP_ArchiveState: mismatch in g_ScriptableClRuntimePartStatesCount. Scriptable defs have likely changed. |
| 223 | ScriptableClSP_ArchiveState: mismatch in g_scriptableClReservedPartRuntimeCount. Plase review allocation logic. |
| 224 | ScriptableClSP_ArchiveState: Could not allocate Stream Buffer of %i bytes |
| 225 | ERROR: ran out of sound allocations |
| 226 | ERROR: could not find sound allocation for address %p |
| 227 | SOUND: Allocation failed for %ld bytes |
| 228 | SOUND: Allocation failed for %ld bytes |
| 229 | ERROR: could not find sound allocation %p |
| 230 | Invalid VisionSet index %d. |
| 231 | Invalid visionSetIndex %d |
| 232 | Invalid visionSetIndex %d |
| 233 | Error reading CSV files in the fx directory to identify impact effects |
| 234 | Tournament: Starting join communication channel failed - could not allocate a task!. |
| 235 | Tournament: Starting leave communication channel task failed - could not allocate a task!. |
| 238 | Tournament: Starting send message task failed - could not allocate a task!. |
| 239 | CODE ERROR: GScr_SetScriptAndLabel: (%s) out of alignment with loadscript (%s) |
| 240 | CODE ERROR: GScr_SetScriptAndLabel: functions->maxSize exceeded |
| 241 | CODE ERROR: GScr_SetScriptAndLabel: missmatched enforceExists (%s) |
| 242 | Could not translate loadout name "%s" |
| 245 | BG_BulletPenetration: Could not load bullet penetration table '%s' |
| 247 | Shared ammo cap mismatch for "%s" shared ammo cap: '%s" set it to %i, but "%s" already set it to %i. |
| 248 | Weapon has too many attachments '%s'. |
| 249 | GraphFloat_ParseBuffer: File [%s] has too many knots. Max is [%d] |
| 250 | GraphFloat_ParseBuffer: Error parsing graph file [%s] |
| 251 | MemFile_StartSegment: Out of memory |
| 252 | MemFile_EndSegment: Out of memory |
| 253 | MemFile_WriteError: Out of memory |
| 254 | Trying to read corrupted file |
| 255 | Trying to read corrupted file |
| 266 | Cloth client model doesn't have valid cloth node with xanimClothLCN param in %s |
| 269 | ERROR: Using bots in map with non-existent pathdata. Map name: %s |
| 270 | ERROR: Using bots in map without 'path extra data'. Map name: %s |
| 271 | ERROR: Using bots in map with no path node zones. Reconnect paths for this map and look for zone warnings. Map name: %s |
| 272 | ERROR: Using bots in map with broken pathdata. Map name: %s |
| 273 | ERROR: Using bots in map with spawn points outside of path grid. (see log for details) Map name: %s |
| 275 | ERROR: SV_BotFindNodeRandom() %d valid nodes exceeded in %s. |
| 276 | ERROR: SV_BotFindNodeRandom() %d selective valid nodes exceeded in %s. |
| 277 | XAnimReadParameterValues: expected %d parameters, found %d instead |
| 278 | XAnimReadParameterValue: bad archived parameter type (%d) |
| 279 | Unrecognized custom node type '%s' |
| 280 | Client Model %i %s has a dynamic root body and keyframed offset bodies - this is not allowed. |
| 281 | Client Model %i %s has a dynamic root body and keyframed bone bodies - this is not allowed. |
| 288 | WARNING: Exploder with fxid of %s has an invalid exploder id of %s. The exploder id must be an integer from 0 to %d. |
| 289 | WARNING: Exploder with fxid of %s has an invalid ID of %i. ID must be from 0 to %d. |
| 291 | CL_ParsePacketEntities: end of message |
| 292 | Exceeded the maximum valid command size (%i / %zu) |
| 293 | Message overflowed when reading binary command |
| 294 | Compressed server message overflow %i < %i |
| 295 | Uncompressed server message overflow |
| 298 | CL_ReadDemoMessage: demoMsglen > MAX_MSGLEN |
| 302 | Too many cameras loaded, maximum is %i |
| 303 | Cannot load map '%s' in current game mode. |
| 304 | Cannot seamless-load map '%s' in current game mode. |
| 305 | Cloth error detected - check TTY |
| 306 | Script vehicle [%s] needs [%s] |
| 307 | Scriptable %s tried to apply localized splash damage, but had no dobj on the client |
| 308 | Scriptable %s tried to apply localized splash damage, but couldn't find the part name %s |
| 311 | Entity using model %s has too many models - max is %i |
| 312 | Entity using model %s has too many cloth assets - max is %i |
| 313 | Sound table zone entries are not sorted by state |
| 314 | Sound table zone entries are not sorted by state |
| 321 | Could not translate subtitle text: "%s" |
| 322 | Could not translate menu string reference %s |
| 324 | Matchrules did not match checksum |
| 332 | CL_GamepadEvent: bad axis %i |
| 333 | CREATEFX_COMMAND_HEAP_MEMORY_SIZE failed to allocate. |
| 334 | Invalid model path '%s' in %s; path must begin with '%s' / Invalid model path '%s' in misc_prefab; path must begin with '%s' |
| 335 | Invalid model path '%s' in misc_prefab; path must end in '%s' / Prefab '%s' has a local offset, and therefore cannot be edited when the root also has an offset |
| 336 | Only a root-level CreateFx map file may contain misc_prefab entities |
| 337 | Invalid CreateFx map file '%s'; must have extension %s |
| 338 | Invalid CreateFx map file '%s'; must be located under folder 'share/raw/%s' |
| 339 | [SPAWNGROUP_LOOT] Failed to find any remaining item in the set '%s' matching type '%s' for spawning |
| 340 | [SPAWNGROUP_LOOT] Failed to pick an item from the item list using chance logic |
| 341 | Aborted replay due to inconsistency. |
| 342 | SV_DemoMP_InitRead: Bad demo save version %i, expected %i |
| 343 | SV_DemoMP_InitRead: Different thread used for playback, determinism issues will happen, aborting |
| 344 | SV_DemoMP_InitRead: Different checksum from saved demo, calculated %x and the loaded %x |
| 345 | SV_DemoMP_InitRead: Different stats bitmask from saved demo, calculated %zx and the loaded %zx |
| 346 | Invalid index specified for client weapon at (%g %g %g) - valid range is [0,%d) value is %d |
| 347 | Index not specified for weapon at (%g %g %g) |
| 349 | XAnimRegisterConstString: exceeded maximum number of const strings (%d) |
| 352 | Do not nest additives |
| 356 | in_start and in_end notetracks must have matching notetrack in animation %s. |
| 357 | out_start and out_end notetracks must have matching notetrack in animation %s. |
| 358 | in_start_ and in_end notetracks set on same notetrack time for animation %s. |
| 359 | out_start_ and out_end notetracks set on same notetrack time for animation %s. |
| 360 | misordered in_start and in_end notetracks in animation %s. |
| 361 | misordered out_start and out_end notetracks in animation %s. |
| 362 | in and out notetracks overlap for animation %s |
| 363 | in and out notetracks overlap for animation %s |
| 364 | More than one %s notetrack specified in animation %s. |
| 365 | More than one %s notetrack specified in animation %s. |
| 366 | More than one %s notetrack specified in animation %s. |
| 367 | More than one %s notetrack specified in animation %s. |
| 368 | exceeded maximum number of anim transfer parameters |
| 369 | exceeded maximum number of anim transfer info |
| 370 | exceeded maximum number of anim transfer parameters |
| 371 | exceeded maximum number of dobj transfer info |
| 373 | animation '%s' in '%s' cannot be sync looping and nonlooping |
| 375 | animation '%s' in '%s' cannot be sync nonlooping and looping |
| 377 | duplicate specification of animation sync in '%s', %d nodes above '%s' |
| 379 | animation cannot be sync looping and sync nonlooping |
| 380 | Bad Data - Initializing a net const string transient model '%s' |
| 381 | Save game saved with different script files |
| 383 | ClientConnect(): Couldn't allocate sentient |
| 385 | Difficulty values cannot be saved to player profiles when SP isn't active. |
| 386 | Difficulty values cannot be saved to player profiles when SP isn't active. |
| 387 | Difficulty values cannot be saved to player profiles when when SP isn't active. |
| 388 | Difficulty values cannot be saved to player profiles when when SP isn't active. |
| 391 | TaskManager_ConstructTaskSet: MAX_TASK_SETS exceeded. |
| 392 | Unable to read offline stats because we are connected to a server |
| 393 | Trying to set persistent player data while connected to a server (%s, connState = %i)! |
| 394 | Error getting persistent data: see console |
| 395 | Error getting persistent data: result type was a %i, expected string; see console |
| 396 | Error setting persistent data: result type was a %i, expected string; see console |
| 397 | Error getting persistent data: result type was a %i, expected int; see console |
| 398 | Error setting persistent data: result type was a %i, expected string; see console |
| 401 | Missing DDL file '%s' |
| 402 | Illegal specifier |
| 420 | Failed to spawn XModel (%s) via NoteTrack in animation (%s). |
| 421 | ClientCharacter couldn't find asset %s |
| 422 | ClientCharacter couldn't turn netconststring to asset %i |
| 423 | ClientCharacter couldn't find asset %s |
| 424 | Cannot spawn model (%s) via NoteTrack from a non-leaf animation. |
| 425 | Too many NoteTrack-spawned models active for Client Character. Max:%i |
| 426 | Failed to initialize networking |
| 427 | Mismatched usingBotsConnectType <client %i, server %i> |
| 428 | Mismatched agentCount <client %i, server %i> |
| 430 | No 'AI vs AI' accuracy graph for weapon '%s' for AI %d |
| 431 | No 'AI vs AI' accuracy graph for weapon '%s' |
| 432 | No 'AI vs Player' accuracy graph for weapon '%s' for AI %d |
| 433 | No 'AI vs Player' accuracy graph for weapon '%s' |
| 434 | G_FindLocalizedConfigstringIndex: overflow: '%s' |
| 436 | ScriptableCommon_ArchivePartLimits: Instance (reserved) limit is smaller than the required amount (%i vs %i) |
| 437 | ScriptableCommon_ArchivePartLimits: Part limit is smaller than the required amount (%i vs %i) |
| 438 | ScriptableCommon_ArchivePartLimits: Part limit is smaller than the required amount for world %i (%i vs %i) |
| 439 | ScriptableCommon_ArchiveReplicatedInstances: Replicated instance count is too small to read the state (read %i vs have %i) |
| 440 | ScriptableCommon_ArchivePartWorldstate: Part data limit is too small to read the specified part count (%i vs %i) |
| 441 | Buffer (%zu) is not large enough for file '%s' (%zu) |
| 442 | Cannot find network priority entry for perk '%s' at index %d in codePerks.csv. |
| 443 | Invalid perk network priority index %d detected for perk '%s' in codePerks.csv. |
| 444 | Multiple perk network priority entries detected for priority index %d in codePerks.csv. |
| 445 | Cover Wall Model not found (modelIdx == 0), is it included in the level? |
| 446 | Cover Wall Model expects bones %d - %d to all be j_foam_<number> controllers, bone %d is %s |
| 447 | Unknown pixel format |
| 448 | Unknown pixel format |
| 449 | Received invalid stats delta: byte index %d is out of range (see console) |
| 450 | Received invalid stats delta: block size %d is invalid (see console) |
| 451 | Received invalid stats delta: message is less than block size %d (see console) |
| 452 | Received entnum %d: must index ComCharacterLimits::GetCharacterMaxCount() |
| 453 | invalid strength %f |
| 454 | Received duration %d: must be less than USHORT_MAX |
| 455 | Updating ConfigString index %d is out of bounds |
| 457 | Malformed origin for path node '%s' |
| 458 | Could not find parent %s |
| 460 | Path_LoadNode: node out of range (%i) |
| 468 | Scr_AddSpawnVarToken: Exceeded MAX_SPAWN_VARS_CHARS (%d > %d) |
| 469 | Scr_ParseSpawnVars: found %s when expecting { |
| 470 | Scr_ParseSpawnVars: EOF without closing brace |
| 471 | Scr_ParseSpawnVars: EOF without closing brace |
| 473 | Scr_ParseSpawnVars: MAX_SPAWN_VARS |
| 474 | Scr_ParseSpawnVars: found %s when expecting { |
| 475 | Scr_ParseSpawnVars: EOF without closing brace |
| 476 | Scr_ParseSpawnVars: EOF without closing brace |
| 477 | Scr_ParseSpawnVars: EOF without closing brace |
| 478 | Scr_ParseSpawnVars: closing brace without data |
| 479 | Scr_ParseSpawnVars: MAX_SPAWN_VARS |
| 496 | G_SaveSP_ReadClient: %s |
| 497 | G_LoadGame: wrong number of omnvars, got %d, s.b. %d |
| 498 | G_LoadGame: entitynum out of range (%i, MAX = %i) |
| 499 | G_LoadGame: clientnum out of range |
| 500 | G_LoadGame: client mis-match in savegame |
| 501 | G_WriteStruct_FixupFieldPointers: actor out of range (%zi) |
| 502 | G_SaveFieldSP_ReadStruct: %s |
| 503 | Too many gestures loaded, maximum is %i |
| 505 | Could not find IK attach bone %s |
| 506 | Too many XAnimCurves loaded, maximum is %i |
| 507 | DEVONLY - Must create game lobby with LOCAL_CLIENT_0. Bug to code with log file. |
| 508 | DEVONLY - Must create private match with LOCAL_CLIENT_0. Bug to code with log file. |
| 509 | Too many active clients. You may be missing a "nosplitscreen" in a menu. |
| 510 | CL_MainMP_CheckForResend: bad connstate |
| 511 | Client kicked from server due to command baseline checksum mismatch with out of date sequence. REPORT THIS AND ATTACH CLIENT AND HOST LOGS. |
| 512 | Client kicked from server due to command baseline checksum mismatch from invalid command. REPORT THIS AND ATTACH CLIENT AND HOST LOGS. |
| 513 | Client kicked from server due to command baseline checksum mismatch from invalid prediction. REPORT THIS AND ATTACH CLIENT AND HOST LOGS. |
| 514 | Client kicked from server due to command baseline checksum mismatch. REPORT THIS AND ATTACH CLIENT AND HOST LOGS. |
| 515 | map_restart does not support triggering more snapshot entities. Re-launch the level |
| 516 | file '%s', line %d: %s |
| 518 | Could not translate string "%s" |
| 520 | Explosive bullets are not supported for entities of type '%s'. |
| 521 | LUI: Out of memory |
| 522 | LUI error handler |
| 523 | G_BeamEntity_ScriptSpawn given a client tag that is not in ncsclienttags.txt |
| 524 | Could not find or allocate hint string id for message '%s'. See console log for details. |
| 525 | Failed to add a party member to our lobby |
| 526 | Invalid hostname provided to devjoin command |
| 527 | cannot spawn turret '%s' - The turret weapon's GDT field 'Script' has '%s', but loading of the gsc file failed. Check the script file name & ensure its in a csv, or clear out the field. |
| 539 | ComFastFile: Not enough request slots, increase FASTFILE_MAX_LOAD_REQUESTS |
| 540 | CG_GetPlayerViewOrigin: Unable to get DObj for turret entity %i |
| 541 | CG_GetPlayerViewOrigin: Couldn't find [tag_player] on turret |
| 542 | This is an error test! |
| 543 | LUI: Fatal error. Check console output. |
| 546 | Failed to BG_AddAttachmentViewModelsToTag: Model limit (%d) exceeded with model '%s'. |
| 547 | Invalid weapon setup: No animation found for part %d on weapon (%s)%s |
| 548 | Invalid animIndex %d for weapon %s in BG_MultipleReloadInterruptTime function. |
| 549 | More than %d skel mem allocations |
| 573 | SV_GetUserinfo: bufferSize == %i |
| 574 | SV_GetUserinfo: bad index %i |
| 575 | SV_SetUserinfo: bad index %i |
| 582 | Cannot set data "%d" to %i: we don't have stats yet |
| 583 | Cannot set data navHashes[0]="%d" to %i: we don't have stats yet |
| 585 | Error: Calculated Usables limit %u exceeds code-defined limit %u for usables. Consider raising the limit (~48 B per usable) or reduce the usables in the current map. |
| 586 | Ran out of free Sound Ents. The limit is %i. |
| 587 | Expected to reach end of SoundEntities at this position, found value "%i" instead. Savegame may be corrupt. |
| 590 | Stack underflow in %s, trying to %s '%s'. |
| 591 | Stack overflow in %s, trying to %s '%s'. |
| 592 | Unterminated if in %s |
| 593 | No loading movie specified by %s |
| 595 | Could not open %s |
| 597 | SCR_DrawScreenField: bad clcState |
| 598 | SOUND: failed to add bank %s |
| 599 | Failed to create render thread |
| 600 | State timer associated with animation [%s] for weapon [%s] has an invalid value of %ims. (dstate %i, astate %i) |
| 601 | Animation [%s] for weapon [%s] has an invalid length of %f seconds. |
| 602 | Fire blending is enabled for weapon %s but the timing of the hip and ADS fire animations do not match: %s(%d ms) - %s(%d ms) |
| 603 | Animation index %d is referenced more than once in g_weapAnimGroups. This is not allowed. |
| 604 | CG_RegisterWeapon: No idle anim specified for [%s] |
| 605 | CG_Weapon_CreateHandAnimTree: No idle anim specified for [%s] |
| 609 | Failed to CG_Weapon_AddViewmodelWeapon: Model limit (%d) exceeded with model '%s'. |
| 610 | Failed to CG_Weapon_AddRocketModel: Model limit (%d) exceeded with model '%s'. |
| 612 | Failed to CG_Weapon_ChangeViewmodelDobj: Model limit (%d) exceeded with model '%s'. |
| 613 | CG_SaveViewModelAnimTrees: viewmodel has no dobj |
| 614 | CG_LoadViewModelAnimTrees: viewmodel has no dobj. This implies that the weapon that was given at the time of the save does not have a valid hand model. |
| 615 | no viewmodel set for player %d for '%s' in '%s' hand. Set viewmodel hands in script or weapon settings |
| 616 | CG_EjectWeaponBrass: entityWeapon >= BG_GetNumWeapons() |
| 617 | CG_FireWeapon: base weapon >= BG_GetNumWeapons() |
| 618 | Weapon %d: Could not translate display name "%s" |
| 619 | Weapon %d: Could not translate loot variant name "%s" |
| 620 | ARENA_BUFFER_SIZE exceeded |
| 621 | Loading CG SoundEnts: index out of range (%i, MAX = %i) |
| 622 | no script specified for weapon info '%s' being used by AI |
| 641 | Hit max entities in snapshot for a client. See server log for entity details. |
| 642 | >>>>> !!!! Missing precomputed los data file [%s] for map [%s]! Disable this error msg using dev_verifyLOSDataFile. !!!! <<<<< |
| 643 | >>>>> !!!! line of sight data file [%s] for map [%s] is larger than the buffer size required for the map we just loaded (%d > %d)! Rebuild the visdata for this map! !!!! <<<<< |
| 644 | >>>>> !!!! precomputed los data file [%s] for map [%s] is the wrong version! Expected %d != %d! Disable this error msg using dev_verifyLOSDataFile. !!!! <<<<< |
| 645 | >>>>> !!!! precomputed los data file [%s] for map [%s] is the wrong nodecount! Expected %d != %d! Disable this error msg using dev_verifyLOSDataFile. !!!! <<<<< |
| 646 | >>>>> !!!! precomputed los data file [%s] for map [%s] is the wrong quantization! Expected %d != %d! Disable this error msg using dev_verifyLOSDataFile. !!!! <<<<< |
| 647 | Unknown weapon type %i for %s |
| 648 | G_Weapon_SetupWeaponDef: Could not find default weapon '%s' |
| 652 | G_UtilsMP_GetWeaponWorldModels: '%s' does not have a valid world model. |
| 653 | Missing client tag '%s' when attaching model |
| 654 | jpeg internal error |
| 655 | Tag %s should be lower case. See output above. |
| 656 | Tag %s should be lower case. |
| 657 | Could not allocate node for LUI DataModel key %s |
| 658 | Exceeded limit of dirty LUI models in one frame. See list in console output. |
| 660 | Hud elem string too long, %s |
| 662 | Too many turrets loaded, not enough turret anim sets to support them all, increase MAX_TURRET_ANIM_SETS(%d), or remove unused turrets |
| 663 | Missing precomputed constBaseline data file [%s] for map [%s]! Disable this error msg using dev_verifyBaselineFile |
| 664 | Unknown block compression type: %d |
| 665 | script variable leak due to cyclic usage (see console for details) |
| 667 | Could not find compute shader '%s' |
| 668 | Could not find material '%s' |
| 716 | XAnimWobble: Can't find bone (chain index: %d) in model |
| 717 | CG_SnapshotMP_ProcessSnapshots: n < cgameGlob->latestSnapshotNum |
| 718 | CG_SnapshotMP_ProcessSnapshots: Server time went backwards |
| 719 | Unknown Demonware flavor %s |
| 721 | Unsupported data type for SV_Game_RecordDemo_GetPlayerProfileData |
| 722 | Unsupported data type for SV_Game_RecordDemo_GetPlayerProfileData |
| 723 | Ragdoll: Body names should start %s, %s has one called %s |
| 724 | Ragdoll: Could not find const string for bone name %s - maybe it's not in the character based around model %s. |
| 725 | Ragdoll: We only support %i constraints and %s has %i |
| 727 | Cloth entity doesn't have valid cloth node with xanimClothLCN param in %s |
| 733 | Could not translate exe string "%s" |
| 734 | Could not translate part of %s: "%s" |
| 735 | %s too long when translated: "%s" |
| 738 | SCR_DEBUG_FUNCTION_BUFFER_SIZE exceeded. See console for functionList dump. |
| 739 | SCR_DEBUG_FUNCTION_BUFFER_SIZE exceeded. See console for functionList dump. |
| 740 | XAnim Load hit fatal errors. See above output for details |
| 747 | BG_EvaluateTrajectoryDelta: unknown trType: %i |
| 748 | G_Spawn: no free entities, probable cause: %s |
| 750 | Bone index out of range. Looking for bone index %d on an entity with %d bones, who has an entity number of %d, a modelname of %s, and a position of %f %f %f. |
| 751 | Missing tag [%s] on entity [%d] with model(s): %s |
| 754 | CL_GetUserCmdMutable: %i >= %i |
| 755 | G_WriteStruct_FixupFieldPointers: agent out of range (%zi) |
| 756 | G_SaveFieldMP_ReadStruct: %s |
| 757 | Unsupported data type for SV_Game_RecordDemo_GetPlayerProfileData |
| 758 | Unsupported data type for SV_Game_RecordDemo_GetPlayerProfileData |
| 759 | Must create private party with LOCAL_CLIENT_0. Bug to code with log file. |
| 764 | ERROR: Trying to create a Physics FX Pipeline of type %s with %i particles but it only supports %i |
| 766 | duplicate animation '%s' in 'animtrees/%s.atr' |
| 767 | Anim node '%s' cannot be specified as 'additive' and 'overlay' at the same time. |
| 768 | LoadAnimTreeInternal Failed: Unknown anim tree '%s' |
| 770 | Too many server-side scriptable replicated parts (%i) in map, max allowed is %i. See console output for details |
| 771 | ScriptableSv_ArchiveReservedFreeList: freeListSize too large (%u vs %u). |
| 772 | ScriptableSv_ArchiveState: mismatch in g_scriptableSVReservedPartRuntimeCount. Plase review allocation logic. |
| 773 | ScriptableSv_ArchiveState: Could not allocate Stream Buffer of %i bytes |
| 774 | ScriptableSv_CheckIsScriptAccessAllowed: [%i: %s] Scriptables have not been created - most likely caused by script calling Set/Get ScriptablePartState too early (%s) |
| 775 | ScriptableSv_CheckIsScriptAccessAllowed: [%i: %s] Scriptables have been created, but not initialized - most likely caused by script calling Set/Get ScriptablePartState too early (%s) |
| 779 | BaseForFields: invalid fields[] |
| 780 | Unknown event: '%s' |
| 781 | Cannot load map '%s' in current game mode. |
| 783 | Player ASM condition using invalid function '%s'. Set the animset state property 'Skip Condition Function Validation' to ON if this built-in function really needs to be used. |
| 784 | PlayerASM_TranslateFunction: Unable to find code function '%s' |
| 785 | Error processing scriptables. See console log for details. |
| 786 | There were %i error(s) parsing the playlist. |
| 787 | Invalid selectedWeight of %i for playlist %i, total weight for this playlist is %i |
| 788 | Playlist %s has no entries |
| 789 | Playlist complete |
| 790 | Playlist %d has an unknown gametype when trying to run rules. |
| 791 | Invalid playlist |
| 793 | MAX_GAMESTATE_CHARS exceeded |
| 794 | configStringIndex (%d) >= MAX_CONFIGSTRINGS_MP |
| 795 | MAX_GAMESTATE_CHARS exceeded |
| 800 | ERROR: Vehicle path node at( %f, %f, %f ) has negative speed |
| 801 | ERROR: Hit max vehicle path node count [%d] |
| 802 | ERROR: Vehicle path node( %f, %f, %f ) found with no name |
| 803 | Loading CG FXEnts: index out of range (%i, MAX = %i) |
| 804 | ClientCharacter couldn't find netconststring for asset %s |
| 805 | Too many client characters allocated. Maximum is %d |
| 806 | ClientCharacter couldn't find asset %s |
| 813 | script compile error %s %s (see console for details) |
| 814 | script compile error %s %s (see console for details) |
| 815 | CODE ERROR: GScr_SetScriptAndLabel: functions->maxSize exceeded |
| 816 | Exceeded maximum number of script strings (see console log for more info) |
| 817 | Exceeded maximum number of script strings (see console log for more info) |
| 818 | Max string length exceeded: "%s" |
| 819 | Filename '%s' exceeds maximum length of %d |
| 831 | Could not find script '%s' |
| 833 | MAX_GRENADE_HINTS (%zu) exceeded |
| 840 | DenseGrid_AllocPageForCell: Ran out of pages! |
| 841 | DenseGrid::Remove: Failed to remove key %d in cell %d |
| 842 | Couldn't find tag "%s" on turret (entity %d, classname '%s'). |
| 843 | Couldn't find tag_player on turret (entity %d, classname '%s'). |
| 844 | Player anim '%s' has no children |
| 845 | Player anim '%s' has no children |
| 846 | Couldn't find tag "%s" on turret (entity %d, classname '%s'). |
| 847 | G_Turret_Spawn: max number of turrets (%d) exceeded |
| 848 | no weaponinfo specified for turret |
| 849 | GMovingPlatforms: Mismatch in saved client count. %d vs %d |
| 850 | Changing the number of clients is not supported while the server is running |
| 851 | Changing the server framerate is not supported while the server is running |
| 852 | Changing the map name is not supported while the server is running |
| 853 | G_LoadGame: entitynum out of range (%i, MAX = %i) |
| 856 | AnimTreeParser Lookup Failed: No NCS found for anim tree '%s' |
| 857 | script runtime error (see console for details) %s%s%s %s |
| 858 | Error looking up "%s" in playerdata.ddl |
| 859 | Could not look up "%s" entry of "%s" array in playerdata.ddl. |
| 860 | Could not look up "%s" in playerdata.ddl |
| 861 | Could not look up "%d" entry of "%s" array in playerdata.ddl |
| 862 | Expected "%s" to be an array of bools in playerdata.ddl |
| 863 | Could not look up "%s" entry of "%s" array in playerdata.ddl |
| 864 | Could not look up "%s" in playerdata.ddl |
| 865 | Could not look up "%d" entry of "%s" array in playerdata.ddl |
| 866 | Could not look up "%s" to in playerdata.def |
| 867 | Could not look up "%s" entry of "%s" array in playerdata.def |
| 868 | Could not look up "%s" in playerdata.ddl |
| 869 | Could not look up "%d" entry of "%s" array in playerdata.ddl |
| 870 | Could not look up "%s" in playerdata.ddl |
| 871 | Could not look up "%s" entry of "%s" array in playerdata.ddl |
| 872 | Could not look up "%s" in playerdata.ddl |
| 873 | Could not look up "%d" entry of "%s" array in playerdata.ddls |
| 874 | CG_CanSeePlayer: Couldn't find [j_head] on player |
| 875 | CG_CanSeePlayer: Couldn't find [j_mainroot] on player |
| 876 | Failed to create thread |
| 878 | Too many usable client masks in the save file |
| 879 | Error: Calculated Usables limit %d exceeds code-defined limit %u for usables. Consider raising the limit (~48 B per usable) or reduce the usables in the current map. |
| 880 | Error: Calculated Usables limit %lu exceeds code-defined limit %u for usables. Consider raising the limit (~48 B per usable) or reduce the usables in the current map. |
| 881 | Too many usable clientmasks allocated - we support %i |
| 882 | Failed to add new usable clientmask |
| 883 | AimTarget_GetTagPos: Cannot find tag [%s] on entity with the models: %s |
| 884 | Physics Worker Error - please check tty for details |
| 885 | Overflow uncompressed msg buf in CL_WritePacket() |
| 886 | Overflow compressed msg buf in CL_WritePacket() |
| 887 | G_WriteStruct_FixupFieldPointers: entity out of range (%zi) |
| 888 | G_WriteStruct_FixupFieldPointers: entity out of range (%zi) |
| 889 | G_WriteStruct_FixupFieldPointers: client out of range (%zi) |
| 890 | G_WriteStruct_FixupFieldPointers: vehicle out of range (%zi) |
| 891 | G_WriteStruct_FixupFieldPointers: sentient out of range (%zi) |
| 892 | G_WriteStruct_FixupFieldPointers: sentient out of range (%zi) |
| 893 | G_WriteStruct_FixupFieldPointers: unknown field type |
| 894 | G_SaveField_ReadStruct: %s |
| 895 | SCR_DrawScreenField: bad clcState %i |
| 896 | Unable to compare against column number %i - there are only %i columns |
| 897 | Unable to compare against column number %i - there are only %i columns |
| 898 | Unable to compare against column number %i - there are only %i columns |
| 899 | Unable to compare against column number %i - there are only %i columns |
| 900 | Unable to find the lookup table '%s' in the fastfile |
| 902 | Scriptable %s tried to apply localized splash damage, but had no dobj |
| 903 | Scriptable %s tried to find bone %s in model %s and failed |
| 904 | Scriptable %s tried to find bone %s but had no model |
| 905 | [SPAWNGROUP_LOOT] exceeded SPAWNGROUP_LOOT_MAX_TYPES in table '%s' |
| 906 | [SPAWNGROUP_LOOT] empty item name in table '%s' |
| 907 | [SPAWNGROUP_LOOT] unable to find item '%s' in for table '%s' |
| 908 | [SPAWNGROUP_LOOT] empty set name in table '%s' |
| 909 | [SPAWNGROUP_LOOT] chanceMax '%s' is invalid size for set '%s', needs to be zero to %d |
| 910 | [SPAWNGROUP_LOOT] empty parse set in file loot table '%s' |
| 911 | [SPAWNGROUP_LOOT] unable to find set table '%s' from loot table '%s' |
| 912 | [SPAWNGROUP_LOOT] item '%s' from loot set '%s' row %d not found in table '%s' / [SPAWNGROUP_LOOT] rarity '%s' is too long in row %d of table '%s' / [SPAWNGROUP_LOOT] rarity '%s' cannot file type '%s' in row %d of table '%s' |
| 913 | [SPAWNGROUP_LOOT] exceeded SPAWNGROUP_LOOT_MAX_ITEM_DEFS in set '%s' in table '%s' |
| 914 | [SPAWNGROUP_LOOT] exceeded SPAWNGROUP_LOOT_MAX_SET_REQS in set '%s' in table '%s' |
| 915 | [SPAWNGROUP_LOOT] exceeded SPAWNGROUP_LOOT_MAX_SETS when adding set '%s' in table '%s' |
| 916 | [SPAWNGROUP_LOOT] there are two global zones in '%s' in table '%s' |
| 917 | [SPAWNGROUP_LOOT] unable to find item '%s' for depend with '%s' in for table '%s' |
| 918 | [SPAWNGROUP_LOOT] unable to depend item '%s' for base item '%s' in for table '%s' |
| 919 | [SPAWNGROUP_LOOT] unable to add depend to item '%s', because we hit SPAWNGROUP_LOOT_MAX_DEPENDS in for table '%s' |
| 921 | [SPAWNGROUP_LOOT] Errors parsing item defs from '%s', see console |
| 922 | [SPAWNGROUP_LOOT] There are no zones in '%s', perhaps you need to specify zone map on the command line '+set loot_table_zones <name>' |
| 927 | Too many math triggers (trigger_radius or trigger_disk) - max is %i |
| 928 | radius not specified for trigger_radius at (%g %g %g) |
| 929 | height not specified for trigger_radius at (%g %g %g) |
| 930 | radius not specified for trigger_rotatable_radius at (%g %g %g) |
| 931 | height not specified for trigger_rotatable_radius at (%g %g %g) |
| 932 | radius not specified for trigger_disk at (%g %g %g) |
| 950 | Compass ownerdraw had width or height of 0 |
| 951 | Compass ownerdraw had width or height of 0 |
| 952 | Channel name too long in specified list: "%s" |
| 953 | Vision set name exceeds the maximum length of %d characters in vehicle %s for map %s |
| 955 | Too many camos loaded; maximum is %i |
| 956 | Client Stream Sync: Exceeded max item list count (%i) for type %i trying to add model %s. See console for details. |
| 957 | SV_StreamSync: Default model '%s' is either transient or not usable. |
| 958 | Netchan_Transmit: length = %i |
| 959 | Updating ConfigString index %d is out of bounds |
| 960 | Unknown transient command: %s |
| 961 | Unknown mayhem command: %s |
| 962 | DirectX error |
| 963 | DirectX error |
| 967 | ERROR: Mismatch between code constant table in ff, vs runtime. Did you forget to bump db_version? Do you have a conditional code const, that isn't active all the time? |
| 968 | ERROR: code constant table hash was not found in DB, something went horribly wrong |
| 979 | G_Spawn_WorldSpawn: The first entity isn't 'worldspawn' |
| 980 | SpawnEntities: no entities |
| 981 | Anchor entity with targetname '%s' did not spawn! |
| 982 | Max number of %d Player ASM states reached. . |
| 983 | Recursive subtree ASMs are not supported. Asset: ['%s'], Subasset: ['%s'], State: ['%s'], Substate: ['%s']. |
| 984 | ent %d is not running an asm! |
| 990 | Cannot set 'duration' when using a blendspace node ('%s'). Feature not supported. |
| 991 | Couldn't find a spawn point |
| 992 | No sentient for player. |
| 993 | CL_GetSnapshot: snapshotNumber > cl->snapshot.messageNum |
| 994 | CL_SetCGameTime: cl->stap is not valid |
| 995 | cl->snap.info.serverTime < cl->oldFrameServerTime |
| 1023 | TAGINFO_HEAP_MEMORY_SIZE exceeded. |
| 1024 | TAGINFO_HEAP_MEMORY_SIZE failed to allocate. |
| 1043 | LOCAL_VAR_STACK_SIZE exceeded. Script function has more than %d local variables. |
| 1044 | MAX_SWITCH_CASES exceeded. Switch statement has more than %d cases. |
| 1045 | Forced stub asset '%s'(%s) is not supported in fastfile '%s'. These assets must only be placed in the global mode fastfiles (global_core_mp or global_cp) |
| 1046 | Transient asset %s(%s) already loaded by %s. |
| 1047 | Failed to preload '%s'. It is either missing or out of date. |
| 1048 | rawfile '%s' exceeded max size for len %d |
| 1049 | Had Streamed Image Mismatch errors. Search output for 'WARNING: Streamed Image Mismatch' to find them |
| 1050 | ERROR: "%s", Since this is the default suit, view height "%s" must be the hard-coded default value (%d). Otherwise, a code change is required. |
| 1051 | ERROR: "%s" View height "%s" %d must be less than code-defined maximum %d. |
| 1052 | ERROR: "%s" View height "crouch" %d must be less than "stand" %d. |
| 1053 | ERROR: "%s" View height "prone" %d must be less than "crouch" %s. |
| 1054 | ERROR: "%s" View height "dead" %d must be less than "crouch" %d. |
| 1055 | ERROR: "%s" View height "slide" %d must be less than "crouch" %d. |
| 1056 | ERROR: "%s" View height "sprint" %d must be less than "stand" %d. |
| 1057 | ERROR: "%s" View height "stand" %d must be less than bounds "stand" %d. |
| 1058 | ERROR: "%s" View height "crouch" %d must be less than bounds "crouch" %d. |
| 1059 | ERROR: "%s" View height "prone" %d must be less than bounds "prone" %d. |
| 1060 | ERROR: "%s" Bounds radius %d must be <= to bounds half height "stand" %f. |
| 1061 | ERROR: "%s" Bounds radius %d must be <= to bounds half height "crouch" %f. |
| 1062 | ERROR: "%s" Bounds radius %d must be <= to bounds half height "prone" %f. |
| 1069 | max config string length exceeded for %s |
| 1070 | SV_SetConfigstring: bad index %i |
| 1071 | SV_AgentSentientInit(): Couldn't allocate sentient |
| 1072 | Trying to spawn agent with unknown animset %s |
| 1075 | CM_LoadMap: NULL name |
| 1076 | Ent %d is not running an ASM! |
| 1077 | Ent %d is not currently running ASM %s |
| 1078 | Unable to find state %s in ent %d ASM %s |
| 1082 | Failed to initialize net |
| 1083 | Sys_SendPacket: bad address type %i |
| 1084 | too many actors in bsp file |
| 1085 | Tried to force spawning of an AI when all AI slots are used by undeletable AI |
| 1086 | AI species '%s' does not have anim tree set. Check that 'useAnimTree' has been called |
| 1087 | Failed to preload fastfiles. They are either missing or out of date. |
| 1090 | AddRadiantSpawnVarToken: MAX_SPAWN_VARS_CHARS |
| 1091 | ParseRadiantSpawnVars: found %s when expecting { |
| 1092 | ParseRadiantSpawnVars: EOF without closing brace |
| 1093 | ParseRadiantSpawnVars: EOF without closing brace |
| 1094 | ParseRadiantSpawnVars: closing brace without data |
| 1095 | ParseRadiantSpawnVars: MAX_SPAWN_VARS |
| 1096 | animation state '%s' does not exist in animset '%s' |
| 1097 | animation entry '%s' is not a part of the state '%s' in animset '%s' |
| 1098 | animation state %s does not exist in animset %s |
| 1099 | state '%s' does not exist in animset '%s' |
| 1100 | Could not find animation entry '%d' in state '%s' in animset '%s'. Selected animset alias might be empty. |
| 1102 | ASM asset is not specified for suit '%s'. |
| 1103 | ASM asset is not specified for suit '%s'. |
| 1104 | Animset asset is not specified for suit '%s'. |
| 1105 | HTTP Web service request exceeds given buffer size %i > %i |
| 1108 | FS_BuildOSPath: os path length exceeded |
| 1109 | Short read in FS_CopyFile() |
| 1110 | Short write in FS_CopyFile() |
| 1117 | FS_ReadFile with empty name |
| 1124 | FSH_FOpenFile: bad mode |
| 1125 | FSH_FOpenFile: unsupported mode %i |
| 1126 | G_LoadWeaponCue: entity out of range (%i) |
| 1127 | G_LoadWeaponCue: entity out of range (%i) |
| 1128 | No world model loaded for entity %i with model %s |
| 1129 | Bad weapon model '%s' for weapon '%s' |
| 1130 | No world model loaded for entity %i with model %s |
| 1131 | BG_PlayerASM_FindWeaponPackageKnobNode: alias '%s' does not contain any animations. |
| 1132 | Too many asset requests generated. See console output for details. |
| 1133 | NULL ent think |
| 1134 | G_Main_InitServerShellshock: Couldn't find shell shock '%s' -- see console |
| 1135 | We stalled loading into '%s' because the preload wasn't complete before the ChangeLevel() call |
| 1136 | We have preloaded fastfiles, but we're not loading into the correct level! See output for list of preload fastfiles, it doesn't include '%s' |
| 1137 | Unable to load the requested map '%s' |
| 1141 | Unknown function |
| 1142 | SCR_DEBUG_FUNCTION_COUNT exceeded. See console for functionList dump. |
| 1143 | ScriptCompile: MAX_PRECACHE_ENTRIES (%u) exceeded when adding far_function_count. See console log. |
| 1146 | CG_WeaponsMP_PrecacheWeaponDef: Weapon index mismatch for '%s' |
| 1147 | CG_GetPlayerViewOrigin: Unable to get DObj for turret entity %i |
| 1148 | Turret has no bone: tag_player |
| 1150 | cannot find '%s' |
| 1151 | unknown type '%s' in '%s' |
| 1152 | missing field name in '%s' |
| 1153 | duplicate key '%s' in '%s' |
| 1155 | Com_GameMode_ActivateGameMode_Dvars: Validation of permanent dvars failed. Look for 'Dvar_ValidatePermanentDvars' in console log for details. |
| 1156 | Com_GameMode_DeactivateGameMode_Dvars: Validation of permanent dvars failed. Look for 'Dvar_ValidatePermanentDvars' in console log for details. |
| 1157 | CG_VisionSetDebugCmd has insufficient size |
| 1158 | CG_VisionSetDebugCmd has insufficient size |
| 1159 | Invalid visionSetIndex %d |
| 1160 | Thermal is active but a visionset isn't provided. |
| 1161 | Missile Cam is active but a visionset isn't provided. |
| 1162 | Night Vision is active but a visionset isn't provided. |
| 1163 | Invalid visionSetIndex %d |
| 1164 | CG_VisionSetDefaultAlternates found more visionsets than MAX_ALTERNATE_VISION of %d |
| 1165 | Player anim '%s' has no children |
| 1166 | Player anim '%s' has no children |
| 1167 | Transitional anim %s should not be set as looping in anim script for %s |
| 1168 | Transitional anim %s cannot use 'duration' with 'speed_scaled_transition' in anim script for %s |
| 1169 | Synced transition anim %s cannot use 'duration' in anim script for %s |
| 1170 | transition or event anim '%s' is longer than limit of %i milliseconds in anim script for %s |
| 1171 | Animation (%s) found in anim script for %s but is not present in the atr file |
| 1172 | shadow animation '%s' should be looping to match '%s' |
| 1173 | shadow anim '%s' duration (%.2fs) does not match that of replaced anim '%s' (%.2fs) |
| 1174 | number of animations in anim tree '%s' too large for shadow anim map (has %d anims, max %d) |
| 1175 | anim '%s' already mapped to shadow anim '%s' (tried to add '%s') |
| 1176 | BG_AnimationMP_RegisterWeaponAnims: length (%zu) of anim name '%s' exceeds limit of %zu |
| 1177 | BG_GetAnimationForIndex: index out of bounds |
| 1178 | Player animation index out of range (%i): %i |
| 1179 | death animation '%s' is looping |
| 1180 | Could not find animation tree '%s' |
| 1181 | Player ASM: At least one archetype should be marked as default in ASMEd |
| 1182 | Turret has no bone: tag_player |
| 1183 | Script function count mismatch |
| 1184 | Mismatching scripts between save and fast files. Map will start from the beginning in demo/ship |
| 1199 | null name in Dvar_GenerateChecksum |
| 1200 | Dvar_GenerateChecksum given dvarName %s that is longer than %d characters in length. |
| 1201 | Script dvars are not allowed to be re-registered under a different permission set ( %s ) |
| 1203 | Can't create dvar '%s': %i dvars already exist |
| 1204 | Dvar SAVE_STRING_MAX_SIZE exceeded in save game |
| 1205 | SAVE_STRING_MAX_SIZE exceeded in save game |
| 1208 | G_VehicleSpawner_Create: Could not find server def for vehicle def '%s'. |
| 1216 | Could not load %s file [%s] |
| 1217 | File [%s] is not a %s file |
| 1218 | File [%s] is not a valid %s |
| 1220 | CameraLerpTrace camera time is %f. Must be greater than 0. |
| 1221 | Rotate camera cannot have <fixed> camera angles since angles will be rotating. |
| 1222 | Rotate camera cannot be <attached> to an entity camera angles. Perhaps you want to use <target> angles instead |
| 1223 | Rotate camera (transition) cannot have <fixed> camera angles since angles will be rotating. |
| 1224 | Rotate camera (transition) cannot be <attached> to an entity camera angles. Perhaps you want to use <target> angles instead |
| 1226 | Ran out of free FX Ents; the limit is %i |
| 1227 | Expected to reach end of FXEntities at this position, found value "%i" instead. Savegame may be corrupt. |
| 1230 | Could not find label '%s' in script '%s' |
| 1231 | Too many CinematicMotion assets loaded, maximum is %i |
| 1232 | G_Vehicle_PrecacheDefs: Could not find server def for default vehicle '%s' |
| 1233 | Script vehicle [%s] needs [%s] |
| 1234 | ERROR: Vehicle '%s' type '%d' with xmodel '%s' is missing a required wheel tag! Valid tags are: %s |
| 1235 | VEH_LinkCommonChecks: Player is already using a vehicle |
| 1236 | VEH_LinkCommonChecks: Player already has an owner |
| 1237 | VEH_LinkCommonChecks: Vehicle already has an owner |
| 1238 | VEH_LinkCommonChecks: Trying to use vehicle without a bone [tag_player\|tag_rider\|tag_driver] |
| 1239 | VEH_LinkPlayer: Cannot link to vehicle bone [tag_player\|tag_rider\|tag_driver]: %s |
| 1243 | Hit max vehicle count [%d] |
| 1244 | G_Vehicle_Create: Could not find server def for vehicle def '%s'. |
| 1246 | Could not find %s on vehicle %s but it is needed for dual fire |
| 1247 | vehicle (entnum %i) (of type %s) has a bad setup, physics object couldn't be created |
| 1248 | Vehicle type not specified for entity with classname '%s' and origin (%.2f, %.2f, %.2f) |
| 1249 | Couldn't load file '%s' |
| 1250 | Player anim type array size(%d) exceeded ( %s ) |
| 1333 | Com_BeginParseSession: session overflow trying to parse %s |
| 1334 | Com_EndParseSession: session underflow |
| 1335 | %sFile %s, line %i: %s |
| 1336 | Havok Internal Error %x: %s (%s): %s: %i |
| 1347 | PlayerASM animset '%s' has more than one patch animset. |
| 1348 | PlayerASM patched animset alias '%s' has reached the max number '%d' of ref const strings. |
| 1349 | PlayerASM animset '%s' and patch '%s' animset must use the same anim tree. |
| 1350 | G_LoadGame: Sentient info count out of range |
| 1351 | SV_LoadHistoryState: Sentient info count out of range |
| 1352 | Missing node in client_character.atr : %s |
| 1355 | Cannot currently register more than %i commands per player/global |
| 1356 | Not supported on front-end server |
| 1357 | usage: buttonPressed(<button name>) |
| 1358 | USAGE: <player> SetCameraThirdPerson( <boolean> ) |
| 1359 | USAGE: <player> PingPlayer( <team> ) |
| 1360 | team must be "enemy", "friendly", or "all" |
| 1361 | Scavenger item does not exist or has not been pre-cached. |
| 1362 | Scavenger bags must not have transient models. Transient equipment weapon models are not supported because are not accounted for in the streaming limits. |
| 1363 | Given stun fraction (%.2f) is outside the allowed range (0,1). |
| 1364 | Trying to do damage to a client that is already dead |
| 1365 | Agent(%d) is already dead when suicide command called! |
| 1366 | LockDeathCamera is only valid for clients. |
| 1367 | unknown team '%s' |
| 1368 | USAGE: self GetMLGSpectatorTeam() |
| 1369 | Player is not an MLG spectator |
| 1370 | Invalid team number |
| 1371 | USAGE: self getGuid() |
| 1372 | USAGE: self getXuidHigh() |
| 1373 | This function can't be used for the front-end server |
| 1374 | USAGE: self getXuidLow() |
| 1375 | This function can't be used for the front-end server |
| 1376 | USAGE: self getXuid() |
| 1377 | This function can't be used for the front-end server |
| 1378 | USAGE: self getJoinType() |
| 1379 | GetJoinType cannot be called for the front end server |
| 1380 | USAGE: self getSkill() |
| 1381 | GetSkill cannot be called for the front end server |
| 1382 | USAGE: self IsHost() |
| 1383 | USAGE: self GetSpectatingPlayer() |
| 1384 | USAGE: self GetMLGSelectedCamera() |
| 1385 | entity %i is not a player or player-controlled agent |
| 1390 | '%i' is an illegal rank value. Must be less than %i. |
| 1391 | '%i' is an illegal prestige value. Must be less than %i. |
| 1392 | Player provided is not a client or an agent. |
| 1393 | Invalid slot number %i, must be between 0 and %i. |
| 1394 | Can not be called if 'camera_thirdPerson' dvar is false |
| 1395 | Entity has no sentient |
| 1398 | GetPrivatePartySize: Must have 0 parameters ( self registerParty( self.clientid ). |
| 1399 | GetPrivatePartySize cannot be called for front-end server |
| 1400 | GetFireteamMembers cannot be called for front-end server |
| 1401 | GetFireteamMembers only works in online games. |
| 1402 | GetFireteamMembers cannot be called for front-end server |
| 1403 | GetFireteamMembers only works in online games. |
| 1404 | SetCustomization requires an index of the customization body and head. |
| 1405 | SetCustomization: Empty body specified. |
| 1406 | SetCustomization: Invalid body specified. %s is not from the body customization table |
| 1407 | SetCustomization: Empty head specified. |
| 1408 | SetCustomization: Invalid head specified. %s is not from the head customization table |
| 1409 | Failed to set the body customization model |
| 1410 | Failed to set the head customization model |
| 1412 | LoadCustomization requires an index of the customization body and head. |
| 1413 | Invalid body specified. Empty strings are not supported |
| 1414 | Invalid head specified. Empty strings are not supported |
| 1415 | ClearCustomization should specify only a single boolean argument. |
| 1416 | Must be called on a player entity |
| 1417 | Must be called on a player entity |
| 1418 | Must be called on a player entity |
| 1419 | The specified model %s is not in the customization table for bodies |
| 1420 | The specified model %s is not in the customization table for bodies |
| 1421 | The specified model %s is not in the customization table for bodies |
| 1422 | LoadWeaponsForPlayer requires an array of weapon names and optionally a boolean indicating if the load should be prioritized. |
| 1423 | ClearWeaponLoadRequestsForPlayer specifies only a single boolean argument. |
| 1424 | LoadWorldWeaponsForPlayer requires an array of weapon names and optionally a boolean indicating if the load should be prioritized. |
| 1425 | LoadWeaponsForPlayer requires an array of weapon names. |
| 1426 | LoadWeaponsForPlayer requires an array of weapon names. |
| 1427 | Invalid slot number specified for ShowHudSplash |
| 1428 | entity %i is not a player or player-controlled agent |
| 1429 | SetDvar: unset dvar '%s' cannot be set as a client dvar. need to be added to the config file. This won't work in a future build |
| 1430 | server dvar '%s' cannot be set as a client dvar |
| 1431 | non-writable dvar '%s' cannot be set as a client dvar |
| 1432 | unknown network id for dvar '%s' |
| 1433 | %s is an invalid dvar name |
| 1434 | Not enough parameters to setclientdvar() - must be an even number of parameters (dvar, value, dvar, value, etc.) |
| 1435 | Dvar %s has an invalid dvar name |
| 1436 | Incorrect number of parameters. <int>, <string> |
| 1437 | SetClientWeaponInfo: entity must be a player entity |
| 1438 | SetClientWeaponInfo: weapon index needs to be between 0 and %i |
| 1439 | Incorrect number of parameters. <loadoutType>, <itemName>/<rawValue> |
| 1440 | SetClientLoadoutInfo: entity must be a player entity |
| 1441 | SetClientLoadoutInfo: raw value of %i exceed max MLG loadout value of %i |
| 1442 | SetClientLoadoutInfo: Incorrect loadoutType passed in! Please refer to log for correct loadout type strings |
| 1443 | Incorrect number of parameters. <int>, <bool> |
| 1444 | SetClientExtraSuper: entity must be a player entity |
| 1445 | SetClientExtraSuper: index %i exceed max MLG loadout index of %i |
| 1446 | SetKillstreakPoints not allowed on front end server |
| 1447 | Incorrect number of parameters. <bool> |
| 1448 | SetKillstreakPoints: entity must be a player entity |
| 1449 | SetKillstreakPoints: value of %i exceed max MLG killstreak value of %i |
| 1450 | SetNextKillstreakCost not allowed on front end server |
| 1451 | Incorrect number of parameters. <bool> |
| 1452 | SetNextKillstreakCost: entity must be a player entity |
| 1453 | SetNextKillstreakCost: value of %i exceed max MLG killstreak value of %i |
| 1454 | SetGametypeVIP not allowed on front end server |
| 1455 | Incorrect number of parameters. <bool> |
| 1456 | SetGametypeVIP: entity must be a player entity |
| 1457 | GetGametypeVIP not allowed on front end server |
| 1458 | Incorrect number of parameters. |
| 1459 | GetGametypeVIP: entity must be a player entity |
| 1460 | SetNoteworthyKillstreakActive not allowed on front end server |
| 1461 | Incorrect number of parameters. <bool> |
| 1462 | SetNoteworthyKillstreakActive: entity must be a player entity |
| 1463 | GetNoteworthyKillstreakActive not allowed on front end server |
| 1464 | Incorrect number of parameters. |
| 1465 | GetNoteworthyKillstreakActive: entity must be a player entity |
| 1466 | SetSpecialActive not allowed on front end server |
| 1467 | Incorrect number of parameters. <bool> |
| 1468 | SetSpecialActive: entity must be a player entity |
| 1469 | GetSpecialActive not allowed on front end server |
| 1470 | Incorrect number of parameters. |
| 1471 | GetSpecialActive: entity must be a player entity |
| 1472 | IncreasePlayerConsecutiveKills: not allowed on front end server |
| 1473 | IncreasePlayerConsecutiveKills: entity must be a player entity |
| 1474 | IncreasePlayerConsecutiveKills: consecutiveKills value %i is greater than max value %i |
| 1475 | ResetPlayerConsecutiveKills: not allowed on front end server |
| 1476 | ResetPlayerConsecutiveKills: entity must be a player entity |
| 1477 | SetPlayerSuperMeterProgress: not allowed on front end server |
| 1478 | SetPlayerSuperMeterProgress: entity must be a player entity |
| 1479 | SetPlayerSuperMeterProgress: super meter value %hu is greater than max value %i |
| 1480 | SetMLGFollowDroneActive: not allowed on front end server |
| 1481 | SetMLGFollowDroneActive: entity must be a player entity |
| 1482 | SetMLGFollowDroneActive: needs 1 argument |
| 1483 | IsMLGFollowDroneActive not allowed on front end server |
| 1484 | IsMLGFollowDroneActive: entity must be a player entity |
| 1485 | invalid sound alias '%s' |
| 1486 | invalid sound alias: '%s' |
| 1487 | invalid sound alias '%s' |
| 1489 | SetKillcamEntSticksToLookAtEnt(): entity %i is not a player |
| 1496 | Unsupported layer name %s |
| 1497 | NavTrace: edge start pos NAN |
| 1498 | NavTrace: edge end pos NAN |
| 1499 | NavTrace: normal NAN |
| 1500 | NavTrace3D: No nav volumes loaded! |
| 1501 | CreateNavLink( linkName, startPos, endPos, animscript ): invalid number of arguments. |
| 1502 | CreateNavLink: pathnode (arg4) must be a negotiation begin node. |
| 1504 | CreateNavLink: unable to find navigational space associated with entity %d. |
| 1505 | DestroyNavLink( linkName ): invalid number of parameters. |
| 1506 | CreateNavRepulsor: cannot create repulsor with non-positive radius. |
| 1507 | CreateNavRepulsor requires a valid entity. If no entity is desired, then create a repulsor with a point/radius instead. |
| 1508 | CreateNavRepulsor: entity has non-positive radius. |
| 1509 | CreateNavRepulsor: unable to create repulsor in non-navigable space at %s. |
| 1510 | CreateNavRepulsor: unable to alloc repulsor. Exceeded max (%d)? |
| 1511 | DestroyNavRepulsor: Invalid entity. |
| 1512 | DockMovingPlatform: No mesh found attached to entity %d. |
| 1513 | DockMovingPlatform: Mesh attached to entity %d is not dockable. |
| 1514 | DockMovingPlatform: Unable to find navigational space associated with entity %d. |
| 1515 | DockMovingPlatform: Unable to find navigational space associated with entity %d. |
| 1516 | UndockMovingPlatform: Unable to find navmesh associated with platform entity %d. |
| 1517 | UndockMovingPlatform: mesh associated with platform entity %d is not marked dockable. |
| 1518 | UndockMovingPlatform: space attached to ent %d already exists. Calling UndockMovingPlatform on something that is not currently docked? |
| 1519 | Unsupported ent type. Currently supporting player, actor, and bot. |
| 1520 | GetRandomNavPoint: Ref ent %d must have a navigator. |
| 1521 | GetRandomNavPoints: Can only find %d points at a time. User requested %d. |
| 1522 | GetRandomNavPoints: ref ent %d must have a navigator. |
| 1523 | GetRandomNavPoints: origin2 must be accompanied by radius2. |
| 1524 | GetClosestPointOnNavmesh can only be used when navmesh is loaded! |
| 1525 | GetClosestPointOnNavmesh: No navmesh loaded in layer %s |
| 1526 | GetClosestPointOnNavmesh3d can only be used when navmesh is loaded! |
| 1527 | Either NavMesh is not loaded or no 3d volumes found! |
| 1528 | GetNavSpaceEnt : invalid entity. |
| 1529 | GetNavSpaceEnt : Entity %d does not have a navigator. |
| 1530 | GetNavSpaceEnt : Navigator (ent %d) is not in any space! |
| 1531 | GetNavSpaceEnt : Space has invalid parent ent %d. |
| 1532 | Unsupported ent type. Currently supporting player, actor, scripted agents, and bot. |
| 1533 | GetModifierLocationOnPath: invalid entity. |
| 1534 | GetModifierLocationOnPath: Entity %d does not have a navigator. |
| 1535 | GetModifierLocationOnPath: Entity %d does not have a 2D navigator. |
| 1536 | CreateNavModifier: invalid name. |
| 1537 | CreateNavModifier: invalid key provided. Must be target, targetname, script_linkname, or script_noteworthy |
| 1538 | CreateNavModifier: creation failed for some reason. Possible causes: no modifier found by that name, no modifier found that was marked NOT_ON_LOAD. |
| 1539 | GetNavPosition: invalid entity. |
| 1540 | GetNavPosition: Entity %d does not have a navigator. |
| 1541 | AnimScripted entities are not supported in this game mode |
| 1542 | too many parameters |
| 1543 | must set nonnegative goal time |
| 1544 | must set nonnegative weight |
| 1545 | No model exists. |
| 1546 | AnimScripted entities are not supported in this game mode |
| 1547 | too many parameters |
| 1548 | must set nonnegative goal time |
| 1549 | must set nonnegative weight |
| 1550 | No model exists. |
| 1551 | too many parameters |
| 1552 | too few parameters |
| 1553 | Must call SetProneAnimNodes before calling EnterProne |
| 1554 | Down angle (parameter 1) must be set to be less than 0. |
| 1555 | Up angle (parameter 2) must be set to be greater than 0. |
| 1556 | can only use a turret |
| 1557 | can only use a turret |
| 1558 | Can't find specified weapon |
| 1559 | Laser flag 'target' requires a target pos. |
| 1560 | SetLaserFlag: Invalid parameter. Must be one of: (none,interpolate,lock,target) |
| 1561 | AnimScripted entities are not supported int this game mode |
| 1562 | No model exists. |
| 1563 | incorrect number of parameters |
| 1564 | must set nonnegative rate for flagged anims |
| 1565 | Expecting int |
| 1566 | Unable to find state %s in animset %s |
| 1567 | invalid entry index %d for animset %s state %s |
| 1568 | AnimScripted entities are not supported in this game mode |
| 1569 | Unable to find state %s in animset %s |
| 1570 | AnimScripted entities are not supported in this game mode |
| 1571 | Unable to find state %s in animset %s |
| 1572 | AnimScripted entities are not supported in this game mode |
| 1573 | incorrect number of parameters |
| 1574 | AISetAnimBlendCurve: could not find curve '%s' |
| 1575 | AnimScripted entities are not supported in this game mode |
| 1576 | too many parameters |
| 1577 | must be > 0 |
| 1578 | must be < 1 |
| 1579 | not a leaf animation |
| 1580 | cannot set time 1 on looping animation |
| 1581 | Incorrect switchtransientset() call. |
| 1582 | Cannot use switchtransientset() in the first frame of a level script. Use add_start() to setup start transient(s) instead. |
| 1583 | Incorrect WaitForAllTransients() call. |
| 1584 | Cannot use WaitForAllTransients() in the first frame of a level script. |
| 1585 | Incorrect WaitForTransient() call. |
| 1586 | WaitForTransient: Bad transient name '%s' |
| 1587 | Cannot use WaitForTransient() in the first frame of a level script. |
| 1588 | WaitForTransient: Unknown transient '%s' |
| 1589 | Incorrect loadtransient() call. |
| 1590 | Bad transient name '%s' |
| 1591 | Cannot use loadtransient() in the first frame of a level script. Use add_start() to setup start transient(s) instead. |
| 1592 | Unknown transient '%s' |
| 1593 | Incorrect unloadtransient() call. |
| 1594 | Bad transient name '%s' |
| 1595 | Unknown transient '%s' |
| 1596 | Incorrect unloadalltransients() call. |
| 1597 | Incorrect synctransients() call. |
| 1598 | Incorrect istransientqueued() call. |
| 1599 | Incorrect istransientqueued() call. |
| 1600 | Transient zone '%s' does not exist |
| 1601 | LoadStartPointTransientSet should only be called on a MapRestart. Talk to a coder before adding anything to call this. |
| 1602 | Incorrect LoadStartPointTransientSet() call. |
| 1603 | LoadStartPointTransients should only be called on a MapRestart. Talk to a coder before adding anything to call this. |
| 1604 | Incorrect LoadStartPointTransients() call. |
| 1605 | All array elements need to be strings. |
| 1606 | Bad transient name '%s' |
| 1607 | Unknown transient '%s' |
| 1608 | Transient '%s' listed twice in LoadStartPointTransients() call |
| 1609 | ClearStartPointTransients should only be called on a MapRestart. Talk to a coder before adding anything to call this. |
| 1610 | Incorrect ClearStartPointTransients() call. |
| 1611 | GetSavegameTransients should only be called on a MapRestart. Talk to a coder before adding anything to call this. |
| 1612 | Incorrect GetSavegameTransients() call. |
| 1613 | GetSavegameTransients should only be called on a MapRestart. Talk to a coder before adding anything to call this. |
| 1614 | Incorrect GetSavegameTransients() call. |
| 1615 | Incorrect SetTransientVisibility() call. |
| 1616 | Transient '%s' does not exist |
| 1618 | Incorrect number of parameters |
| 1619 | Incorrect number of parameters |
| 1620 | Incorrect number of parameters |
| 1621 | Incorrect number of parameters |
| 1622 | Unknown transient set '%s' |
| 1623 | Incorrect number of parameters |
| 1624 | Too few arguments |
| 1625 | Entity %i is not a target |
| 1626 | Too few arguments |
| 1627 | Entity %i is not a target |
| 1628 | Too few arguments |
| 1629 | Too few arguments |
| 1630 | Too few arguments |
| 1631 | Too few arguments |
| 1632 | Maximum number of targets exceeded |
| 1633 | Too few arguments |
| 1634 | Entity %i is not a target |
| 1635 | Too few arguments |
| 1636 | Entity %i is not a target |
| 1637 | Too few arguments |
| 1638 | entity %i is not a player |
| 1639 | FOV must be positive |
| 1640 | Entity %i is not a target |
| 1641 | Too few arguments |
| 1642 | Entity %i is not a target |
| 1643 | Incorrect mode name passed to target_setAttackMode(). |
| 1644 | Too few arguments |
| 1645 | Entity %i is not a target |
| 1646 | Too few arguments |
| 1647 | Entity %i is not a target. |
| 1648 | Entity %i is not a player entity. |
| 1649 | Too few arguments |
| 1650 | Entity %i is not a target |
| 1651 | Too few arguments |
| 1652 | Entity %i is not a target |
| 1653 | Too few arguments |
| 1654 | Entity %i is not a target |
| 1655 | Too few arguments |
| 1656 | Entity %i is not a target |
| 1657 | Too few arguments |
| 1658 | Entity %i is not a target |
| 1659 | Too few arguments |
| 1660 | Entity %i is not a target |
| 1661 | Too few arguments |
| 1662 | Entity %i is not a target |
| 1663 | Too few arguments |
| 1664 | Entity %i is not a target |
| 1665 | Delay value out of range (0 .. 1) %f |
| 1666 | Pause inner radius out of range (0 .. 1000) %f |
| 1667 | Pause outer radius out of range (0 .. 1000) %f |
| 1668 | Pause outer radius must be >= the inner radius (%f < %f) |
| 1669 | Pause inner radius wait time out of range (0 .. 1000) %f |
| 1670 | Too few arguments |
| 1671 | Entity %i is not a target |
| 1672 | Too few arguments |
| 1673 | Entity %i is not a target |
| 1674 | Too few arguments |
| 1675 | Entity %i is not a target |
| 1676 | Too few arguments |
| 1677 | Entity %i is not a target |
| 1678 | entity %u is not a player or agent |
| 1679 | not an entity |
| 1680 | Illegal call to %s(). Entity #%u with name '%s' is not a bot. |
| 1681 | Illegal call to BotSetFlag(). Invalid flag type '%s'. |
| 1682 | BotSetStance() - invalid stance '%s' |
| 1683 | Illegal call to BotSetScriptMove(). Requires yawAngle and seconds. |
| 1684 | Illegal call to BotLookAtPoint(). Requires <point> and <seconds>. |
| 1685 | Illegal call to BotLookAtPoint(). Type '%s' is not allowed |
| 1686 | Illegal call to BotLookAtPoint(). Bot at location (%f %f %f) looking directly up at point (%f %f %f) |
| 1687 | Illegal call to BotSetScriptGoal(). Radius '%f' is not >= 0 |
| 1688 | Illegal call to BotSetScriptGoal(). Type is required |
| 1689 | <objective_radius> parameter only works with 'objective'-type goals |
| 1690 | <objective_radius> parameter needs to be > 0 |
| 1691 | Illegal call to BotSetScriptGoalNode(). A path node is required. |
| 1692 | Illegal call to BotSetScriptGoalNode(). Node is invalid. |
| 1693 | Illegal call to BotSetScriptGoalNode(). Type is required |
| 1694 | <objective_radius> parameter only works with 'objective'-type goals |
| 1695 | <objective_radius> parameter needs to be > 0 |
| 1696 | BotSetScriptEnemy - cannot set an enemy of the same team |
| 1697 | BotSetPersonality - invalid personality '%s' |
| 1698 | BotSetDifficulty - invalid difficulty '%s' |
| 1699 | BotSetDifficultySetting - could not set '%s' to %s |
| 1700 | BotGetDifficultySetting - could not get '%s' |
| 1701 | Illegal call to BotSetPathingStyle(). Must pass a string value |
| 1702 | Illegal call to BotSetPathingStyle(). <%s> is not recognized |
| 1703 | Illegal call to BotSetAwareness(). <awareness> cannot be less than 0 |
| 1704 | Illegal call to BotPressButton(). Unsupported button '%s'. |
| 1705 | 'Invalid <time> parameter - needs to be >= 0 |
| 1706 | Illegal call to BotClearButton(). Unsupported button '%s'. |
| 1707 | BotThrowScriptedGrenade() Invalid grenade type '%s' |
| 1708 | BotFirstAvailableGrenade() requires grenade type 'lethal' or 'tactical' |
| 1709 | BotFirstAvailableGrenade() Invalid grenade type '%s' |
| 1710 | Illegal call to BotGetScriptGoal(). Bot '%s' has no Script Goal. |
| 1711 | Illegal call to BotGetScriptGoalNode(). Bot '%s' has no Script Goal. |
| 1712 | Illegal call to BotPursuingScriptGoal(). Bot '%s' has no Script Goal. |
| 1713 | BotGetWorldClosestEdge() requires vector for point if provided. |
| 1714 | Illegal call to BotNodeAvailable(). A path node is required. |
| 1715 | Illegal call to BotMemoryEvent(). Requires an event type string. |
| 1716 | Illegal call to BotMemoryEvent(). Invalid event type '%s'. |
| 1717 | Bot Memory Event 'known_enemy' needs location1 specified |
| 1718 | Bot memory event needs location1 specified |
| 1719 | Bot memory event needs location2 specified |
| 1720 | array is too large (%u > %zu) |
| 1721 | element %u of array: type %s is not an entity |
| 1722 | element %u of array is a despawned pathnode. |
| 1723 | element %u of array is not a valid pathnode |
| 1724 | Illegal call to BotNodePick(), BotNodePickMultiple(), or BotNodeScoreMultiple(). <nodes> array is empty or invalid |
| 1725 | Unknown stance. Must be 'stand', 'prone', or 'crouch' |
| 1726 | Illegal call to BotNodePick(), BotNodePickMultiple(), or BotNodeScoreMultiple(). Invalid score type '%s'. |
| 1727 | Score type '%s' requires vector parm: <direction> |
| 1728 | Score type '%s' requires vector parm: <point> |
| 1729 | Score type '%s' requires parms: <target_node>, <max_range> |
| 1730 | Score type '%s' requires node parms: <bait_node>, <entrance_node> |
| 1731 | Score type '%s' requires node parms: <point>, <stance> |
| 1732 | Illegal call to BotNodePick(), BotNodePickMultiple(), or BotNodeScoreMultiple(). Score type '%s' not supported for nodes. |
| 1733 | Illegal call to BotNodePick(), BotNodePickMultiple(), or BotNodeScoreMultiple(). Invalid score flag '%s'. |
| 1734 | Illegal call to BotNodePick(), BotNodePickMultiple(), or BotNodeScoreMultiple(). ignore_sky and ignore_no_sky are mutually exclusive. |
| 1735 | Illegal call to BotNodePickMultiple(). <top_pick_count> must be equal to or greater than <num_to_pick> |
| 1736 | Illegal call to BotNodePickMultiple(). <num_to_pick> must be > 0 |
| 1737 | Illegal call to BotPredictSeePoint(). Requires a point vector. |
| 1738 | Incorrect number of parameters |
| 1739 | Illegal call to BotGetMemoryEvents(). Invalid event type '%s'. |
| 1740 | Incorrect number of parameters |
| 1741 | Illegal call to BotGetMemoryEvents(). Invalid event type '%s'. |
| 1742 | Entity %d is not a bot or agent |
| 1743 | Invalid call to BotZoneSetTeam() in map without zones |
| 1744 | BotZoneSetTeam() requires <zone_index> and <team> |
| 1745 | unknown team '%s' |
| 1746 | Invalid call to BotZoneGetCount() in map without zones |
| 1747 | BotZoneGetCount() requires <zone_index>, <team>, <count_type> |
| 1748 | unknown team '%s' |
| 1749 | BotZoneGetCount() Invalid zone index: %d |
| 1750 | BotZoneGetCount() Invalid team: %s |
| 1751 | BotZoneGetCount() Invalid count type: %s |
| 1752 | Invalid call to BotZoneGetCount() in map without zones |
| 1753 | BotZoneGetCount() Invalid zone index: %d |
| 1754 | Invalid call to BotZoneNearestCount() in map without zones |
| 1755 | BotZoneNearestCount() requires <zone_index>, <team>, <maxSteps>, [<count_type>, <condition>, <value>], ... |
| 1756 | unknown team '%s' |
| 1757 | BotZoneNearestCount() Invalid zone index: %d |
| 1758 | BotZoneNearestCount() Invalid team: %s |
| 1759 | BotZoneNearestCount() limit of %d comparisons exceeded |
| 1760 | BotZoneNearestCount() Invalid count type: %s |
| 1761 | BotZoneNearestCount() Invalid compare type: %s |
| 1762 | BotGetTeamLimit() requires a team (0 or 1) |
| 1763 | BotGetTeamLimit() team must be 0 or 1 |
| 1764 | BotGetTeamDifficulty() requires a team (0 or 1) |
| 1765 | BotGetTeamDifficulty() team must be 0 or 1 |
| 1766 | unknown clip mode. |
| 1767 | Number value expected |
| 1768 | Invalid stance %s |
| 1769 | Invalid shootparams objective %s |
| 1770 | Invalid shootparams style %s |
| 1771 | %s "%s" not precached |
| 1772 | MayMoveFromPointToPoint: does not work on 3D actors. Use a nav trace (allow edge) instead. |
| 1773 | Script command is not supported yet. |
| 1774 | Attempting to set invalid animset %s |
| 1775 | Invalid state passed to SetLookAtStateOverride |
| 1776 | SetCivilianFocus requires a focus parameter between -1 and 1 |
| 1777 | SetAimState requires an integer argument |
| 1778 | not an spawner |
| 1779 | entity of type '%s', classname '%s', origin (%f, %f, %f) does not have an animation tree |
| 1781 | AnimScripted entities are not supported in this game mode |
| 1782 | entity (classname: '%s') does not currently support animscripted |
| 1783 | modelIndex [%d] must be greater than or equal to zero |
| 1784 | SetDroneTurnParams: entity not a vehicle! |
| 1785 | SetDroneGoalPos: There are no nav volumes loaded. Drones now require NavVolumes. |
| 1786 | VehCmd_SetDroneGoalPos: Vehicle must be a helicoptor. |
| 1787 | Usage: VehCmd_SetDroneGoalPos ( owner, offset ). |
| 1788 | VehCmd_SetDroneGoalPos: Owner entity is not a player |
| 1789 | VehCmd_SetDroneGoalPos: Speed and acceleration must not be zero before setting goal pos |
| 1790 | SetDroneGoalPos: There are no nav volumes loaded. Drones now require NavVolumes. |
| 1791 | VehCmd_SetDroneGoalPos: Vehicle must be a helicoptor. |
| 1792 | Usage: VehCmd_SetDroneGoalPos ( position, bStopAtGoal ). |
| 1793 | VehCmd_SetDroneGoalPos: Speed and acceleration must not be zero before setting goal pos |
| 1794 | G_VehicleScriptMPCmd_ApplyImpulseToDrone: Vehicle must be a helicopter. |
| 1795 | Usage: G_VehicleScriptMPCmd_ApplyImpulseToDrone ( velocity [, accumulates] [,decel_speed] ). |
| 1796 | G_VehicleScriptMPCmd_ApplyImpulseToDrone: Speed and acceleration must not be zero before setting goal pos |
| 1798 | PlayVehicleAnim - Feature Not Enabled |
| 1799 | VehicleShowOnMiniMap: function requires 1 parameter. |
| 1800 | usage: buttonPressed(<button name>) |
| 1801 | AllowSwim is not supported in this game mode |
| 1802 | Can't enable swimming when allowStand is set to false |
| 1803 | AllowSwimPredicted can only be called on a player or agent. |
| 1804 | AllowSwimPredicted is not supported in this game mode |
| 1805 | ZeroGrav is not supported in this game mode |
| 1806 | USAGE: AllowWalk( <can player walk> ) |
| 1807 | USAGE: SetMantleSoundFlavor( <plr_soundalias>, <npc_soundalias> ) |
| 1808 | unknown sound alias '%s' |
| 1809 | unknown sound alias '%s' |
| 1810 | Incorrect number of parameters |
| 1811 | Incorrect number of parameters |
| 1812 | Player is already driving a vehicle |
| 1813 | Entity is not a vehicle |
| 1814 | Player is not driving a vehicle |
| 1815 | Player is not linked |
| 1816 | Player is not linked to a vehicle |
| 1818 | Player is not on a vehicle. |
| 1819 | ForceViewmodelAnimation is not supported in this game mode |
| 1820 | Animation name "%s" is not supported by this function. |
| 1821 | EnableSlowAim is not supported in this game mode |
| 1822 | EnableSlowAim: Incorrect number of parameters ( %d ). |
| 1823 | DisableSlowAim is not supported in this game mode |
| 1824 | Incorrect number of parameters |
| 1825 | Too many parameters passed to PlayLocalSound |
| 1826 | unknown sound alias '%s' |
| 1827 | unknown sound alias '%s' |
| 1828 | Unknown soundalias. |
| 1829 | Incorrect number of parameters |
| 1830 | EnableQuickWeaponSwitch is not supported in this game mode |
| 1831 | Incorrect number of parameters |
| 1832 | Exceeded max trigger exception types, max is %d. |
| 1833 | All elements need to be strings. |
| 1834 | Incorrect number of parameters |
| 1835 | usage: SetLegsModel(<model name>) |
| 1836 | PlayerCmdSP_SetScriptedMeleeActive: Incorrect number of parameters |
| 1837 | PlayerCmdSP_SetHandsOccupied: Not supported in this mode |
| 1838 | PlayerCmdSP_SetHandsOccupied: Incorrect number of parameters |
| 1839 | PlayerCmdSP_GetHandsOccupied: Not supported in this mode |
| 1840 | PlayerCmdSP_SetNextBulletDryFire: Incorrect number of parameters |
| 1841 | ERROR: G_RagdollConstraintEntity_Spawn(), failed to find bone. Will not spawn. |
| 1842 | ERROR: G_RagdollConstraintEntity_Spawn(), failed to find valid boneName for hitLoc %i. Will not spawn. |
| 1843 | ERROR: G_RagdollConstraintEntity_Spawn(), failed to transform point into bone space for bone name %s. Will not spawn. |
| 1884 | ScriptableSv_UnlinkReservedScriptable: Trying to unlink a scriptable %s that we are updating. If the callstack shows the script VM and the scriptable update call, you should add a waitframe or similar. |
| 1885 | ScriptableSv_UnlinkReservedScriptable: Trying to unlink a scriptable %s that we are damaging. If the callstack shows the script VM and the scriptable update call, you should add a waitframe or similar. If not, this is probably a code bug. |
| 1903 | unknown variable |
| 1904 | unknown object |
| 1905 | self cannot be applied to %s |
| 1906 | thread not active |
| 1907 | self cannot be applied to %s |
| 1908 | parameter %d does not exist |
| 1909 | unknown variable |
| 1910 | thread not active |
| 1911 | thread not active |
| 1912 | unknown field |
| 1913 | unknown object |
| 1914 | unknown object |
| 1915 | thread not active |
| 1916 | bad expression |
| 1917 | thread not active |
| 1918 | bad expression |
| 1919 | not a field object |
| 1920 | unknown field |
| 1921 | thread not active |
| 1922 | cannot evaluate [] |
| 1923 | bad expression |
| 1924 | %s is not an object |
| 1925 | %s is not an entity |
| 1928 | player field %s is read-only |
| 1929 | Ran out of attractor/repulsors. Max allowed: %zu |
| 1930 | maxDist must be greater than zero |
| 1931 | maxDist must be greater than zero |
| 1932 | maxDist must be greater than zero |
| 1933 | maxDist must be greater than zero |
| 1934 | Invalid attractor or repulsor |
| 1935 | entity type '%s' is not a missile |
| 2027 | Scriptable %s tried to add a model, but the entity is in the ragdoll state |
| 2028 | Scriptable %s tried to remove a model, but the entity is in the ragdoll state |
| 2029 | bad escape character (%i) present in string |
| 2030 | %s is too long. Max length is %i |
| 2031 | %s is too long. Max length is %i |
| 2032 | %s is too long. Max length is %i |
| 2038 | GetAccuracyFraction <weapon> <distance> [use player accuracy]. |
| 2039 | "%s" weapon is not precached |
| 2040 | GetSpawnerStruct() requires two arguments (name, key) |
| 2041 | GetSpawner() used with more than one entity ("%s" = "%s") |
| 2042 | GetSpawnerArray must provide 0 arguments or a targetname. |
| 2043 | no team was specified - use getspawnerarray instead |
| 2044 | Depricated - use GetOrigin. |
| 2045 | Only Space enabled actors are supported for ForceMovingPlatformEntity |
| 2046 | ForceMovingPlatformEntity can only be called on players or actors |
| 2047 | SetSlowMotionView requires at least 1 parameter. |
| 2050 | Moving platform turn rates can only be set on clients. |
| 2051 | Rate not set. |
| 2052 | Trying to relative teleport entity that is linked to parent. Please teleport parent instead. |
| 2053 | %s can only be called on actor spawners attempted to call %s on entity with name '%s' of type '%s' at (%.0f %.0f %.0f) |
| 2054 | DoSpawn Unsupported class type %s |
| 2055 | MagicGrenade <grenade type> <origin> <target position> [time to blow (seconds)] [should throw]. |
| 2056 | "%s" grenade weapon is not precached |
| 2057 | Invalid grenade weapon for MagicGrenade |
| 2058 | BulletTracer called with unknown weapon |
| 2062 | unknown sound alias '%s' |
| 2063 | '%s' is a looping alias, use 'playloopsound' instead |
| 2064 | Sound Error |
| 2066 | unknown sound alias '%s' |
| 2067 | '%s' is a looping alias, use 'playloopsound' instead |
| 2091 | Must be called with a player. |
| 2092 | Must be called with a player. |
| 2093 | Type should be a sentient or a vehicle |
| 2094 | entity type '%s' is not a turret |
| 2095 | 'manual_target' is not support in SP |
| 2096 | Error setting the mode of a turret. |
| 2097 | entity type '%s' is not a turret |
| 2098 | entity type '%s' is not a turret |
| 2099 | entity type '%s' is not a turret |
| 2100 | entity type '%s' is not a turret |
| 2101 | entity type '%s' is not a turret |
| 2102 | entity type '%s' is not a turret |
| 2103 | entity type '%s' is not a turret |
| 2104 | entity type '%s' is not a turret |
| 2105 | entity type '%s' is not a turret |
| 2106 | entity type '%s' is not a turret |
| 2107 | usage: SetTurretFov( <fov> ) |
| 2108 | SetTurretFov: weapon isn't a turret. |
| 2109 | LerpFOV is not supported in this gamemode |
| 2110 | Not enough parameters. |
| 2111 | ModifyBaseFOV is not supported in this gamemode |
| 2112 | Not enough parameters. |
| 2113 | SetViewModelDepth can only be used for entities with models. |
| 2114 | makeFakeAI must be applied to a script_model |
| 2115 | spawnDrone can only be called on actor spawners attempted to call spawnDrone on entity with name '%s' of type '%s' at (%s) |
| 2116 | SetCorpseRemoveTimer must be called on a corpse |
| 2117 | GetCorpsePhysicsOrigin must be called on a corpse |
| 2118 | GetCorpsePhysicsOrigin must be called when dvar ai_corpseSynch enabled |
| 2119 | Too few arguments |
| 2120 | Must be called on a client entity. |
| 2121 | Non local players are not supported for this call. |
| 2122 | setspawnerteam can only be applied to AI spawners |
| 2123 | unknown team '%s' |
| 2124 | unknown team '%s' |
| 2125 | Time must be positive |
| 2126 | Blur value must be greater than 0 |
| 2127 | Attempting to save during a restart. |
| 2128 | Attempting to save multiple times in a single frame. |
| 2129 | Attempting to save while dead |
| 2130 | Attempting to save while dead |
| 2131 | Save Commit attempted with no valid committable save available |
| 2132 | Save Commit attempted on incorrect save. currentsave:%i, commit:%i |
| 2133 | Attempting to commit an invalid save |
| 2134 | illegal call to isturretactive() |
| 2135 | NULL turret passed to isturretactive |
| 2136 | frequencyroll must be >= 0 and <= 100 |
| 2137 | frequencyyaw must be >= 0 and <= 100 |
| 2138 | frequencypitch must be >= 0 and <= 100 |
| 2139 | radius must be >= 0 |
| 2140 | scalepitch must be >= 0 and <= 100 |
| 2141 | scaleyaw must be >= 0 and <= 100 |
| 2142 | scaleroll must be >= 0 and <= 100 |
| 2143 | duration must be > 0 |
| 2144 | usage: ScreenShake( <sourcePoint>, <scalepitch>, <scaleyaw>, <scaleroll>, <duration>, [durationfadeup], [durationfadedown], [radius], [frequency] ); |
| 2145 | In ScreenShake: [durationfadeup] + [durationfadedown] <= <duration>. |
| 2146 | ScreenShakeOnEntity called on a server only entity with classname %s |
| 2147 | frequencyroll must be >= 0 and <= 100 |
| 2148 | frequencyyaw must be >= 0 and <= 100 |
| 2149 | frequencypitch must be >= 0 and <= 100 |
| 2150 | radius must be >= 0 |
| 2151 | scalepitch must be >= 0 and <= 100 |
| 2152 | scaleyaw must be >= 0 and <= 100 |
| 2153 | scaleroll must be >= 0 and <= 100 |
| 2154 | duration must be > 0 |
| 2155 | usage: <ent> ScreenShakeOnEntity( <scalepitch>, <scaleyaw>, <scaleroll>, <duration>, [durationfadeup], [durationfadedown], [radius], [frequencypitch], [frequencyyaw], [frequencyroll] ); |
| 2156 | In ScreenShakeOnEntity: [durationfadeup] + [durationfadedown] <= <duration>. |
| 2157 | PrecacheNightvisionCodeAssets() must be called during level initialization. |
| 2158 | usage: DigitalDistortSetParams( <intensity>, <timeScale> ) |
| 2159 | intensity must be between 0 and 1 - %f |
| 2160 | timeScale must be between 0 and 100 - %f |
| 2161 | PrecacheMinimapSentryCodeAssets() must be called during level initialization. |
| 2162 | exitTime cannot be negative |
| 2163 | missionSuccess only takes two parameters: missionSuccess(nextMap, persistent) |
| 2164 | exitTime cannot be negative |
| 2165 | cinematicingame requires at least one parameter: cinematicingame(<cinematic name>, [pause_at_start]) |
| 2166 | cinematicingameloop takes at least one parameter: cinematicingameloop(<cinematic name>) |
| 2167 | CinematicInGameLoopResident takes one parameter: CinematicInGameLoopResident(<cinematic name>) |
| 2168 | stopcinematicingame takes no parameters: stopcinematicingame() |
| 2169 | pausecinematicingame takes one parameter: pausecinematicingame( <pause> ) |
| 2170 | Invalid parameters. |
| 2171 | Invalid parameters. |
| 2172 | IsMayhem() requires two arguments (name, key) |
| 2173 | IsMayhem() key must be 'targetname' (for now) |
| 2174 | Invalid parameters. |
| 2175 | Invalid parameters. |
| 2176 | Invalid parameters. |
| 2177 | Invalid parameters. |
| 2178 | Incorrect number of parameters |
| 2179 | Incorrect number of parameters |
| 2180 | Incorrect number of parameters |
| 2181 | Incorrect number of parameters |
| 2182 | Incorrect number of paramters |
| 2184 | Incorrect number of parameters |
| 2185 | Incorrect number of parameters |
| 2186 | Incorrect number of parameters |
| 2187 | Incorrect number of parameters |
| 2188 | Lerp time must be greater than 1 ms |
| 2189 | Incorrect number of parameters: (<angles now>, <angles end>, <lerp time>, [accel time], [decel time]) |
| 2190 | Lerp time must be greater than 1 ms |
| 2191 | Lerp time must be greater than accel and decel time combined |
| 2192 | Neither accel nor decel time can be negative |
| 2193 | Incorrect number of parameters |
| 2194 | Incorrect number of parameters |
| 2195 | Incorrect number of parameters |
| 2196 | invalid velocity parameter in launch command: %f %f %f |
| 2197 | Entity is not a sound_blend |
| 2198 | Lerp must be between 0.0f and 1.0f |
| 2199 | SetLightRadius only works for lights with maxmove or maxturn KVP specified in Radiant |
| 2200 | ForceDeathFall only supports actor entities. |
| 2201 | SetLightFovRange only works for lights with maxmove or maxturn KVP specified in Radiant |
| 2202 | outer fov must be in the range of 1 to 120 |
| 2203 | outer fov cannot be larger than the fov when the map was compiled |
| 2204 | inner fov must be in the range of 0 to outer fov |
| 2205 | GScr_StartRagdollFromImpact requires at least 2 parameters. |
| 2206 | GScr_StartRagdollFromImpact must be called on an actor. |
| 2207 | HitLoc parameter must be a valid hit location string e.g. 'torso_upper' |
| 2208 | USAGE: notifyOnCommand( <notify>, <command> ) |
| 2210 | SetLaserMaterial takes two parameters (laser material name, light material name) |
| 2211 | PushPlayerVector is not supported in this game mode |
| 2212 | PushPlayerVector must be called on a player entity |
| 2213 | PushPlayerVector first parameter must be a vector |
| 2214 | PushPlayerVector second parameter must be a boolean |
| 2215 | IsEnemyAware only supports actor entities. |
| 2216 | HasEnemyBeenSeen only supports actor entities. |
| 2217 | HasEnemyBeenSeen takes a single parameter (time in ms). |
| 2218 | Wrong number of parameters to SetHUDDynLight( <fade time>, <color> ) |
| 2219 | May not call SetMotionTrackerVisible on a player. |
| 2220 | May not call SetMotionTrackerVisible on an actor. |
| 2221 | May not call GetMotionTrackerVisible on a player. |
| 2222 | May not call GetMotionTrackerVisible on an actor. |
| 2223 | AddOnToViewModel called on an ent that's not a player. |
| 2224 | AddOnToViewModel needs a model name and bone name. |
| 2225 | Player is not linked. |
| 2226 | Player is not linked. |
| 2227 | Player is not linked. |
| 2228 | Invalid entity to set a streaming origin on |
| 2229 | Only view unlinks from ent to player are allowed. |
| 2230 | Could not find entity to remove from player view link. |
| 2231 | Only view links from ent to player are allowed. |
| 2232 | Only script model ents can be view linked. |
| 2233 | Cannot link entity, MAX_VIEW_LINKED_ENTITIES ents has been reached. |
| 2234 | Cannot view link entity, it is already linked. |
| 2235 | This game mode does not support view linked entities |
| 2236 | Unrecognized proceduralMotion type provided to LinkToPlayerView(). |
| 2237 | This game mode does not support view linked entities |
| 2238 | Unrecognized proceduralMotion type provided to LinkToPlayerView(). |
| 2239 | This game mode does not support view linked entities |
| 2240 | BlendLinkToPlayerViewMotion() must be called on a player entity. |
| 2241 | This game mode does not support view linked entities |
| 2242 | LinkToPlayerViewFollowRemoteEyes() must be called on a player entity. |
| 2243 | This game mode does not support view linked entities |
| 2244 | Sound error. Wrong number of parameters to '%s'. |
| 2245 | Sound error. First argument for '%s' must be greater than zero. |
| 2246 | Sound error. First argument for '%s' must be less than %.2f. |
| 2247 | Sound error. Second argument for '%s' must be greater than zero. |
| 2248 | Sound error. Second argument for '%s' must be less than %.2f. |
| 2249 | Must be called on a client entity. |
| 2250 | Non local players are not supported for this call. |
| 2251 | Must be called on a client entity. |
| 2252 | Non local players are not supported for this call. |
| 2253 | Must be called on a client entity. |
| 2254 | Non local players are not supported for this call. |
| 2255 | Must be called on a client entity. |
| 2256 | Non local players are not supported for this call. |
| 2257 | SetYoloState must be provided a state between 0-3 inclusive, recieved %d |
| 2258 | Currently only supported on script movers and entities with brush models |
| 2259 | Currently only supported on script movers and entities with brush models |
| 2260 | GetLocalPlayerProfileData() must be called on a client entity |
| 2261 | GetLocalPlayerProfileData() requires the name of the setting |
| 2262 | Cannot get buffer from profile |
| 2263 | Invalid profile data %s for GetLocalPlayerProfileData() |
| 2264 | SetLocalPlayerProfileData() must be called on a client entity |
| 2265 | SetLocalPlayerProfileData() requires the name of the setting and value |
| 2266 | invalid setting %s for SetLocalPlayerProfileData() |
| 2267 | Cannot set buffer in profile |
| 2268 | Unsupported data type for SV_Game_RecordDemo_GetPlayerProfileData |
| 2269 | SpringCam is not supported in this game mode |
| 2270 | Cannot turn on spring camera unless player is linked. |
| 2271 | Cannot turn on spring camera when angles are locked. |
| 2272 | Spring camera view range cannot be more than %f. |
| 2273 | springReleaseLerpSpeed cannot be greater than springLerpSpeed. |
| 2274 | SpringCam is not supported in this game mode |
| 2275 | Cannot turn off spring camera unless player is linked. |
| 2276 | Must be called on a client entity. |
| 2277 | Non local players are not supported for this call. |
| 2278 | Must be called on a client entity. |
| 2279 | Non local players are not supported for this call. |
| 2280 | Must be called on a client entity. |
| 2281 | Non local players are not supported for this call. |
| 2282 | Must be called on a client entity. |
| 2283 | Non local players are not supported for this call. |
| 2284 | Sound %s was not found. |
| 2285 | GetPlayerSetting() only supports the "gameskill" setting |
| 2287 | GetPlayerSetting must be called on a client entity |
| 2288 | GetPlayerSetting() requires the name of the setting |
| 2289 | %s is not a valid setting name! |
| 2301 | SetGrappleVolume is only valid on info_volume_grapple; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 2302 | SetGrappleRoundVolume is only valid on info_volume_grapple; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 2303 | SetGrappleActor can only be set on an actor. |
| 2304 | SetVolumeWalkingEnabled can only be set on an info_volume_grapple. |
| 2305 | SetVolumeSnapEnabled can only be set on an info_volume_grapple. |
| 2306 | SetVolumeGrappleDisable can only be set on an info_volume_grapple. |
| 2307 | can only be set on info_volume_grapple entities. |
| 2308 | normalizedVector must be a vector or undefined. |
| 2309 | normalizedVector is required. |
| 2310 | SetModelUsesMaterialOverride() must be called on a script mover. |
| 2311 | SetModelUsesMaterialOverride() must be called on an ent that has had SetOtherEnt set to the owner. |
| 2312 | SetRagdollNoBloodPoolFx can only be set on an actor or corpse. |
| 2313 | self.health must be greater than 0 (tried to set %i on ent %i, name %s) |
| 2314 | IsPlatformConsole can only be called from SP. |
| 2315 | IsPlatformPC can only be called from SP. |
| 2316 | IsPlatformPS4 can only be called from SP. |
| 2317 | IsPlatformXB3 can only be called from SP. |
| 2320 | You have already preloaded a set of fastfiles. You can only issue one PreloadZones() at a time. |
| 2321 | PreloadZones should be called with one parameter. A string, or an array. |
| 2322 | Cannot use preloadzones() in the first frame of a level script. Wait at least a frame after your start point. |
| 2323 | All array elements need to be strings. |
| 2324 | Incorrect WasPreloadZonesStarted() call. |
| 2325 | Incorrect IsPreloadZonesComplete() call. |
| 2327 | physicslaunch called more than once for the same entity. |
| 2328 | Cannot set slide velocity on an entity which is not doing MoveSlide() |
| 2329 | USAGE: spawn( "script_weapon", <origin>, <spawnflags>, <forceNoCollision>, <weaponNameOrId> ) |
| 2330 | Invalid index specified for client weapon - valid range is [0,%d) value is %d |
| 2331 | USAGE: spawn( "script_weapon", <origin>, <spawnflags>, <weaponNameOrId> ) INVALID ARGUMENT: weaponNameOrId must be a string name of a weapon or an index specifying which client weapon to use |
| 2332 | total time must be positive |
| 2333 | accel time must be nonnegative |
| 2334 | decel time must be nonnegative |
| 2335 | accel time plus decel time is greater than total time |
| 2336 | Cannot set an entity's position while it is playing a delta animation. Call scriptmodelclearanim first. |
| 2337 | PhysicsWarpTo operating on gentity with no physics instance. |
| 2338 | PhysicsWarpTo operating on gentity with no detail physics instance. |
| 2339 | invalid velocity parameter in movegravity command: %f %f %f |
| 2340 | Cannot set an entity's position while it is playing a delta animation. Call scriptmodelclearanim first. |
| 2341 | invalid velocity parameter in moveslide command: %f %f %f |
| 2342 | Cannot set an entity's position while it is playing a delta animation. Call scriptmodelclearanim first. |
| 2343 | Cannot set an entity's position while it is playing a delta animation. Call scriptmodelclearanim first. |
| 2344 | Cannot set an entity's position while it is playing a delta animation. Call scriptmodelclearanim first. |
| 2345 | %s is not supported in this game mode |
| 2346 | %s: only use this function on linked entities. |
| 2347 | %s: only supporting script models & script origins at the moment. |
| 2348 | %s: not supporting animscripted entities at the moment. |
| 2349 | Cannot set an entity's angles while it is playing a delta animation. Call scriptmodelclearanim first. |
| 2350 | Cannot set an entity's angles while it is playing a delta animation. Call scriptmodelclearanim first. |
| 2351 | Cannot set an entity's angles while it is playing a delta animation. Call scriptmodelclearanim first. |
| 2352 | G_ScrMover_ProtoAddRotate: expect exactly one parameter. |
| 2353 | Cannot set an entity's angles while it is playing a delta animation. Call scriptmodelclearanim first. |
| 2354 | Cannot set an entity's angles while it is playing a delta animation. Call scriptmodelclearanim first. |
| 2355 | illegal call to vibrate() |
| 2356 | Cannot set an entity's angles while it is playing a delta animation. Call scriptmodelclearanim first. |
| 2357 | Cannot set an entity's angles while it is playing a delta animation. Call scriptmodelclearanim first. |
| 2358 | illegal call to setcandamage() |
| 2359 | not an entity |
| 2360 | illegal call to setcanradiusdamage() |
| 2361 | not an entity |
| 2362 | Cannot set an entity's position while it is playing a delta animation. Call scriptmodelclearanim first. |
| 2363 | PhysicsLaunchClient only supports entities with 1 rigid body - %s has %i. |
| 2364 | physicsLaunchServer valid only for script models and script brush models |
| 2365 | Ent does not have a model and is not a brush. Cannot create physics for an ent without a model or that is a brush. |
| 2366 | Cannot set an entity's position while it is playing a delta animation. Call scriptmodelclearanim first. |
| 2367 | PhysicsLaunchServer only supports entities with 1 rigid body - %s has %i. |
| 2368 | physicsLaunchServer failed to create physics |
| 2369 | PhysicsStopServer valid only for script models and script brush models |
| 2370 | Ent does not have a model and is not a brush. Cannot stop physics for an ent without a model or that is a brush. |
| 2371 | You can only call PhysicsStopServer on an entity which PhysicsLaunchServer is executed upon. |
| 2372 | not an entity |
| 2373 | physicsLaunchServerItem valid only for weapons/items |
| 2374 | Ent does not have a model. Cannot create physics for an ent without a model. |
| 2375 | Cannot set an entity's position while it is playing a delta animation. Call scriptmodelclearanim first. |
| 2376 | physicsLaunchServerItem failed to create physics |
| 2377 | CloneBrushmodelToScriptmodel valid only for script models |
| 2378 | Entity has no brushmodel |
| 2379 | entity %i is not a script_brushmodel, script_model, script_origin, light, script_vehicle or misc_turret |
| 2380 | not an entity |
| 2381 | entity %i is not a script_brushmodel, script_model, script_origin, light, script_vehicle or misc_turret |
| 2382 | not an entity |
| 2383 | Must be called on a script_weapon. |
| 2384 | Must be called on a script_weapon. |
| 2385 | USAGE: SetClientWeapon( <weaponNameOrId> ) |
| 2386 | Invalid index specified for client weapon - valid range is [0,%d) value is %d |
| 2387 | USAGE: spawn( "script_weapon", <origin>, <spawnflags>, <weaponNameOrId> ) INVALID ARGUMENT: weaponNameOrId must be a string name of a weapon or an index specifying which client weapon to use |
| 2388 | Calling SEtMoverBehavior on entity that is not a mover. Entity number %d |
| 2389 | Undefined mover behavior %s |
| 2390 | Can't set the body model '%s' to a transient model from the server |
| 2391 | Can't set the head model '%s' to a transient model from the server |
| 2392 | Cannot remove an entity's model while it is playing an animation. Call scriptmodelclearanim first. |
| 2393 | Trying to set model for entity in a way where the existing child models will no longer have the correct attach bones, this will cause child models to appear in front of the camera. %s |
| 2394 | not a sentient |
| 2395 | GetEnemySqDist is depricated, use GetClosestEnemySqDist. |
| 2396 | createthreatbiasgroup [name] |
| 2397 | threatbiasgroupexists [name] |
| 2398 | getthreatbias [group for] [group against] |
| 2399 | Invalid threat bias group '%s'. |
| 2400 | Invalid threat bias group '%s'. |
| 2401 | setthreatbias [group for] [group against] [threat] |
| 2402 | Invalid threat bias group '%s'. |
| 2403 | Invalid threat bias group '%s'. |
| 2404 | setthreatbiasagainstall [group for] [threat] |
| 2405 | Invalid threat bias group '%s'. |
| 2406 | setignoremegroup [group for] [group ignoring] |
| 2407 | Invalid threat bias group '%s'. |
| 2408 | Invalid threat bias group '%s'. |
| 2409 | Invalid threat bias group '%s'. |
| 2410 | <otherSentient> must be a sentient entity |
| 2411 | <otherSentient> must be sentient |
| 2412 | <otherSentient> must be a sentient entity |
| 2413 | <otherSentient> must be sentient |
| 2414 | isEnemyTeam [team1] [team2] |
| 2415 | unknown team '%s' |
| 2416 | unknown team '%s' |
| 2417 | Do not use makeEntitySentient on players |
| 2418 | Do not use makeEntitySentient on actors |
| 2419 | Do not use makeEntitySentient on agents |
| 2420 | unknown team '%s' |
| 2421 | Too many non-expendable sentients. Unable to make entity %d a sentient (see log for full sentients info) |
| 2422 | Entity %d being made a sentient, but physics are not properly initialized. |
| 2423 | Do not use FreeEntitySentient on players |
| 2424 | Do not use FreeEntitySentient on actors |
| 2425 | Do not use FreeEntitySentient on agents |
| 2426 | 'LastKnownTime' must be called on sentients |
| 2427 | 'LastKnownTime' must be passed a sentient |
| 2428 | 'LastKnownPos' must be called on sentients |
| 2429 | GetDropToFloorPosition supported for players and AI only. |
| 2430 | Must provide a string value for <hitLocDamageTable> |
| 2431 | Cannot find %s Hit Location Damage Table in hardcoded table array. Using default dmg table. |
| 2432 | other ent must be a sentient |
| 2433 | GetEnemyInfo must be called on sentient AI entity (Actor or Bot) |
| 2434 | CopyEnemyInfo can only be called on a sentient AI |
| 2435 | CopyEnemyInfo can only copy from a sentient |
| 2436 | DLog_RecordEvent field '%s' unhandled value type '%s' |
| 2437 | DLog_RecordEvent %s[%d] expects string name but found '%s' |
| 2438 | DLog calls from GSC expect two arguments |
| 2439 | DLog calls from GSC expect string event name |
| 2440 | DLog calls from GSC expect array keyvals |
| 2441 | DLog_ProcessGSCScriptArgs keyvals[%d] expects string name but found %s |
| 2443 | DLog_RecordEvent value of '%s' is neither an array or row |
| 2444 | sentient property '%s' is read-only |
| 2445 | IsRequestedStance: Invalid stance %s |
| 2446 | Unknown shoot style |
| 2447 | ShootUpdateParams: ent %d shootparams invalid |
| 2448 | Animset %s state %s - unable to find alias %s |
| 2449 | Animset %s state %s - alias name %s_%s exceeds max allowed length %d |
| 2450 | Animset %s state %s - unable to find alias %s |
| 2451 | Animset %s state %s - unable to find appropriate shoot anim for %d shots |
| 2452 | Illegal call to setnodepriority |
| 2453 | Cannot enable disable priority for this node type. Priority can only be set for : |
| 2454 | Lists each pathnode type that supports enable/disable priority after an unsupported-type error |
| 2455 | illegal call to isnodeoccupied() |
| 2456 | Wrong number of arguments to setturretnode |
| 2457 | Can only set arc angle for node_turret |
| 2458 | Entity is not a turret |
| 2459 | USAGE : Must have a node_turret as a parameter. |
| 2460 | Can only do this call for node_turret |
| 2461 | SpawnCoverNode: Unrecognized node type %s. |
| 2462 | SpawnCoverNode: node type %s must have the 'animscript' parameter passed-in. |
| 2463 | DespawnCoverNode: invalid node index %d. |
| 2464 | DespawnCoverNode: Attempting to delete a node that was not spawned with SpawnCoverNode(). |
| 2465 | pathnode property '%s' is read-only |
| 2466 | pathnode property '%s' must be non-negative |
| 2467 | pathnode property '%s' must be non-negative for pathnode '%s' |
| 2468 | G_SetPathnodeScriptVariable: vector is an unsupported script variable type for pathnodes |
| 2469 | key is not internally a string |
| 2470 | getnode used with more than one node |
| 2471 | key '%s' does not internally belong to nodes |
| 2472 | key is not internally a string |
| 2473 | NodesVisible() requires pathnodes nodeA and nodeB |
| 2474 | NodesVisible() requires two valid pathnodes nodeA and nodeB |
| 2475 | GetNodesOnPath() requires vectors <start> and <end> |
| 2477 | Invalid call to GetNodeZone() in map without zones |
| 2478 | Invalid call to GetZoneOrigin() in map without zones |
| 2479 | Invalid zone for GetZoneOrigin(zone:%d) |
| 2480 | Invalid call to GetZoneNodes() in map without zones |
| 2481 | Invalid zone for GetZoneNodes(zone:%d, maxSteps:%d) |
| 2482 | Invalid call to GetZoneNodesByDist() in map without zones |
| 2483 | Invalid zone for GetZoneNodesByDist(zone:%d, dist:%f) |
| 2484 | Invalid call to GetZoneNodeForIndex() in map without zones |
| 2485 | Invalid call to GetZonePath() in map without zones |
| 2486 | GetZonePath() requires <zone_index_start> and <zone_index_end> |
| 2487 | GetZonePath(): Invalid <zone_index_start> %d |
| 2488 | GetZonePath(): Invalid <zone_index_end> %d |
| 2494 | close list included an index that is not a pathnode |
| 2495 | far list included an index that is not a pathnode |
| 2496 | spawnPointNodes parameter too large to buffer, increase MAX_SPAWN_PATHNODE_LIST or reduce the number of spawn points in this map! |
| 2497 | path node list included an index that is not a pathnode |
| 2498 | precomputedlosdatatest requires non empty node lists, this means a entity in the map has no path node nearby! |
| 2499 | precomputedlosdatatest requires non empty node lists, this means a spawn point in the map has no path node nearby! |
| 2500 | precomputedlosdatatest requires the system to be enabled with dvar sv_usePrecomputedLOSData set via the playlist |
| 2501 | precomputedlosdatatest requires a generated data file for LOS data, this map appears to not have one, script should call GetIsLOSDataFileLoaded before precomputedlosdatatest! |
| 2502 | entity path node list included an index that is not a pathnode |
| 2503 | spawn point path node list included an index that is not a pathnode |
| 2504 | list A included an index that is not a pathnode |
| 2505 | list B included an index that is not a pathnode |
| 2506 | GetValidCoverPeekOuts: called on despawned pathnode. |
| 2507 | GetValidCoverPeekOuts not called on pathnode. |
| 2508 | GetValidCoverMultiNodeTypes: called on despawned pathnode. |
| 2509 | GetValidCoverMultiNodeTypes: not called on Cover Multi node. |
| 2510 | GetValidCoverMultiNodeTypes: not called on pathnode. |
| 2511 | IsNodeLMGMountable: called on despawned pathnode. |
| 2512 | IsNodeLMGMountable not called on pathnode. |
| 2513 | GetHighestNodeStance: called on despawned pathnode. |
| 2514 | GetHighestNodeStance not called on pathnode. |
| 2516 | DoesNodeAllowStance: called on despawned pathnode. |
| 2517 | DoesNodeAllowStance not called on pathnode. |
| 2518 | Dynamic Peekouts disabled |
| 2519 | SetCoverMultiNodeType : This function must be called only on cover multi nodes. |
| 2520 | SetCoverMultiNodeType : Unsupported node type : %s |
| 2521 | GetNodeFromEntref: called on despawned node. |
| 2522 | Not a pathnode |
| 2523 | GetPathnode: called on despawned node. |
| 2524 | not a pathnode |
| 2525 | GetPathnode: called on despawned node. |
| 2526 | not a pathnode |
| 2527 | devGetOmnvar - Omnvar type is unsupported! |
| 2536 | Parameter (%s) must be an array |
| 2537 | Unable to find the lookup table '%s' in the fastfile |
| 2538 | divide by 0 |
| 2539 | %g out of range |
| 2540 | %g out of range |
| 2541 | cannot cast %s to int |
| 2542 | cannot cast %s to float |
| 2543 | The two points on the line must be different from each other |
| 2544 | Line segment must not have zero length |
| 2545 | wrong number of arguments to vectornormalize! |
| 2546 | Wrong number of arguments to GenerateAxisFromForwardVector |
| 2547 | Wrong number of arguments to GenerateAxisFromUpVector |
| 2548 | wrong number of arguments to vectortoyaw! |
| 2549 | wrong number of arguments to vectortopitch! |
| 2550 | wrong number of arguments to vectorlerp |
| 2551 | Zero length vector was passed as first parameter! |
| 2552 | An empty array is not valid. |
| 2553 | All elements need to be 3d points. |
| 2554 | Incorrect number of parameters for ProjectileIntercept. |
| 2555 | wrong number of arguments to anglestoup! |
| 2556 | Wrong number of arguments to AngleLerpQuat( <startAngles>, <end>, <maxAngleDelta> ) |
| 2557 | Wrong number of arguments to AngleLerpQuatFrac( <startAngles>, <end>, <fraction> ) |
| 2558 | wrong number of arguments to axistoangles |
| 2559 | wrong number of arguments to vectortoangle! |
| 2560 | string too long |
| 2561 | string too long |
| 2562 | string too long |
| 2563 | assert fail |
| 2564 | assert fail: %s |
| 2565 | assert fail: %s |
| 2566 | USAGE: tableLookup( filename, searchColumnNum, searchValue, returnValueColumnNum ) |
| 2567 | TableLookupByRow: Unable to find the lookup table '%s' in the fastfile |
| 2568 | USAGE: tableLookupByRow( filename, rowNum, returnValueColumnNum ) |
| 2569 | USAGE: tableLookup( filename, searchColumnNum, searchValue, returnValueColumnNum ) |
| 2570 | USAGE: tableLookupByRow( filename, rowNum, returnValueColumnNum ) |
| 2571 | USAGE: tableLookupRowNum( filename, searchColumnNum, searchValue ) |
| 2572 | Unable to find the lookup table '%s' in the fastfile |
| 2573 | TableExists() called with no table name id. |
| 2574 | USAGE: tableLookupGetNumRows( filename ) |
| 2575 | TableLookupGetNumRows: Unable to find the lookup table '%s' in the fastfile |
| 2576 | USAGE: tableLookupGetNumCols( filename ) |
| 2577 | TableLookupGetNumCols: Unable to find the lookup table '%s' in the fastfile |
| 2578 | USAGE: TableLookupRowNum_StartFromRow( filename, searchColumnNum, searchValue, index ) |
| 2579 | TableLookupRowNum_StartFromRow: Unable to find the lookup table '%s' in the fastfile |
| 2580 | USAGE: TableLookupRowNum_ReverseFromRow( filename, searchColumnNum, searchValue, index ) |
| 2581 | TableLookupRowNum_ReverseFromRow: Unable to find the lookup table '%s' in the fastfile |
| 2582 | EasePower() requires a percentage parameter. |
| 2583 | EaseExponential() requires a percentage parameter. |
| 2584 | EaseLogarithmic() requires a percentage parameter. |
| 2585 | EaseSine() requires a percentage parameter. |
| 2586 | EaseBack() requires a percentage parameter. |
| 2587 | EaseElastic() requires a percentage parameter. |
| 2588 | EaseBounce() requires a percentage parameter. |
| 2589 | Expected at least 5 arguments, USAGE: SpawnScriptItem( classname, origin, angles, spawnflags, model, optional:hintstring, optional:impulseVector, optional:impulseContactPoint ) |
| 2590 | Error spawning script item, no model name provided |
| 2591 | Error spawning script_item at pos %s, it is neither touchable or usable |
| 2592 | failed to create physics for script item |
| 2593 | ERROR: script_item - at pos %s is in solid, item not created. |
| 2594 | Cannot change a 'willNeverChange' entity |
| 2595 | Owner entity is not a player |
| 2596 | Can only be called on a script mover or player |
| 2597 | Could not register entity as bot affecter |
| 2598 | Cannot change a 'willNeverChange' entity |
| 2599 | Can only be called on a script mover or player |
| 2600 | unknown team '%s' |
| 2601 | Invalid blend type. Valid options include 'normal', 'slow' and 'none'. |
| 2602 | start time must be non-negative |
| 2603 | Spawn() with 'script_arms' given client %d, which is not currently connected! |
| 2604 | Cannot change a 'willNeverChange' entity |
| 2605 | <anim name> needs to be a string or an animation |
| 2606 | MP Anim [%s] was not precached. |
| 2608 | Cannot play an animation on an entity that already has an animated trajectory. Call scriptmodelclearanim first. |
| 2609 | ScriptModelPlayAnim: animRate %f not valid. Needs to be between %f and %f and rounded to %i decimal places |
| 2610 | ScriptModelPlayAnim: <anim name> needs to be a string or an animation |
| 2611 | ScriptModelPlayAnim: <notify name> needs to be a string |
| 2612 | ScriptModelPlayAnim: Entity needs model to play animation. |
| 2613 | Entity needs model to play delta motion animation. |
| 2614 | ScriptModelPlayAnimDeltaMotion: <anim name> needs to be a string or an animation |
| 2615 | ScriptModelPlayAnimDeltaMotion: start time must be non-negative |
| 2616 | ScriptModelPlayAnimDeltaMotionFromPos: <anim name> needs to be a string |
| 2617 | ScriptModelPlayAnimDeltaMotionFromPos: start time must be non-negative |
| 2619 | Entity needs model to clear animation. |
| 2621 | Entity needs model to pause animation. |
| 2622 | Entity needs to be playing an anim to pause. |
| 2623 | Animation is already paused. |
| 2624 | Animation is not currently paused. |
| 2625 | Unable to find AI event for [%s] |
| 2626 | Max listeners exceeded; entity id: %d |
| 2627 | unknown team '%s' |
| 2628 | unknown team '%s' |
| 2629 | unknown team '%s' |
| 2630 | unknown team '%s' |
| 2632 | Spawn factor weights less than 0 are prohibited |
| 2633 | unknown team '%s' |
| 2639 | Unknown means of death "%s" |
| 2640 | Unknown hit location "%s" |
| 2645 | Vehicle field %s is read-only |
| 2646 | Trying to set vehicle transmission to '%s'. Must be 'forward' or 'reverse'. |
| 2647 | Trying to set vehicle path direction to '%s'. Must be 'forward' or 'reverse'. |
| 2648 | This vehicle does not use physics. Path type '%s' is allowed only on vehicles that use physics |
| 2649 | '%s' is an illegal path type. Must be '%s' or '%s' |
| 2650 | Top speed must not be negative |
| 2651 | Braking can only be set on physics vehicles |
| 2652 | Brake must be a value from 0 to 1 |
| 2653 | Throttle can be set only on physics vehicles |
| 2654 | Throttle must be nonnegative |
| 2655 | Throttle can be set only on vehicles on a path in follow mode. |
| 2656 | Not a valid entity |
| 2657 | Trying to call a vehicle command on a spawner. Entity '%s' Classname '%s' at (%.0f %.0f %.0f) |
| 2658 | Trying to call a vehicle command on a non-script_vehicle entity. Entity '%s' Classname '%s' at (%.0f %.0f %.0f) |
| 2659 | not an entity |
| 2660 | Trying to call a vehicle spawner command on a vehicle. Entity '%s' Classname '%s' at (%.0f %.0f %.0f) |
| 2661 | Trying to call a vehicle spawner command on a non-script_vehicle entity. Entity '%s' Classname '%s' at (%.0f %.0f %.0f) |
| 2662 | Owner entity is not a player |
| 2663 | A vehicle of type %s spawned at (%f, %f, %f ) when its model %s was not loaded. |
| 2664 | Vehicle_IsOnGround() must be called on a physics enabled vehicle |
| 2665 | Vehicle_GetSpawnerArray() must provide 0 arguments or a targetname. |
| 2666 | AttachPath: Unable to attach to path node |
| 2667 | GetAttachPos: Unable to attach to path node |
| 2668 | StartPath: Unable to attach to path node |
| 2669 | StartPath: Can't start vehicle on path. Vehicle must already be attached if no node is specified |
| 2670 | StartPathFind: Only available for physics vehicles |
| 2671 | StartPathFind: A goal position is needed |
| 2672 | Cannot have a negative wait speed on a vehicle |
| 2673 | Cannot set negative speed on vehicle |
| 2674 | Acceleration must be > 0 |
| 2675 | Acceleration must be > 0 when setting speed |
| 2676 | Deceleration must be > 0 |
| 2678 | Vehicle_RotateYaw() only works with tanks on paths |
| 2679 | Accel must be positive |
| 2680 | Cannot set negative acceleration on vehicle |
| 2681 | Vehicle must be a helicopter. |
| 2682 | Overshoot must be in 0 to 1 range |
| 2683 | Cannot set negative yaw speed on vehicle |
| 2684 | Cannot set negative yaw acceleration on vehicle |
| 2685 | Cannot set zero yaw deceleration on vehicle |
| 2686 | Max Pitch Roll only valid for helicopters |
| 2687 | Cannot set negative max pitch |
| 2688 | Cannot set negative max roll |
| 2689 | Max air resistance speed must be >= 1 MPH |
| 2690 | Speed fraction must be between [0,1] |
| 2691 | Deceleration can't be negative |
| 2692 | Can't make corpse on a vehicle that a player is using |
| 2693 | GetWheelSurface() is not set up to work with vehicle type [%s] |
| 2694 | Valid wheel names are: [front_left, front_right, back_left, back_right, middle_left, middle_right] |
| 2695 | Vehicle has no middle wheels |
| 2696 | unknown team '%s' |
| 2697 | Vehicle must be a helicopter. |
| 2698 | Speed and acceleration must not be zero before setting goal pos |
| 2737 | Vehicle must be a helicopter. |
| 2738 | Vehicle must be a helicopter. |
| 2739 | Vehicle must be a helicopter. |
| 2740 | Vehicle must be a helicopter. |
| 2741 | Vehicle must be a helicopter. |
| 2742 | Can't set target position on player's vehicle |
| 2743 | Vehicle must have health to control the turret |
| 2744 | Vehicle must have health to control the turret |
| 2745 | Vehicle has no tag [tag_barrel] |
| 2746 | Can't set target on player's vehicle |
| 2747 | Vehicle must have health to control |
| 2748 | Invalid entity |
| 2751 | SetVehicleDef must be called on a vehicle. |
| 2752 | SetVehicleDef: Can't find vehicle def '%s' |
| 2753 | Vehicle must have health to control the turret |
| 2754 | Invalid weapon specified for [%s] |
| 2755 | Vehicles only support bullet and projectile weapons |
| 2756 | No tag_barrel for [%s] |
| 2757 | unknown tag '%s' |
| 2758 | No tag %s for [%s] |
| 2759 | No %s for [%s] |
| 2760 | Invalid lock strength provided to FireWeapon(). (%i) |
| 2761 | Must be called on a player controlled vehicle |
| 2762 | Speed can't be negative |
| 2763 | Parameter must be a valid client |
| 2764 | This vehicle is not a physics vehicle. |
| 2765 | Top speed forward must be >= 0 |
| 2766 | Top speed reverse must be >= 0 |
| 2767 | Top rotational speed must be >= 0 |
| 2768 | This vehicle is not a physics vehicle. |
| 2769 | This vehicle is not a physics vehicle. |
| 2770 | This vehicle at is already wrecked. |
| 2771 | This vehicle is not a physics vehicle. |
| 2772 | This vehicle is not a physics vehicle. |
| 2773 | Cannot set negative speed on vehicle |
| 2774 | This vehicle is not a physics vehicle. |
| 2775 | This vehicle is not a physics vehicle. |
| 2776 | This vehicle is not a physics vehicle. |
| 2777 | Vehicle_SetWorldCollisionCapsule must be called on a vehicle entity. |
| 2778 | Usage: Vehicle_SetWorldCollisionCapsule( <radius>, <midz>, <height> ). |
| 2779 | key is not internally a string |
| 2780 | key cannot be used for lookup |
| 2781 | Scriptable property '%s' has no getter function - this should never happen |
| 2782 | Scriptable property '%s' is read-only |
| 2783 | Scriptable does not support directly being set, implementation needed |
| 2785 | Cannot set count on a non-spawner entity |
| 2786 | entity %i is not a script_brushmodel, script_model, script_origin, script_arms, script_weapon, light or script_vehicle |
| 2787 | not an entity |
| 2788 | InitDamageParts: invalid entity! |
| 2789 | InitDamageParts: entity %d is not a sentient. Currently only supporting sentients. |
| 2790 | InitDamageParts: Exceeded max number of damageables with ent %d: max %d. |
| 2791 | AddDamagePart: invalid entity! |
| 2792 | AddDamagePart: entity %d is not a sentient. Currently only supporting sentients. |
| 2793 | AddDamagePart: entity %d needs to have InitDestructionParts called on it first. |
| 2794 | AddDamagePart: entity %d has too many destruction parts |
| 2795 | AddDamagePart: entity %d has too many damage sub parts for part %s |
| 2796 | AddDamagePart: entity %d has too many damage sub parts for part %s |
| 2797 | DamageDamagePart: invalid entity! |
| 2798 | DamageDamagePart: entity %d is not a sentient. Currently only supporting sentients. |
| 2799 | DamageDamagePart: Unable to find damage parts for ent %d. |
| 2800 | DamageDamagePart: invalid part name for damage part for ent %d. Must be non-null. |
| 2801 | DestroyDamagePart: invalid entity! |
| 2802 | DestroyDamagePart: entity %d is not a sentient. Currently only supporting sentients. |
| 2803 | DestroyDamagePart: invalid part name for damage part for ent %d. Must be non-null. |
| 2804 | DestroyDamagePart: Unable to find damage parts for ent %d. |
| 2805 | DestroyAllDamageParts: invalid entity! |
| 2806 | DestroyAllDamageParts: entity %d is not a sentient. Currently only supporting sentients. |
| 2807 | DestroyDamagePart: Unable to find damage parts for ent %d. |
| 2808 | GetDamagePartHealth: invalid entity! |
| 2809 | GetDamagePartHealth: entity %d is not a sentient. Currently only supporting sentients. |
| 2810 | GetDamagePartHealth: Unable to find damage parts for ent %d. |
| 2811 | GetDamagePartHealth: invalid part name specified. must be non null. |
| 2812 | SetDamagePartHealth: invalid entity! |
| 2813 | SetDamagePartHealth: entity %d is not a sentient. Currently only supporting sentients. |
| 2814 | SetDamagePartHealth: Unable to find damage parts for ent %d. |
| 2815 | SetDamagePartHealth: invalid part name specified. must be non null. |
| 2816 | SetDamagePartHealth: cannot set health to <= 0 with this function. Use DamageDamagePart instead. |
| 2817 | G_Spawn: no free entities, probable cause: %s |
| 2856 | index %i is an illegal objective index. Valid indexes are 0 to %i |
| 2857 | Illegal objective state "%s". Valid states are "empty", "active", "invisible", "done", "current", "failed" |
| 2860 | index %i is an illegal objective index. Valid indexes are 0 to %i |
| 2861 | Invalid objective location index: %d |
| 2862 | This game mode does not support more than one objective location. Invalid objective location index: %d |
| 2863 | Invalid objective location index: %d |
| 2864 | Entity must be a player or a bot. |
| 2865 | unknown team '%s' |
| 2866 | Entity must be a player or a bot. |
| 2867 | unknown team '%s' |
| 2868 | Illegal objective icon size "%s" |
| 2869 | Entity must be a player or a bot. |
| 2870 | Entity must be a player or a bot. |
| 2871 | unknown team '%s' |
| 2872 | unknown team '%s' |
| 2873 | Objective_SetProgress must be called with a progress value within [0.f, 1.f] |
| 2874 | Entity must be a player or a bot. |
| 2875 | unknown team '%s' |
| 2876 | Entity must be a player or a bot. |
| 2877 | unknown team '%s' |
| 2878 | Entity must be a player or a bot. |
| 2879 | unknown team '%s' |
| 2880 | Entity must be a player or a bot. |
| 2881 | unknown team '%s' |
| 2882 | unknown team '%s' |
| 2883 | Unable to find ASM asset %s. |
| 2884 | Unsupported function param type (%d). |
| 2885 | Ent %d is not running an ASM! |
| 2886 | Ent %d is not currently running ASM %s |
| 2887 | Unable to find state %s in ent %d ASM %s |
| 2888 | ent %d is not running an ASM! |
| 2889 | ent %d is not currently running asm %s! |
| 2890 | ent %d is not running an asm! |
| 2891 | ent %d is not currently running asm %s! |
| 2892 | ent %d is not running an asm! |
| 2893 | ent %d is not currently running asm %s! |
| 2894 | ent %d is not running an asm! |
| 2895 | ent %d is not currently running asm %s! |
| 2896 | ent %d is not running an asm! |
| 2897 | ent %d is not currently running asm %s! |
| 2898 | Unable to find pain state listed in ASM %s, state %s |
| 2899 | Unable to set pain state %s. No valid passthrough transitions evaluated. |
| 2900 | ent %d is not running an asm! |
| 2901 | ent %d is not currently running asm %s! |
| 2902 | ent %d is not running an asm! |
| 2903 | ent %d is not currently running asm %s! |
| 2904 | ent %d is not running an asm! |
| 2905 | ent %d is not currently running asm %s! |
| 2906 | ent %d is not running an asm! |
| 2907 | ent %d is not currently running asm %s! |
| 2908 | Function passed more than the max allowed param overrides (%d) |
| 2909 | Function passed unsupported param override type (%d) |
| 2910 | unable to find state %s in asm %s. |
| 2911 | unable to successfully transition to state %s. no valid passthrough transitions found. |
| 2912 | ent %d is not running an asm! |
| 2913 | ent %d is not currently running asm %s! |
| 2914 | ent %d is not running an asm! |
| 2915 | ent %d is not running an asm! |
| 2916 | ent %d is not running an asm! |
| 2917 | ent %d is not currently running asm %s! |
| 2918 | ent %d is not running an asm! |
| 2919 | ent %d is not running an asm! |
| 2920 | ent %d is not currently running asm %s! |
| 2921 | ent %d is not running an asm! |
| 2922 | ent %d is not currently running asm %s! |
| 2923 | ent %d is not running an asm! |
| 2924 | ent %d is not running an asm! |
| 2925 | ASMGetEventID: invalid event name. |
| 2926 | ent %d is not currently running asm %s! |
| 2927 | ent %d is not running an asm! |
| 2928 | ASMGetEventID: invalid event name. |
| 2929 | ent %d is not currently running asm %s! |
| 2930 | ent %d is not running an asm! |
| 2931 | ASMGetEvent: invalid event name. |
| 2932 | ASM %s not loaded! |
| 2933 | ASM %s not loaded! |
| 2934 | ai_scripted field %s is read-only |
| 2935 | AI field %s clamped from %g to 1 |
| 2936 | AI field %s clamped from %g to 0 |
| 2937 | AI field %s clamped from %g to 0 |
| 2938 | AI field %s clamped from %i to 0 |
| 2939 | AI field %s clamped from %i to 255 |
| 2940 | AI field %s clamped from %i to 0 |
| 2941 | AI field %s clamped from %i to 65535 |
| 2942 | unknown combat mode '%s', should be cover, cover3d, no_cover, ambush_nodes_only, or ambush |
| 2943 | radius must be >= 0 |
| 2944 | unknown alert level '%s' |
| 2945 | unknown type '%s', should be soldier, civilian, dog, alien, zombie, juggernaut, suicidebomber |
| 2946 | Invalid stance %s. |
| 2947 | Invalid gun pose override %s |
| 2948 | G_VehiclePath_SetScriptVariable: unsupported type %s |
| 2949 | Not a vehicle node |
| 2950 | key is not internally a string |
| 2951 | GetVehicleNode used with more than one node |
| 2952 | key is not internally a string |
| 2953 | 1 >= pip fov <= 179 |
| 2960 | PIP look direction being set to NANs. |
| 2961 | PIP up direction being set to NANs. |
| 2962 | PIP right direction being set to NANs. |
| 2963 | pip near z must be >= 0 |
| 2964 | pip blur radius must be >= 0 |
| 2965 | LOD over-ride must be >= %f |
| 2966 | LOD over-ride must be <= %f |
| 2967 | PIP aspect ratio must be >= %f |
| 2968 | PIP aspect ratio must be <= %f |
| 2969 | pip cull distance must be >= 0 |
| 2970 | Pip already allocated for this client |
| 2971 | USAGE: spawn( "script_character", <origin>, <spawnflags>, <forceNoCollision>, <characterId>, <assetname> ) |
| 2972 | %s is too long. Max length is %i |
| 2973 | Entity is not a player |
| 2974 | %s is too long. Max length is %i |
| 2975 | bad escape character (%i) present in string |
| 2976 | %s is too long. Max length is %i |
| 2977 | Knockback(): Only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 2978 | Knockback() USAGE: <player> Knockback(<velocity dir>,<velocity magnitud>) |
| 2979 | SetOnWallAnimConditional(): Only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 2980 | SetOnWallAnimConditional() USAGE: <player> SetOnWallAnimConditional(<enable>) |
| 2981 | SetSeatedAnimConditional(): Only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 2982 | SetSeatedAnimConditional() USAGE: <player> SetSeatedAnimConditional(<type>,<enable>) |
| 2983 | SetSeatedAnimConditional() USAGE: <player> SetSeatedAnimConditional(<type>,<enable>). Valid types are "seat" and "zipline". |
| 2984 | SetSeatedAnimConditional() USAGE: <player> SetSeatedAnimConditional(<type>,<enable>). Valid types are "seat" and "zipline". |
| 2985 | Owner entity is not a player |
| 2986 | Couldn't find icon index for friendly icon '%s' |
| 2987 | Couldn't find icon index for enemy icon '%s' |
| 2988 | unable to spawn "%s" entity |
| 2989 | Owner entity is not a player |
| 2990 | Can't spawn extra helicopter. In ship this will return 'undefined' instead of a script error. |
| 2991 | You cannot create more than %d impulse fields. |
| 2992 | Incorrect number of parameters. Correct usage: SpawnImpulseField( <owner>, <weapon>, <origin> ) |
| 2993 | First argument must be either the owner entity or undefined. |
| 2994 | Invalid weapon. |
| 2995 | SetOmnvarForAllClients - '%s' not found |
| 2996 | SetOmnvarForAllClients '%s' must be done on a client-scope Omnvar |
| 2997 | unknown sound alias '%s' |
| 2998 | '%s' is a looping alias, use 'playloopsound' instead |
| 2999 | '%s' does has not have a valid index (%i) |
| 3000 | playsurfacesound has %d parameters. There should be exactly two. |
| 3001 | unknown surface sound alias '%s' |
| 3002 | '%s' is a looping alias, cannot play a looping surface sound |
| 3003 | '%s' does has not have a valid index (%i) |
| 3004 | Surface type '%s' does not exist (%s) |
| 3005 | playcontextsound has %d parameters. There should be exactly three or five. |
| 3006 | unknown context sound alias '%s' |
| 3007 | '%s' is a looping alias, cannot play a looping context sound |
| 3008 | '%s' does has not have a valid index (%i) |
| 3009 | Sound Context '%s' - '%s' does not exist (%s) |
| 3010 | Sound Context '%s' - '%s' does not exist (%s) |
| 3011 | Entity is not a sound_blend |
| 3012 | Lerp must be between 0.0f and 1.0f |
| 3014 | Usage: PlaySoundOnMovingEnt( <aliasname> ); |
| 3015 | PlaySoundOnMovingEnt called on a server only entity of type %s. |
| 3016 | unknown sound alias '%s' |
| 3017 | '%s' is a looping alias, use 'playloopsound' instead |
| 3018 | '%s' does has not have a valid index (%i) |
| 3019 | playsoundatpos needs 2 parameters - you had %i parameters |
| 3020 | EnableAudioPortal: Incorrect number of parameters |
| 3021 | This function only supports trigger brush entity types. |
| 3022 | Invalid trigger model index |
| 3023 | SetThirdPersonCamVehicle needs 1 parameter - you had %i parameters |
| 3024 | unknown team '%s' |
| 3025 | Illegal team string '%s'. Must be allies, axis, team_three, team_four, team_five, or team_six. |
| 3026 | entity %i is not a player |
| 3027 | entity %i is not a player |
| 3028 | Invalid sound name specified |
| 3029 | Function not supported for front-end server |
| 3030 | Invalid sound allies sound name specified |
| 3031 | Invalid sound axis sound name specified |
| 3032 | unknown team '%s' |
| 3033 | array is too large (%d > %d) |
| 3034 | element %i of array: type %s is not an entity |
| 3035 | SetFXKillDefOnDelete should only be operated on an entity that is an FX type |
| 3037 | Cannot change a 'willNeverChange' entity |
| 3038 | Dont call Hide() on players, use PlayerHide() for that. |
| 3039 | Cannot change a 'willNeverChange' entity |
| 3040 | Must be called on a player. |
| 3041 | Must be called on a player. |
| 3042 | Cannot change a 'willNeverChange' entity |
| 3043 | showToClient error: param must be a client entity |
| 3044 | Cannot change a 'willNeverChange' entity |
| 3045 | showToClient error: param must be a client entity |
| 3046 | SetClientOwner() cannot be called on a client. |
| 3047 | SetClientOwner() needs to have a client passed in. |
| 3048 | Cannot change a 'willNeverChange' entity |
| 3049 | entity is not a client |
| 3050 | MissileDontTrackKillCam must be called on a missile. |
| 3051 | First argument missing in SetScriptMoverKillCam() invocation. |
| 3052 | SetScriptMoverKillCam needs to have a correct killcam type passed in. This can be 'helicopter', 'airstrike', 'explosive', 'fast explosive', 'rocket', 'turret', 'javelin', 'missile'. We had %s. |
| 3053 | SetScriptMoverKillCam() can only be invoked on a script mover. |
| 3054 | SetScriptMoverKillCam() was invoked on a 'willNeverChange' entity |
| 3055 | SetJammerFlag: Invalid parameter. Must be one of: (little, medium, large) |
| 3056 | TriggerOneOffRadarSweep(): Invalid parameters. Check documentation. |
| 3057 | TriggerOneOffRadarSweep(): Owner must be a player. |
| 3058 | PingLocationEnemyTeams(): Invalid parameters. Check documentation. |
| 3059 | PingLocationEnemyTeams(): Invalid number of decoys. |
| 3060 | PingLocationEnemyTeams(): Invalid magnitude. |
| 3061 | unknown team '%s' |
| 3062 | TriggerPortableRadarPing(): Invalid parameters. Check documentation. |
| 3063 | TriggerPortableRadarPing(): Invalid radius. |
| 3064 | TriggerPortableRadarPing(): Invalid sweep time. |
| 3065 | TriggerPortableRadarPing(): Owner must be a player or agent player. |
| 3066 | TriggerPortableRadarPingTeam(): Invalid parameters. Check documentation. |
| 3067 | unknown team '%s' |
| 3068 | TriggerPortableRadarPingTeam(): Invalid radius. |
| 3069 | TriggerPortableRadarPingTeam(): Invalid sweep time. |
| 3070 | Not supported for front-end server |
| 3071 | Agent entity %i could not enable entity %i for use, probably exceeded max enabled ents (%i, %i) |
| 3072 | entity is not a player or agent |
| 3073 | entity is not a player or agent |
| 3074 | entity type '%s' is not a turret |
| 3075 | 'manual_ai' is not support in MP |
| 3076 | 'auto_ai' is not support in MP |
| 3077 | Error setting the mode of a turret. |
| 3078 | entity type '%s' is not a turret |
| 3079 | Entity is not a client or agent. |
| 3080 | Entity is not a client or agent. |
| 3081 | CapsuleTrace capsule radius too small! |
| 3082 | CapsuleTrace capsule half height must be greater than the radius! |
| 3083 | map_restart already called |
| 3084 | map_restart already called |
| 3085 | AddBot: Invalid number of args |
| 3086 | This function cannot be used by the front-end server |
| 3087 | AddBot: Invalid number of args |
| 3088 | unknown team '%s' |
| 3089 | SpawnBotOrTestClient() must be called on a valid bot or test client. See console log for details |
| 3090 | Localized string should start with %s |
| 3091 | Not enough parameters for Kick() |
| 3092 | Invalid client specified (%i) out of %i clients |
| 3093 | USAGE: <player> VisionSetAlternateForPlayer( <stage>, <duration> ) |
| 3094 | entity %i is not a player or player-controlled agent |
| 3095 | entity %i is not a player or player-controlled agent |
| 3096 | USAGE: <player> VisionSetForPlayer( <visionset name>, <transition time> ) |
| 3097 | visionset %s has not been precached, include with precache_vision |
| 3098 | USAGE: <player> VisionSetForPlayer( <visionset name>, <transition time> ) |
| 3099 | visionset %s has not been precached, include with precache_vision |
| 3100 | visionset %s has not been precached, include with precache_vision |
| 3101 | visionset %s has not been precached, include with precache_vision |
| 3102 | visionset %s has not been precached, include with precache_vision |
| 3103 | visionset %s has not been precached, include with precache_vision |
| 3104 | EnableWorldUp only works on players. |
| 3105 | SetEyesOnUplinkEnabled requires 1 argument |
| 3106 | entity type '%s' is not a turret |
| 3107 | entity type '%s' is not a turret |
| 3108 | Specified entity is not a helicopter. Currently only helicopters are supported for this feature. |
| 3109 | Specified helicopter is not controlled by a player |
| 3110 | Specified helicopter is not controlled by a player |
| 3111 | Specified entity is not a helicopter. Currently only helicopters are supported for this feature. |
| 3112 | Specified helicopter is not controlled by a player |
| 3113 | Specified helicopter is not controlled by a player |
| 3114 | Currently only supported on script movers, vehicle/helicopters entities, entities with brush models |
| 3115 | Currently only supported on vehicle/helicopters entities or entities with brush models |
| 3116 | cannot read persistent data: player has not finished connecting, or ExitLevel has been called. (%s, %d) |
| 3120 | GetDDL: Can't use this function on the front end server |
| 3121 | SetDDL: Can't use this function on the front end server |
| 3122 | GetPlayerData not allowed on front end server |
| 3123 | GetPlayerData: entity must be a player entity |
| 3124 | GetPlayerData: we don't have that player's stats yet |
| 3125 | Cannot use codcaster client match data for front-end server |
| 3126 | Already set codcaster client match data def |
| 3128 | SetPlayerData not allowed on front end server |
| 3129 | SetPlayerData: entity must be a player entity |
| 3130 | GetPlayerData: we don't have that player's stats yet |
| 3131 | GetPlayerData: entity must be a player entity |
| 3132 | LogStatMatchGUID not allowed on front end server |
| 3133 | LogStatMatchGUID: entity must be a player entity |
| 3134 | LogStatMatchGUID: we don't have that player's stats yet |
| 3135 | Incorrect number of parameters |
| 3136 | unknown team '%s' |
| 3137 | Illegal teammode string '%s'. Must be ffa, or axis_allies. |
| 3138 | unknown team '%s' |
| 3139 | unknown team '%s' |
| 3140 | Unknown mode |
| 3141 | Only works in [manual_change] mode |
| 3142 | unknown team '%s' |
| 3143 | setPlayerTeamRank [entity] [team] [rank] |
| 3144 | setPlayerTeamRank Error: param 1 is not an entity |
| 3145 | setPlayerTeamRank Error: param 2 must be >= 0 |
| 3146 | getAssignedTeam [player] |
| 3147 | getAssignedTeam Error: param 1 is not an entity. |
| 3148 | sendleaderboards: entity must be a player entity |
| 3149 | No match data def defined |
| 3150 | No match data def defined |
| 3151 | No match data def defined |
| 3153 | No match data def defined |
| 3154 | No match data def defined |
| 3155 | Incorrect number of parameters. |
| 3156 | SetMatchClientIP cannot be called for front-end server |
| 3157 | matchdata set ip Error: param 1 is not an entity |
| 3158 | RecordBreadcrumbDataForPlayer must be called on a player |
| 3159 | RecordBreadcrumbDataForPlayer takes 3 params! |
| 3168 | No match data def defined |
| 3169 | Missing information for match data life. |
| 3170 | LogMatchDataLife must be called on a client |
| 3171 | Invalid match data definition specified. Must contain a lifeCount field. |
| 3172 | Invalid match data definition specified. lifeCount must be a short |
| 3173 | Invalid match data definition specified. The lives field must be an indexed array of life structures |
| 3174 | Invalid match data definition specified. The lives field must be an indexed array of life structures |
| 3175 | Invalid match data definition specified. The attachments field must be an indexed array of enumerations |
| 3176 | All elements need to be strings. |
| 3177 | Invalid match data definition specified. The attachments field must be an indexed array of enumerations |
| 3178 | No match data def defined |
| 3179 | LogMatchDataDeath must be called on a client |
| 3180 | GScr_LogMatchDataDeath requires 17 params |
| 3181 | array is too large (%d > %d), need to increase sortable array size |
| 3182 | array is too large (%d > %d), need to increase sortable array size |
| 3183 | Invalid match data definition specified. The lives field must be an indexed array of life structures |
| 3184 | Invalid match data definition specified. The lives field must be an indexed array of life structures |
| 3185 | No matchdata def defined |
| 3189 | No match data def defined |
| 3190 | SetMatchDataDef takes exactly one argument: <def asset name> |
| 3191 | Already set match data def |
| 3192 | def name too long. |
| 3193 | SetClientMatchDataDef takes exactly one argument: <def asset name> |
| 3194 | Cannot use client match data for front-end server |
| 3195 | Already set client match data def |
| 3196 | def name too long. |
| 3197 | level compassMapWorldSize is not set or cannot be 0 |
| 3198 | No client match data def defined |
| 3199 | LogClientMatchDataDeath must be called on a client |
| 3200 | Invalid client match data definition specified. Must contain a deathCount field. |
| 3201 | Invalid client match data definition specified. deathCount must be a short |
| 3202 | Invalid client match data definition specified. The deaths field must be an indexed array of life structures |
| 3203 | Invalid client match data definition specified. The deaths field must be an indexed array of life structures |
| 3204 | GetMatchRulesData called when not using recipe! |
| 3205 | setstat: entity must be a player entity |
| 3206 | GetRestedTime not allowed on front end server |
| 3207 | setstat: entity must be a player entity |
| 3208 | This function is not available for the front end scene |
| 3209 | StartPodium(): invalid number of arguments provided. StartPodium( <client>, <mvp array> ); |
| 3210 | StartPodium(): client index should be an integer between -1 and %i |
| 3211 | StartPodium(): expected an array. |
| 3212 | StartPodium(): too many array entries were provided. %i provided, %i supported. |
| 3213 | StartPodium(): element %i of provided array -- type %s is not an object |
| 3214 | StartPodium(): element %zu of provided array -- field 'rigIndex' is missing or is not an integer. |
| 3215 | StartPodium(): element %zu of provided array -- field 'headindex' is missing or is not an integer. |
| 3216 | StartPodium(): element %zu of provided array -- field 'bodyIndex' is missing or is not an integer. |
| 3217 | StartPodium(): element %zu of provided array -- field 'weaponName' is not a valid weapon entity. |
| 3218 | StartPodium(): element %zu of provided array -- field 'weaponName' is missing or is not a weapon type. |
| 3219 | StartPodium(): element %zu of provided array -- field 'clanTag' is missing or is not a string. |
| 3220 | StartPodium(): element %zu of provided array -- field 'name' is missing or is not a string. |
| 3221 | StartPodium(): element %zu of provided array -- field 'xuid' is missing or is not a string. |
| 3222 | setteamfortrigger: trigger entity must be of type %s or %s |
| 3223 | unknown team '%s' |
| 3224 | clientclaimtrigger: claimer must be a client or an agent. |
| 3225 | clientclaimtrigger: trigger entity must be of type %s or %s |
| 3226 | clientreleasetrigger: releaser must be a client or an agent. |
| 3227 | clientreleasetrigger: trigger entity must be of type %s or %s |
| 3228 | releaseclaimedtrigger: trigger entity must be of type %s or %s |
| 3229 | Expected 1 argument to setMapCenter() |
| 3230 | Expected 1 argument to setGameEndTime() |
| 3231 | unknown team '%s' |
| 3232 | Illegal team string '%s'. Must be allies, axis, team_three, team_four, team_five, team_six or none. |
| 3233 | unknown team '%s' |
| 3234 | Illegal team string '%s'. Must be allies, axis, team_three, team_four, team_five, team_six or none. |
| 3235 | unknown team '%s' |
| 3236 | Illegal team string '%s'. Must be a valid team. |
| 3237 | Illegal UAV radar strength value'%d'. Must be between %d and %d. |
| 3238 | unknown team '%s' |
| 3239 | Illegal team string '%s'. Must be axis, allies, team_three, team_four, team_five, team_six or none. |
| 3240 | unknown team '%s' |
| 3241 | Illegal team string '%s'. Must be axis, allies, team_three, team_four, team_five, team_six or none. |
| 3242 | unknown team '%s' |
| 3243 | Illegal team string '%s'. Must be axis, allies, team_three, team_four, team_five, team_six or none. |
| 3244 | unknown team '%s' |
| 3245 | Illegal team string '%s'. Must be axis, allies, team_three, team_four, team_five, team_six or none. |
| 3246 | GScr_MainMP_StartRagdoll - first param must be a boolean - it should be true if the ragdoll is being played instantly on a player |
| 3247 | GScr_MainMP_StartRagdoll - flagged as an instant player ragdoll - but the entity isn't a player corpse |
| 3248 | GScr_MainMP_StartRagdollFromImpact - Must only be called on agent corpse. |
| 3249 | HitLoc parameter must be a valid hit location string e.g. 'torso_upper' |
| 3250 | Only valid on player corpses |
| 3251 | SetCorpseFalling called on a non-corpse entity |
| 3252 | Must be called on a player. |
| 3253 | not a player entity |
| 3254 | SetBallPassAllowed() must be called on a player |
| 3255 | not a player entity |
| 3256 | not a player entity |
| 3257 | This functionality has been disabled, code needs to re-enable special ambient support |
| 3258 | not a player entity |
| 3259 | This functionality has been disabled, code needs to re-enable special ambient support |
| 3260 | USAGE: DevSetMinimapDvarSettings(<znear>, <angle>); |
| 3261 | PlayerSetGroundReferenceEnt is not supported in this game mode |
| 3262 | not a player entity |
| 3263 | not an entity |
| 3264 | duration must be positive. |
| 3265 | duration must be less than 65.535s any longer will require more bandwidth. |
| 3266 | Intensity must be between 0 and 1 |
| 3267 | Currently only supported on script model entities |
| 3268 | Currently only supported on script model entities |
| 3269 | not a player entity |
| 3270 | not a vehicle entity |
| 3271 | failed to link player entity %i to vehicle entity %i: %s |
| 3272 | not a player entity |
| 3273 | player not riding a vehicle |
| 3274 | player not riding a vehicle |
| 3275 | Cannot get moving platform data from player entity. |
| 3276 | This should only be used with missiles |
| 3277 | This should only be used with missiles |
| 3278 | LaunchGrenade <grenade type> <origin> <velocity> [time to blow (seconds)] [rethrow grenade entity]. |
| 3279 | Specified grenade weapon is not valid |
| 3280 | LaunchGrenade(): provided rethrow entity is not an entity. |
| 3281 | LaunchGrenade(): provided rethrow entity type is not ET_MISSILE. |
| 3282 | LaunchGrenade(): provided rethrow entity classname is not 'grenade'. |
| 3283 | LaunchGrenade(): provided rethrow entity script_classname is not 'grenade'. |
| 3284 | Incorrect number of parameters |
| 3285 | Incorrect number of parameters |
| 3286 | Incorrect number of parameters |
| 3287 | effect team %s is invalid |
| 3288 | effect id %i is invalid |
| 3289 | cannot use " characters in tag names |
| 3290 | tag '%s' does not exist on entity with model '%s' |
| 3291 | Incorrect number of parameters |
| 3292 | entity wasn't created with 'newFx' |
| 3293 | Incorrect number of parameters |
| 3294 | USAGE: SetNoJIPScore( no_jip ) |
| 3295 | USAGE: SetNoJIPTime( no_jip ) |
| 3296 | LoadWeaponsForPlayer requires an array of weapon names. |
| 3297 | LoadWorldWeapons requires an array of weapon names. |
| 3298 | ClearWorldWeapons does not accept arguments. |
| 3299 | SetUAVJammed: Entity must be a player entity |
| 3300 | SetUAVJammed: Feature not supported by the current game mode |
| 3301 | IsSpectatingPlayer must be called on a player. |
| 3302 | unknown species '%s' (should be human, dog, alien or all) |
| 3303 | unknown unit type %s. (should be soldier, civilian, C6, C8, C12, alien, juggernaut, suicidebomber, or all.) |
| 3304 | LoadInfilTransient has %d parameters. There should be exactly one. |
| 3305 | LoadInfilTransient takes a string parameter |
| 3306 | UnloadInfilTransient has %d parameters. There should be exactly one. |
| 3307 | UnloadInfilTransient takes a string parameter |
| 3308 | Trying to kill AI %d with damageShield (magic_bullet_shield) |
| 3309 | Source Damage vector is invalid : %f %f %f |
| 3310 | Usage: kill( <source position>, <attacker>, <inflictor> ) |
| 3311 | Agent(%d) is already dead when suicide command called! |
| 3313 | ReportChallengeUserEvent: entity must be a player entity |
| 3314 | ReportChallengeUserEvent: must supply at least an event name |
| 3318 | assignment of client.health: health (%i) must be less than maxHealth (%i) |
| 3319 | self.health must be greater than 0 (tried to set %i on ent %i, name %s) |
| 3320 | checkGrenadeThrow: method must be 'min energy', 'min time', or 'max time' - value passed in was %s |
| 3321 | Missing tag [%s] on entity [%d] (%s) |
| 3322 | Grenade_CheckLaunch: invalid weapon for entity %d |
| 3323 | Grenade_CheckLaunch: grenade launcher speed must be > 0 |
| 3324 | Too many grenade throw methods |
| 3326 | SpawnLoopingSound is not supported in this game mode |
| 3327 | SpawnLoopingSound called with too many sounds in level |
| 3328 | SpawnLoopingSound requires 2, 3, or 4 parameters |
| 3329 | SpawnLoopingSound() must be called during level initialization. |
| 3330 | unknown sound alias '%s' |
| 3331 | '%s' is not a looping alias, do not use createLoopSound |
| 3332 | turret manual target not defined |
| 3333 | turretData cannot point at target |
| 3334 | 'tag_aim' bone not found on turret |
| 3335 | useTurretViewmodelAnims is not supported with remote turrets. |
| 3336 | G_Turret_Spawn: Bad weapon specified for turret. It likely needs to be precached in script. |
| 3337 | G_Turret_Spawn: weapon '%s' isn't a turret. This usually indicates that the weapon failed to load. |
| 3338 | G_Turret_Spawn: '%s' not precached |
| 3348 | %s is not an entity |
| 3349 | %s is not an entity |
| 3403 | type %s is not a float |
| 3423 | Invalid match data definition specified. Must contain a lifeCount field. |
| 3424 | Invalid match data definition specified. lifeCount must be a short |
| 3425 | Invalid match data definition specified. |
| 3426 | Invalid match data definition specified. Must be a int, short or byte type |
| 3427 | Invalid match data definition specified. The lives field must be an indexed array of life structures |
| 3428 | Invalid match data definition specified. The players field must be an indexed array of player structures |
| 3429 | FinishPlayerDamage(): invalid partName provided. Impaling weapons require valid partName and hitLoc upon kill. |
| 3430 | unknown team '%s' |
| 3431 | USAGE: spawn( "trigger_radius", <origin>, <spawnflags>, <radius>, <height> ) |
| 3432 | USAGE: spawn( "trigger_rotatable_radius", <origin>, <spawnflags>, <radius>, <height> ) |
| 3433 | Failed to spawn trigger_use; useCommand value is invalid (%s). |
| 3434 | unknown type '%s', should be human, dog, alien |
| 3435 | actor field %s is read-only |
| 3436 | All elements need to be ints or floats or strings, for now... |
| 3437 | Invalid blend curve name '%s' |
| 3438 | AnimScripted entities are not supported in this game mode |
| 3439 | No model exists. |
| 3440 | Illegal mode %s for animScripted. Valid modes are normal and deathplant |
| 3441 | tried to play a scripted animation on a dead AI; entity %i team %i origin %s targetname %s classname %s |
| 3442 | AnimScripted entities are not supported in this game mode |
| 3443 | too many parameters |
| 3444 | too few parameters |
| 3445 | AnimScripted entities are not supported in this game mode |
| 3446 | AnimScripted entities are not supported in this game mode |
| 3447 | AnimScripted entities are not supported in this game mode |
| 3448 | No model exists. |
| 3449 | Illegal mode %s for animScripted. Valid modes are normal and deathplant |
| 3450 | tried to play a scripted animation on a dead AI; entity %i team %i origin %.2f %.2f %.2f targetname %s classname %s |
| 3451 | AnimScripted entities are not supported in this game mode |
| 3452 | too many parameters |
| 3453 | Incorrect number of parameters for ScrCmd_animrelative command |
| 3456 | AnimScripted entities are not supported in this game mode |
| 3457 | too many parameters |
| 3458 | must set nonnegative goal time |
| 3459 | must set nonnegative weight |
| 3460 | No model exists. |
| 3461 | AnimScripted entities are not supported in this game mode |
| 3462 | incorrect number of parameters |
| 3463 | must set nonnegative goal time |
| 3464 | must set nonnegative weight |
| 3465 | root anim is not an ancestor of the anim |
| 3466 | No model exists. |
| 3467 | AnimScripted entities are not supported in this game mode |
| 3468 | blended nonsynchronized animation has no concept of time |
| 3469 | AnimScripted entities are not supported in this game mode |
| 3470 | AnimScripted entities are not supported in this game mode |
| 3471 | GetAnimIKWeights: No model exists. |
| 3472 | AnimScripted entities are not supported in this game mode |
| 3473 | blended nonsynchronized animation has no concept of rate |
| 3474 | AnimScripted entities are not supported in this game mode |
| 3475 | AnimScripted entities are not supported in this game mode |
| 3476 | too many parameters |
| 3477 | must set nonnegative rate for flagged anims |
| 3478 | must set nonnegative goal time |
| 3479 | must set positive weight |
| 3480 | blended nonsynchronized animation has no concept of time |
| 3481 | No model exists. |
| 3482 | AnimScripted entities are not supported in this game mode |
| 3483 | Illegal call to SetFlaggedAnimKnobAll() / Illegal call to SetFlaggedAnimKnobAllRestart() |
| 3484 | must set nonnegative rate for flagged anims |
| 3485 | must set nonnegative goal time |
| 3486 | must set positive weight |
| 3487 | blended nonsynchronized animation has no concept of time |
| 3488 | root anim is not an ancestor of the anim |
| 3489 | No model exists. |
| 3490 | AnimScripted entities are not supported int this game mode |
| 3491 | incorrect number of parameters |
| 3492 | must set nonnegative rate for flagged anims |
| 3493 | must set nonnegative goal time |
| 3494 | must set positive weight |
| 3495 | blended nonsynchronized animation has no concept of time |
| 3496 | No model exists. |
| 3497 | AnimScripted entities are not supported in this game mode |
| 3498 | cannot change the animtree of classname '%s' |
| 3499 | AnimScripted entities are not supported in this game mode |
| 3500 | cannot change the animtree of classname '%s' |
| 3501 | AnimScripted entities are not supported in this game mode |
| 3502 | too many parameters |
| 3503 | must be > 0 |
| 3504 | must be < 1 |
| 3505 | not a leaf animation |
| 3506 | cannot set time 1 on looping animation |
| 3507 | AnimScripted entities are not supported in this game mode |
| 3508 | incorrect number of parameters |
| 3509 | AnimScripted entities are not supported in this game mode |
| 3510 | incorrect number of parameters |
| 3511 | SetAnimBlendCurve: could not find curve '%s' |
| 3512 | AnimScripted entities are not supported in this game mode |
| 3513 | Invalid number of parameters. |
| 3514 | Invalid parameter name. |
| 3515 | No model exists. |
| 3516 | Invalid value. |
| 3517 | AnimScripted entities are not supported in this game mode |
| 3518 | too many parameters |
| 3519 | must set nonnegative goal time |
| 3520 | must set nonnegative weight |
| 3521 | No model exists. |
| 3522 | Cannot set the animTree for a vehicle using the an always loaded .atr |
| 3523 | root anim is not an ancestor of the anim |
| 3524 | cannot flag anim since it has 0 effective goal weight |
| 3525 | entity %i is not a player or agent |
| 3526 | entity %i references an animset that has not been loaded |
| 3527 | animation state %s does not exist in the animset for %s |
| 3528 | animation entry %d is not a part of anim state %s in animset %s |
| 3529 | animation entry %s is not a part of anim state %s in animset %s |
| 3530 | entry must be either int, string, or undefined (to select at random) |
| 3534 | agent is not initialized |
| 3535 | agent is not initialized |
| 3536 | agent field %s is read only. |
| 3537 | unknown team '%s' |
| 3538 | agent is not pointing to the level.agents array |
| 3539 | agent is not initialized |
| 3540 | agent is not pointing to the level.agents array |
| 3541 | agent is not initialized |
| 3542 | agentname must be a localized string or undefined |
| 3543 | agent is not pointing to the level.agents array |
| 3544 | agent is not initialized |
| 3545 | agent is not initialized |
| 3546 | out of hudelems |
| 3547 | '%s' is not a valid value for HUD element field '%s'; expected one of the listed values |
| 3548 | cannot read label |
| 3549 | font scale was %g; should be > 0 |
| 3550 | Font scale is not within the appropriate range |
| 3551 | not a client |
| 3552 | not a hud element |
| 3553 | This must be called on a DEV string hud elem |
| 3554 | Hud elem doesn't reference any text. Make sure to call setDevText before using clearAllTextAfterHudElem. |
| 3555 | USAGE: <hudelem> setShader("materialname"[, optional_width, optional_height]); |
| 3556 | Trying to set the material to an empty string |
| 3557 | width %i < 0 |
| 3558 | height %i < 0 |
| 3559 | LinkWaypointToTargetWithOffset requires 2 parameters. |
| 3560 | LinkWaypointToTargetWithOffset can only be used on hud elements of type WAYPOINT. |
| 3561 | USAGE: <hudelem> %s(time_in_seconds); |
| 3562 | time %g should be > 0 |
| 3563 | USAGE: <hudelem> %s(time_in_seconds, total_clock_time_in_seconds, shadername[, width, height]); |
| 3564 | time %g should be > 0 |
| 3565 | duration %g should be > 0 |
| 3566 | width %i < 0 |
| 3567 | height %i < 0 |
| 3568 | SetWaypointBackground: Hudelem needs to be of 'waypoint' type. |
| 3569 | Hudelem needs to be of 'waypoint' type. |
| 3570 | Hudelem needs to be of 'waypoint' type. |
| 3571 | Hudelem needs to be of 'waypoint' type. |
| 3572 | fade time %g <= 0 |
| 3573 | fade time %g > 60 |
| 3574 | scale time %g <= 0 |
| 3575 | scale time %g > 60 |
| 3576 | hudelem scaleOverTime(time_in_seconds, new_width, new_height) |
| 3577 | scale time %g <= 0 |
| 3578 | scale time %g > 60 |
| 3579 | move time %g <= 0 |
| 3580 | move time %g > 60 |
| 3581 | rotate time %g <= 0 |
| 3582 | rotate time %g > 60 |
| 3583 | RotateOverTime() not supported in this game mode |
| 3585 | Time (%i) must be greater than zero. |
| 3586 | USAGE: <hudelem> SetPulseFX( <speed>, <decayStart>, <decayDuration> ); |
| 3587 | unknown team '%s' |
| 3588 | getent key is not internally a string |
| 3589 | %s used with more than one entity ("%s" = "%s") |
| 3590 | key is not internally a string |
| 3591 | SetGenericField is attempting to truncate an invalid number into an F_SHORT |
| 3592 | SetGenericField is attempting to truncate an invalid number into an F_USHORT (which is unsigned) |
| 3593 | SetGenericField is attempting to truncate an invalid number into an F_BYTE (which is unsigned) |
| 3594 | Not a weapon object |
| 3595 | Scr_SetGenericFieldByValue is attempting to truncate an invalid number into an F_SHORT |
| 3596 | Scr_SetGenericFieldByValue is attempting to truncate an invalid number into an F_USHORT (which is unsigned) |
| 3597 | Scr_SetGenericFieldByValue is attempting to truncate an invalid number into an F_BYTE (which is unsigned) |
| 3598 | Scr_SetGenericFieldByValue is attempting to set an entity by value which is unsupported. |
| 3599 | Scr_SetGenericFieldByValue is attempting to set an ent handle by value which is unsupported. |
| 3600 | Scr_SetGenericFieldByValue is attempting to set a sentient by value which is unsupported. |
| 3601 | Scr_SetGenericFieldByValue is attempting to set a sentient handle by value which is unsupported. |
| 3602 | Scr_SetGenericFieldByValue is attempting to set a pathnode by value which is unsupported. |
| 3603 | Scr_SetGenericFieldByValue is attempting to set a weapon by value which is unsupported. |
| 3604 | vehicle node is read-only |
| 3605 | FXEntity node is read-only |
| 3606 | SoundEntity node is read-only |
| 3607 | PIP is not supported in this game mode |
| 3608 | entity does not support directly being set, implementation needed |
| 3609 | hudelem does not support directly being set, implementation needed |
| 3610 | pathnode does not support directly being set, implementation needed |
| 3611 | vehicle node is read-only |
| 3612 | FXEntity node is read-only |
| 3613 | SoundEntity node is read-only |
| 3614 | entity does not support directly being set, implementation needed |
| 3615 | entity does not support directly being set, implementation needed |
| 3616 | PIP is not supported in this game mode |
| 3617 | entity %i is not an agent |
| 3618 | not an entity |
| 3622 | AgentCmd_SpawnAgent called for an agent that already has active character data |
| 3623 | Cannot create capsule with radius larger than half the height |
| 3624 | ERROR: Cannot set owner that is not in use. |
| 3625 | Given stun fraction (%.2f) is outside the allowed range (0,1). |
| 3626 | Trying to do damage to a client that is already dead |
| 3627 | AgentCanSeeSentient: entity is not sentient. |
| 3629 | Legacy agent script commands no longer supported |
| 3631 | Legacy agent script commands no longer supported |
| 3632 | Legacy agent script commands no longer supported |
| 3634 | Legacy agent script commands no longer supported |
| 3635 | Legacy agent script commands no longer supported |
| 3636 | Legacy agent script commands no longer supported |
| 3637 | Legacy agent script commands no longer supported |
| 3638 | Legacy agent script commands no longer supported |
| 3639 | Legacy agent script commands no longer supported |
| 3640 | Legacy agent script commands no longer supported |
| 3643 | Legacy agent script commands no longer supported |
| 3647 | Legacy agent script commands no longer supported |
| 3650 | Legacy agent script commands no longer supported |
| 3652 | Legacy agent script commands no longer supported |
| 3654 | Legacy agent script commands no longer supported |
| 3656 | Legacy agent script commands no longer supported |
| 3658 | Legacy agent script commands no longer supported |
| 3659 | Legacy agent script commands no longer supported |
| 3660 | Legacy agent script commands no longer supported |
| 3661 | Legacy agent script commands no longer supported |
| 3663 | Legacy agent script commands no longer supported |
| 3665 | Legacy agent script commands no longer supported |
| 3667 | Legacy agent script commands no longer supported |
| 3669 | Legacy agent script commands no longer supported |
| 3671 | Legacy agent script commands no longer supported |
| 3672 | Legacy agent script commands no longer supported |
| 3675 | Legacy agent script commands no longer supported |
| 3676 | Legacy agent script commands no longer supported |
| 3677 | entity %i is not a player or agent |
| 3678 | not an entity |
| 3679 | entity %i is not a player or agent |
| 3680 | entity %i is not a player |
| 3681 | not an entity |
| 3682 | not an entity |
| 3683 | Tried to set a read only entity field |
| 3684 | issued a second playsound with notification string before the first finished on entity %i classname %s targetname %s location %g %g %g old string %s alias %s new string %s alias %s at time %i |
| 3685 | weapon hand must be 'right' or 'left' |
| 3686 | Hybrid scope state can only be 0 or 1 |
| 3687 | Weapon is not supplied. Usage: level.player IsAlternateMode( <weapon>, <requires complete match>, <ignore inventory validation> ) |
| 3688 | Too many parameters supplied to IsAlternateMode. Usage: level.player IsAlternateMode( <weapon>, <requires complete match>, <ignore inventory validation> ) |
| 3689 | too many parameters. |
| 3690 | too many parameters. |
| 3691 | You cannot call ClearHighPriorityWeapon with a NULL weapon. |
| 3692 | The weapon passed in to clear the high priority weapon does not match with what is set as the high priority weapon. Please verify the script implementation. Weapon passed in: %s High priority weapon: %s |
| 3696 | getHeldOffhand() takes 0 parameters. |
| 3697 | Incorrect number of parameters. |
| 3698 | Must specify 'smoke', 'flash', 'frag', 'throwingknife', or 'none' class to set primary offhand to. |
| 3699 | Incorrect number of parameters. |
| 3700 | Must specify 'smoke', 'flash', 'frag', 'throwingknife', or 'none' class to set secondary offhand to. |
| 3701 | Incorrect number of parameters. |
| 3702 | Must specify 'smoke', 'flash', 'frag', 'throwingknife', or 'none' class to set special offhand to. |
| 3703 | SwitchToWeapon or SwitchToWeaponImmediate can only be called on a player or bot agent |
| 3705 | USAGE: <entity> GiveExecution( <execution_name>, <equipped_propWeapon_name> ) |
| 3706 | Not a valid execution asset: %s. |
| 3707 | USAGE: <entity> ClearExecution() |
| 3708 | Entity passed to ClearDamageIndicators is not a player |
| 3709 | Entity passed to GetGunAngles is not a player or agent entity |
| 3710 | isUseInProgress must be called on a player |
| 3711 | USAGE: <player> setspreadoverride( <spread> ) |
| 3712 | setspreadoverride: spread must be > 0 |
| 3713 | setspreadoverride: spread must be < %d |
| 3714 | USAGE: <player> resetspreadoverride() |
| 3715 | USAGE: <player> SetCinematicMotionOverride( <cinematicmotion asset name> ) |
| 3716 | CinematicMotion asset name is invalid. |
| 3717 | USAGE: <player> ClearCinematicMotionOverride() |
| 3718 | USAGE: <player> SetAimSpreadMovementScale( <movement scale> ) |
| 3719 | SetAimSpreadMovementScale: spread must be between 0 and 1 inclusive. |
| 3720 | giveachievement [name] |
| 3721 | Too many parameters for SetWeaponAmmoStock(), expecting 2. |
| 3722 | Too many parameters for SetWeaponAmmoStock(), expecting 2. |
| 3723 | SetClientOmnvar - '%s' not found |
| 3724 | SetClientOmnvarBit - '%s' not found |
| 3725 | SetClientOmnvarBit - '%s' expects a bit field omnvar |
| 3726 | SetOmnvarBit - '%s' expects three paramaters: <bit field omnvar>, <index>, and <value> |
| 3727 | SetOmnvarBit - '%s' third parameter should be a boolean. |
| 3728 | USAGE: <player> BeginLocationSelection( <boolean>, <boolean>, <boolean> ) |
| 3729 | USAGE: <player> allowads( <boolean> ) |
| 3730 | USAGE: <player> allowreload( <boolean> ) |
| 3731 | Can't disable stand when player is swimming |
| 3732 | Cannot disable stand when player cannot crouch or prone. |
| 3733 | Cannot disable crouch when player cannot stand or prone. |
| 3734 | Cannot disable prone when player cannot stand or crouch. |
| 3737 | USAGE: <player> AllowMovement( <boolean> ) |
| 3738 | USAGE: <player> LimitedMovement( <boolean> ) |
| 3740 | USAGE: <player> AllowWallRun( <boolean> ) |
| 3744 | USAGE: <player> EnableCollisionNotifies( <boolean> ) |
| 3749 | USAGE: <player> SetSuit( <suitName> ) |
| 3750 | Can't find suit '%s' |
| 3764 | Expected inventory types. Use GetWeaponsListAll() if you just want all weapons. |
| 3765 | Unknown weapon inventory type "%s". |
| 3766 | DisableEmptyClipWeaponSwitch needs one argument, either true or false. |
| 3767 | USAGE: CanPlayerPlaceSentry( <checkEntities>, <radius> ) |
| 3768 | USAGE: CanPlayerPlaceTank() needs to be called on player. |
| 3769 | USAGE: CanPlayerPlaceTank( <radius>, <height>, <forward distance>, <up distance>, <sweep distance>, <min normal> ) |
| 3770 | USAGE: SetPerk( <perk name>, <is code perk> ) |
| 3771 | Unknown perk: %s |
| 3772 | USAGE: HasPerk( <perk name> ) |
| 3773 | Unknown perk: %s |
| 3774 | USAGE: UnsetPerk( <perk name>, <is code perk> ) |
| 3775 | Unknown perk: %s |
| 3776 | notifyOnPlayerCommand must be called on a player or agent entity |
| 3777 | USAGE: notifyOnPlayerCommand( <notify>, <command> ) |
| 3778 | NotifyOnPlayerCommand used an unsupported command '%s' on a Bot |
| 3779 | notifyOnPlayerCommandRemove must be called on a player entity |
| 3780 | USAGE: notifyOnPlayerCommandRemove( <notify>, <command> ) |
| 3781 | NotifyOnPlayerCommand used an unsupported command '%s' on a Bot |
| 3782 | SetHasRadar takes exactly one parameter |
| 3783 | SetIsRadarBlocked takes exactly one parameter |
| 3784 | SetRadarStrength takes exactly one parameter |
| 3785 | Illegal radar strength value'%d'. Must be between %d and %d. |
| 3786 | Incorrect number of parameters |
| 3787 | Given scale must be between %i and %i. |
| 3788 | Given fire time scale must be between %i and %i. |
| 3789 | Incorrect number of parameters. |
| 3790 | Entity or vector expected as first argument. |
| 3791 | view kick clamped from %f to 0 |
| 3794 | SetPhaseStatus Failed - Phase system is disabled |
| 3795 | Incorrect number of parameters. |
| 3796 | Entity or vector expected as first argument. |
| 3797 | Blur value must be greater than 0 |
| 3798 | Time must be positive |
| 3799 | usage: setviewmodel(<model name>) |
| 3800 | Should not be using transient models on the server |
| 3801 | Invalid slot (%i) given, expecting 1 - %i |
| 3802 | Invalid option: expected "weapon", "altweapon", or "nightvision". |
| 3803 | SetOrigin(): Cannot invoke SetOrigin() on a client in SPECTATOR mode. |
| 3804 | Entity %d does not have a valid AI type for this function. |
| 3805 | Entity %d does not have a valid AI type for this function. |
| 3811 | arg %d expecting array. |
| 3812 | exceeded max number of points (%d) |
| 3813 | element %d in array is not a valid vector. |
| 3814 | ignore points specified without providing radius. |
| 3815 | ignore radius must be > 0 |
| 3816 | direction specified without providing weight. |
| 3817 | no one was using this function, so did not bother implementing an ignorepoints version. |
| 3819 | ignore radius must be > 0 |
| 3820 | direction specified without providing weight. |
| 3821 | no one was using this function, so did not bother implementing an ignorepoints version. |
| 3822 | ent (arg1) was not an AI |
| 3823 | AI (arg1) does not have a navigator |
| 3824 | FindOpenLookDir only works with AI with 2D nav |
| 3825 | StartPathNodes: Exceeded max number of elements (%d). |
| 3826 | StartPathNodes: Element %d of points array is not a vector 3 |
| 3827 | cannot set goal volume when a goal entity is set |
| 3828 | FindLastPointOnPathWithinVolume: Entity %d must have a navigator. |
| 3829 | GetLastPathPointWithinGoal: Entity %d must have a navigator. |
| 3830 | GetPosOnPath: Entity %d must have a navigator. |
| 3831 | IsAtValidLongDeathSpot: Entity %d must have a navigator. |
| 3832 | PathDistToGoal: Entity %d must have a navigator. |
| 3833 | EnableTeamwalking: Entity %d does not have a navigator. |
| 3834 | accuracy mod must be nonnegative |
| 3835 | accuracy mod must be nonnegative |
| 3836 | ShootCustomWeapon: Sorry, tagflash override does not work properly with weapon (%s) with attachments/etc. |
| 3837 | FindBestCoverNode: Called on a dead actor! |
| 3838 | FindBestCoverList: Called on a dead actor! |
| 3839 | cannot change node when using fixedNode mode |
| 3840 | cannot change node when nodeSelect.keepClaimedNode is set |
| 3841 | invalid stance '%s' in isStanceAllowed() |
| 3842 | SetGoalNode( ent %d, node pos %s ) : Unable to find navmesh in this area. Either no mesh is loaded, or this is a moving platform where all of the mesh has been invalidated due to an obstacle. |
| 3843 | SetGoalPos( ent %d, pos %s ) : Unable to find navmesh at this position. Either there is no mesh loaded, or this is a moving platform where the whole mesh has been invalidated by an obstacle. |
| 3844 | SetGoalPath: %d exceeds max allowed scripted path points (%d) |
| 3845 | SetGoalPath: Invalid object %d in array. |
| 3846 | SetGoalPath: Unable to find origin vector in object %d in array. |
| 3847 | SetGoalPath: array element %d is not a vector. |
| 3848 | SetGoalPath: no path points provided. |
| 3849 | SetGoalPath called on ent with invalid/unsupported navigator type. |
| 3850 | SetGoalPath called with duplicated path point %s |
| 3851 | Cannot set goal volume when a goal entity is set. AI: %d |
| 3852 | Cannot set goal volume (%s) on AI %d which does not contain goal position %s. |
| 3853 | cannot set goal volume when a goal entity is set |
| 3854 | SetGoalVolumeAuto( ent %d, vol midpt %s ): Unable to find navmesh at this position. Either no mesh is loaded, or this is a moving platform that has had all of its mesh invalidated. |
| 3855 | SetGoalVolumeAuto: Parameter 1 must be a vector or undefined. |
| 3856 | illegal call to isingoal() |
| 3857 | illegal call to EnableAvoidance() |
| 3858 | illegal call to SetAvoidanceReciprocity() |
| 3859 | illegal call to SetAvoidanceRadius() |
| 3860 | illegal call to SetAvoidanceBounds() |
| 3861 | CanShootEnemy() called with no enemy set for entity %d |
| 3862 | EnableTraversals: AI %d does not have a valid navigator. |
| 3863 | EnableTraversals: improper usageflag set. |
| 3864 | SetC8ObstacleFlag - doesn't apply to actors. C8s should use their own nav layer and have obstacles applied to those layers. |
| 3865 | SetC8ObstacleFlag - only applies to 2D navigators. |
| 3866 | SetNavLayer - only applies to 2D navigators. |
| 3867 | SetNavLayer - unknown nav layer name. |
| 3868 | SetNavLayer - no navmesh present for layer %s |
| 3869 | AISetDesiredSpeed: speed cannot be negative. (%.2f) |
| 3870 | AISetDesiredSpeed: speed way larger than anything we were expecting: %.2f |
| 3871 | invalid speedscale mode %s. supports none, speed, strafe. |
| 3872 | GetDesiredScaledSpeedForPosAlongPath: Invalid argument count |
| 3873 | GetDesiredScaledSpeedForPosAlongPath: AI doesn't have path yet |
| 3874 | Invalid OrientMode 'face angle' on a 3D actor. Did you mean to use 'face angle 3d'? |
| 3875 | OrientMode - invalid angle provided to 'face angle' |
| 3876 | OrientMode - invalid angles provided to 'face angle 3d' |
| 3877 | 2D Actor cannot face (0, 0, *) |
| 3878 | cannot face (0, 0, 0) |
| 3879 | orientMode must be 'face angle', 'face angle 3d', 'face current', 'face direction', 'face enemy', 'face enemy or motion', 'face goal', 'face motion', 'face point', or 'face default' 'face direction' and 'face point' take a second argument that is a vector giving the way to face 'face angle' takes a second argument that is a yaw angle |
| 3880 | Script OrientMode must be set to face_angles during motionwarp. Current ASM State: %s |
| 3881 | Invalid OrientMode option used on a 3D actor |
| 3882 | illegal call to animmode(%s) |
| 3883 | Sorry, CanSeePeripheral only works with sentient targets. |
| 3884 | Actor [%s] doesn't have a grenade weapon set. |
| 3885 | ShootBlank() only works with bullet weapons. Using weapon [%s] |
| 3886 | AI does not have a navigator |
| 3887 | MayMoveToPoint does not work with 3D nav. Use a nav trace (allow edge) instead. |
| 3888 | illegal call to ClearPotentialThreat() |
| 3889 | 3D actors cannot use ClearPotentialThreat() |
| 3890 | illegal call to SetPotentialThreat() |
| 3891 | 3D actors cannot use SetPotentialThreat() |
| 3892 | no stances given in allowedStances() |
| 3893 | invalid stance '%s' in allowedStances() |
| 3894 | no allowed stances given |
| 3895 | Min dist falloff must be <= min dist. [%f < %f] |
| 3896 | Max dist falloff must be >= max dist. [%f > %f] |
| 3897 | Invalid argument to SetGunAdditive(), check function description for valid options |
| 3898 | Do not use setentitytarget to set an AI or player as a target |
| 3899 | Threat fraction must be in (0, 1] for AI %d |
| 3901 | Turn anim %s does not have code_move notetrack! |
| 3903 | GetNavSpaceEnt : Space has invalid parent ent %d. |
| 3907 | <ai> MagicGrenade <origin> <target position> [time to blow (seconds)] [should throw]. |
| 3908 | Actor [%s] doesn't have a grenade weapon set. |
| 3909 | <actor> MagicGrenadeManual <origin> <velocity> [time To Blow (seconds)]. |
| 3910 | AI [%s] doesn't have a grenade weapon set. |
| 3911 | AnimScripted entities are not supported in this game mode |
| 3912 | AnimScripted entities are not supported in this game mode |
| 3913 | blended nonsynchronized animation has no concept of time |
| 3915 | '%s' is an illegal radar mode. Must be '%s' or '%s' |
| 3916 | unknown team '%s' |
| 3917 | client is not pointing to the level.clients array |
| 3918 | Failed to set sessionTeam string to '%s' for client %i. |
| 3919 | '%s' is an illegal sessionstate string. Must be playing, dead, spectator, or intermission. |
| 3920 | ClientScr_SetScore is attempting to truncate a negative number into an F_USHORT (which is unsigned) |
| 3921 | ClientScr_SetScore is attempting to set the score to %d but the maximum value is %d |
| 3922 | forceSpectatorClient can only be set to -1, or a valid client number |
| 3923 | killcamentity can only be set to -1, or a valid entity number |
| 3924 | killcamentitylookat can only be set to a valid entity number |
| 3925 | Unknown statusicon material '%s'. Use precache_statusicon. |
| 3926 | archiveTime must be a positive number. requested %f ms |
| 3927 | psOffsetTime must be a positive number. requested %f ms |
| 3928 | Invalid weapon name (%s) specified. See console log for details. |
| 3929 | Invalid weapon name (%s) specified. See console log for details. |
| 3930 | Invalid weapon parameter specified. Weapon must be a weapon string, weapon object or weapon item entity. |
| 3932 | Too many weapons requested. Requested %d weapons but only %d are supported for this call. |
| 3933 | All weapons in the array need to be weapon strings or weapon objects. |
| 3934 | Failed to navigate into DDL array index %d |
| 3935 | Failed to navigate into DDL field %s |
| 3936 | Not enough arguments. See log. |
| 3937 | Too many arguments. See log. |
| 3938 | %s: "%s" %s |
| 3939 | Too many arguments. See log. |
| 3940 | DDL navigation failed at root. |
| 3941 | Entity is not an actor or a Scripted Agent |
| 3942 | not an entity |
| 3943 | not an entity |
| 3944 | Only valid on players; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 3945 | unknown team '%s' |
| 3946 | illegal call to createprintchannel() |
| 3947 | Unable to create new channel. Maximum number of channels exeeded. |
| 3948 | illegal call to setprintchannel() |
| 3949 | Invalid Print Channel |
| 3950 | Invalid Print Channel |
| 3951 | Invalid Print Channel |
| 3952 | Script does not have permission to print to this channel |
| 3953 | Script localization error; the specific reason is supplied by the caller |
| 3954 | Illegal localized string reference: %s must contain only alpha-numeric characters and underscore or '/' |
| 3956 | Unknown weapon name "%s": weapon may need to be added to the fast file. Or add attachments to the base weapon description. |
| 3957 | Weapon '%s' is does not define a valid weapon |
| 3958 | Asked for weapon "%s", but got weapon "%s" - check for missing or misspelled attachments, and make sure they are sorted alphabetically |
| 3959 | invalid args to printtoscreen3d |
| 3960 | illegal call to print3d() |
| 3961 | illegal call to line() |
| 3962 | illegal call to box() |
| 3963 | illegal call to DrawEntityBounds() |
| 3964 | illegal call to OrientedBox() |
| 3965 | illegal call to Sphere() |
| 3966 | illegal call to Cylinder() |
| 3967 | SetOmnvarInternal - '%s' is a unsigned type, and can not be set to negative values |
| 3968 | SetOmnvarInternal - '%s' cannot be set to a value of type entity |
| 3969 | SetOmnvarInternal - '%s' cannot be set to a value of undefined |
| 3970 | SetOmnvarInternal - '%s' can only be set to an entity or undefined |
| 3971 | SetOmnvarInternal - '%s' setting to %d would exceed this omnvar's specified range [%d,%d] |
| 3973 | SetOmnvarInternal - '%s' is a LUI NetConstString, and cannot be set to '%s'. Did you add this string to ncsLuiStrings.txt? |
| 3974 | SetOmnvarInternal - Type for paramater %d not recognized |
| 3975 | SetOmnvar - '%s' not found |
| 3976 | SetOmnvar '%s' must be done on a game-scope Omnvar |
| 3977 | SetOmnvarBit - '%s' not found |
| 3978 | SetOmnvar '%s' must be done on a game-scope Omnvar |
| 3979 | SetOmnvarBit - '%s' expects a bit field omnvar |
| 3980 | SetOmnvarBit - '%s' expects three paramaters: <bit field omnvar>, <index>, and <value> |
| 3981 | SetOmnvarBit - '%s' third parameter should be a boolean. |
| 3982 | GetOmnvar - '%s' not found |
| 3983 | GetOmnvar '%s' must be done on a a game-scope Omnvar |
| 3984 | SetOmnvar - Type for paramater 1 not recognized |
| 3985 | Invalid Dvar set: %s - Internal Dvars cannot be changed by script. Use 'setsaveddvar' to alter SAVED internal dvars |
| 3986 | Dvar %s has an invalid dvar name |
| 3987 | SetDynamicDvar is only necessary in Multiplayer. Use SetDvar instead. |
| 3988 | Dvar %s has an invalid dvar name |
| 3989 | Dvar %s has an invalid dvar name |
| 3990 | GScr_SetDvarIfUninitialized( dvarName, initialValue ) requires two parameters |
| 3991 | GScr_SetDevDvarIfUninitialized( dvarName, initialValue ) requires two parameters |
| 3992 | GetDvar( <dvar>, <default> ) takes either one or two parameters |
| 3993 | GetDvarInt( <dvar>, <default> ) takes either one or two parameters |
| 3994 | GetDvarFloat( <dvar>, <default> ) takes either one or two parameters |
| 3995 | GetDvarVector( <dvar>, <default> ) takes either one or two parameters |
| 3996 | GetDvarVector: Dvar %s '%s' isn't parsable as a vector |
| 3997 | unknown ammo name pool for weapon '%s' |
| 3998 | non-primitive animation '%s' has no concept of length |
| 4000 | array is too large (%d > %d) |
| 4001 | element %i of array: type %s is not an entity |
| 4002 | StopSounds() does not support entities of type 'ET_EVENTS' [Ent #%i]. |
| 4003 | MagicGrenadeManual <grenade type> <origin> <velocity> [time To Blow (seconds)]. |
| 4004 | "%s" grenade weapon is not precached |
| 4005 | Invalid grenade weapon specified for MagicGrenadeManual |
| 4006 | entity type '%s' is not a turret |
| 4007 | entity type '%s' is not a turret |
| 4008 | entity type '%s' is not a turret |
| 4009 | entity type '%s' is not a turret |
| 4010 | entity type '%s' is not a turret |
| 4011 | entity type '%s' is not a turret |
| 4012 | Unsafely setting the player's world up in collision, talk to Ben K. with questions. |
| 4013 | entity type '%s' is not a client, can only set world up ref on client. |
| 4014 | Could not get playerstate. |
| 4015 | Setting the world up while linked is only allowed if the world up ent is in the same hierarchy as the linked parent. entNum = %d |
| 4016 | entity type '%s' is not a client, can only teleport world up ref on client. |
| 4017 | Could not get playerstate. |
| 4018 | SetWorldUpReferenceAngles for %s can only be called on a client. |
| 4019 | GetWorldUpReferenceAngles for %s can only be called on a client. |
| 4020 | GetWorldUpReferenceAngles for %s can only be called on a client. |
| 4021 | entity type '%s' is not a turret |
| 4022 | entity type '%s' is not a turret |
| 4023 | entity type '%s' is not a turret |
| 4024 | turret number '%i' is not auto or sentry |
| 4025 | turret number '%i' is being carried and cannot have its owner cleared. |
| 4026 | turrets can only be owned by clients or actors |
| 4027 | entity type '%s' is not a turret |
| 4028 | turret number '%i' is being carried and cannot have its owner cleared. |
| 4029 | turrets can only be owned by clients or actors |
| 4030 | entity type '%s' is not a turret |
| 4031 | turret number '%i' is not a sentry |
| 4032 | turrets can only be carried by clients or actors |
| 4033 | entity type '%s' is not a turret |
| 4034 | entity type '%s' is not a turret |
| 4035 | entity type '%s' is not a turret |
| 4036 | entity type '%s' is not a turret |
| 4037 | Vector target offset has NAN |
| 4038 | entity type '%s' is not a turret |
| 4039 | entity type '%s' is not a turret |
| 4040 | entity type '%s' is not a turret |
| 4041 | entity type '%s' is not a turret |
| 4042 | Convergence type should be either 'pitch' or 'yaw' |
| 4043 | entity type '%s' is not a turret |
| 4044 | expecting one float argument |
| 4045 | <scaler> must between 0 and 1 inclusive |
| 4046 | entity type '%s' is not a turret |
| 4047 | entity type '%s' is not a turret |
| 4048 | entity type '%s' is not a turret |
| 4049 | entity is not a turret |
| 4050 | entity is not a turret |
| 4051 | entity is not a turret |
| 4052 | entity is not a turret |
| 4053 | entity is not a turret |
| 4054 | need a positive duration |
| 4055 | illegal call to setdefaultdroppitch() |
| 4056 | entity is not a turret |
| 4057 | illegal call to restoredefaultdroppitch() |
| 4058 | entity is not a turret |
| 4059 | entity type '%s' is not a turret |
| 4060 | entity type '%s' is not a turret |
| 4061 | Missing parameter: turret TurretSetBarrelSpinEnabled( <enabled> ); |
| 4062 | In TurretSetBarrelSpinEnabled: entity type '%s' is not a turret |
| 4063 | entity type '%s' is not a turret |
| 4064 | entity type '%s' is not a turret |
| 4065 | vehicle entity %i is not being controlled by the player. |
| 4066 | entity type '%s' is not a player nor a player controlled vehicle |
| 4067 | GetPlayerRollVelocity must be called on a player. |
| 4068 | Currently on supported on damage triggers |
| 4069 | Currently on supported on damage triggers |
| 4070 | ForceHideGrenadeHudWarning() invoked on invalid or non-grenade entity. |
| 4071 | USAGE: <ent> ForceHideGrenadeHudWarning( <bool> ) |
| 4072 | Missile_SetPhaseState() invoked on invalid or non-missile entity. |
| 4073 | USAGE: <ent> Missile_SetPhaseState( <bool> ) |
| 4074 | Entity %i is not a rocket - eType '%i' |
| 4075 | Entity %i is not a rocket - classname '%s' |
| 4076 | Entity %i is not a rocket - it is a grenade |
| 4077 | Entity %i is not locked on to anything. |
| 4078 | entity type '%s' is not a missile |
| 4079 | SetBountyCount(). Only valid on players; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4080 | USAGE: <player> SetBountyCount( <int> ) |
| 4081 | Bounty count limit exceeded! The maximum supported bounty count is %d |
| 4082 | SetPerkIcon(). Only valid on players; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4083 | USAGE: <player> SetPerkIcon( <string> ) |
| 4084 | '%s' is not a valid perk icon image. Please make sure it is included as precache_image in zone_source. |
| 4085 | SetSquadIndex(). Only valid on players; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4086 | USAGE: <player> SetSquadIndex( <int> ) |
| 4087 | Squad index limit exceeded! The maximum supported squad index is %d |
| 4088 | SetNonStick() invoked on invalid entity. |
| 4089 | USAGE: <ent> SetNonStick( <bool> ) |
| 4090 | GetNonStick() invoked on invalid entity. |
| 4091 | SetScriptableBeamLength() invoked on invalid entity. |
| 4092 | SetScriptableBeamLength() length is longer than LERP_ENTITY_STATE_MISSILE_EFFECT_MAX_LENGTH %f |
| 4093 | SetScriptableBeamLength() currently only supports missile ents, but could be changed by code with additional work and bandwidth cost. |
| 4094 | SetNoDeploy() invoked on invalid entity. |
| 4095 | USAGE: <ent> SetNoDeploy( <bool> ) |
| 4096 | Scale must be greater than 0 |
| 4097 | duration must be greater than 0 |
| 4098 | Radius must be greater than 0 |
| 4099 | only valid on players; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4100 | only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4101 | USAGE: <player> stopshellshock() |
| 4102 | only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4103 | USAGE: <player> FadeOutShellShock() |
| 4104 | Invalid shellshockDuration %d. It must be < %d |
| 4105 | Can't set the model '%s' to a transient model from the server |
| 4106 | Cannot remove an entity's model while it is playing an animation. Call scriptmodelclearanim first. |
| 4107 | Trying to set model for entity in a way where the existing child models will no longer have the correct attach bones, this will cause child models to appear in front of the camera. %s |
| 4108 | SetTeamInHudData takes at least 1 parameter |
| 4109 | GetMLGSettings takes at least 1 parameter |
| 4110 | SetOtherEnt() cannot be called on a client |
| 4111 | unable to spawn "%s" entity |
| 4112 | unable to spawn "%s" entity |
| 4113 | USAGE: SpawnCoverWall( <origin>, <angles> <optional:healthPerBlock> ) |
| 4114 | DeSpawnCoverWall() called on an entity that is not a cover wall. |
| 4115 | precacheTurret must be called before any wait statements in the level script |
| 4116 | Should not be using transient models on the server |
| 4117 | model '%s' already attached to tag '%s' |
| 4118 | failed to attach model '%s' to tag '%s' |
| 4119 | model '%s' not attached to tag '%s' |
| 4138 | ModifySpaceJumpPath must be called on a player. |
| 4139 | failed to detach model '%s' from tag '%s' |
| 4140 | bad index |
| 4141 | bad index |
| 4142 | bad index |
| 4143 | entity has no model |
| 4144 | cannot find part '%s' in entity model |
| 4145 | cannot find part '%s' in entity model '%s' |
| 4146 | entity has no model |
| 4147 | entity has no model |
| 4148 | cannot find part '%s' in entity model '%s' |
| 4149 | cannot find part '%s' in entity model |
| 4150 | entity has no model |
| 4151 | entity has no model |
| 4152 | cannot find part '%s' in entity model |
| 4153 | cannot find part '%s' in entity model '%s' |
| 4154 | entity has no model |
| 4155 | Value %f is greater than SPEED_SCALE_MULTIPLIER_MAX_SIZE, please increase define. |
| 4156 | not an entity |
| 4157 | This function doesn't support player entities. |
| 4158 | scriptable (model: '%s', type: '%s') link failed. Call EnableLinkTo() on the scriptable first before calling LinkTo() |
| 4160 | entity (classname: '%s', type: '%s') does not currently support linkTo |
| 4162 | failed to link entity %i to entity %i: %s |
| 4163 | not a boolean - allowUnlinkInCollision |
| 4164 | This function only supports players and actors. |
| 4165 | This function only supports moving platform entity types. |
| 4166 | entity already has linkTo enabled |
| 4167 | entity (classname: '%s', type: '%s') does not currently support enableLinkTo |
| 4169 | not an entity |
| 4170 | not a player entity |
| 4171 | player does not support linking |
| 4172 | This function only supports linking to tags named 'tag_player'. |
| 4173 | failed to link entity %i to entity %i: %s |
| 4174 | Not an entity |
| 4175 | Not a player entity |
| 4176 | failed to link entity %i to entity %i: %s |
| 4177 | Player is not linked. |
| 4179 | PlayerUnlinkOnJump is disabled |
| 4180 | Player is not linked. |
| 4181 | Player is not linked. |
| 4182 | Player must be linked using PlayerLinkWeaponViewToDelta() |
| 4183 | Player is not linked. |
| 4184 | Player must be linked using PlayerLinkWeaponViewToDelta() |
| 4185 | total time must be non negative |
| 4186 | acceleration time must be non negative |
| 4187 | deceleration time must be non negative |
| 4188 | accel time plus decel time is greater than total time |
| 4189 | Not enough parameters. |
| 4190 | failed to link entity %i to entity %i: %s |
| 4191 | Incorrect number of parameters. See script docs. |
| 4192 | Must be linked to an entity. |
| 4193 | Angle locked to linked entity. View clamp is 0. |
| 4194 | SetViewAngleResistance is not supported in this game mode |
| 4195 | Incorrect number of parameters. See script docs. |
| 4196 | MakeUsable may not be called on player or agent type entities |
| 4197 | SetCursorHint called on missile without EnableMissileHint() activation. |
| 4198 | SetCursorHint may not be called on player or agent type entities |
| 4199 | %s is not a valid hint type. See above for list of valid hint types |
| 4200 | SetHintString called on missile without EnableMissileHint() activation. |
| 4201 | The setHintString command only works on trigger_use, trigger_use_touch, turret, actor, and script entities. |
| 4202 | The SetHintStringParams command only works on trigger_use, trigger_use_touch, turret, actor, and script entities. |
| 4203 | '%s' is not a valid image. Please make sure it is included as precache_image in zone_source. |
| 4204 | The SetHintStringParams command only works on trigger_use, trigger_use_touch, turret, actor, and script entities. |
| 4205 | The SetHintStringParams passed an unhandled type for parameter %u |
| 4206 | Failed to set the use radius for entity %d |
| 4207 | Illegal duration string "%s". Valid strings are "duration_none", "duration_short", "duration_medium", "duration_long" |
| 4208 | SetUseCommand: NULL useCommand provided. |
| 4209 | SetUseCommand: (%s) is not a valid use command. |
| 4210 | Acceptable values for SetHintOnObstruction are "hide" and "show". |
| 4211 | only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4212 | SetHUDTutorialMessage can't find omnvar ui_tutorial_message_show - check omnvars.csv |
| 4213 | only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4214 | ClearHUDTutorialMessage can't find omnvar ui_tutorial_message_show - check omnvars.csv |
| 4215 | SetHUDWarningOmnvars can't find omnvars - check omnvars.csv |
| 4216 | only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4217 | GScr_AddHUDWarningMessage unable to find given warning key. Please check hudwarnings.csv |
| 4218 | only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4219 | Invalid parameters. Usage: player SetImpactFx( <tableName> ) |
| 4220 | Invalid ent, ent needs to be a player. |
| 4221 | ScriptMoverOutline Must be called on a script mover / MarkKeyframedMover must be called on a script mover / UnmarkKeyframedMover must be called on a script mover |
| 4222 | ScriptMoverOutline Must be called on a script mover |
| 4223 | ScriptMoverClearOutline Must be called on a script mover |
| 4224 | ScriptMoverThermal Must be called on a script mover |
| 4225 | ScriptMoverClearThermal Must be called on a script mover |
| 4226 | MissileThermal Must be called on a missile |
| 4227 | MissileHideTrail Must be called on a missile |
| 4228 | MissileOutline Must be called on a missile |
| 4229 | usage: HudOutlineEnableForClient( <client>, <hudOutline> ); |
| 4230 | Invalid client entity. |
| 4232 | usage: HudOutlineDisableForClient( <client> ); |
| 4233 | Invalid client entity. |
| 4234 | An empty array is not valid. |
| 4235 | All array elements need to be client entities. |
| 4236 | All array elements need to be client entities. |
| 4237 | All array elements need to be client entities. |
| 4238 | All array elements need to be character entities. |
| 4239 | usage: HudOutlineEnableForClients( <client array>, <hudOutline> ); |
| 4241 | usage: HudOutlineDisableForClients( <client array> ); |
| 4242 | Usage: HudOutlineEnable( <hudOutline> |
| 4244 | only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4245 | SetStance is only defined for players. |
| 4246 | Cannot delete a client entity |
| 4247 | Cannot delete an agent entity |
| 4248 | Cannot delete entity '%i' during its think |
| 4249 | Cannot delete a 'willNeverChange' entity |
| 4250 | Cannot delete a radiant placed scriptable |
| 4251 | getEye must be called on a sentient entity. Use self MakeEntitySentient() with '%s' |
| 4252 | Entity is not a client or agent. |
| 4253 | AIPhysicsTrace: arg 6 (bUseActorPhysicsMask) can only be used with a valid actor or agent! |
| 4254 | Physics_SetGravity takes 1 vector parameter |
| 4255 | Physics_SetGravity can't find omnvars - check omnvar.csv |
| 4256 | Physics_SetGravity has invalid omnvar data - check omnvar.csv |
| 4257 | Physics_SetGravityRagdollScalar takes 1 vector parameter |
| 4258 | Physics_SetGravityRagdollScalar doesn't support tiny values between -0.01 and 0.01 |
| 4259 | Physics_SetGravityRagdollScalar can't find omnvars - check omnvar.csv |
| 4260 | Physics_SetGravityRagdollScalar has invalid omnvar data - check omnvar.csv |
| 4261 | Physics_SetGravityDynentScalar takes 1 vector parameter |
| 4262 | Physics_SetGravityDynentScalar doesn't support tiny values between -0.01 and 0.01 |
| 4263 | Physics_SetGravityDynentScalar can't find omnvars - check omnvar.csv |
| 4264 | Physics_SetGravityDynentScalar has invalid omnvar data - check omnvar.csv |
| 4265 | Physics_SetGravityParticleScalar takes 1 vector parameter |
| 4266 | Physics_SetGravityParticleScalar doesn't support tiny values between -0.01 and 0.01 |
| 4267 | Physics_SetGravityParticleScalar can't find omnvars - check omnvar.csv |
| 4268 | Physics_SetGravityParticleScalar has invalid omnvar data - check omnvar.csv |
| 4269 | Physics_SetGravityItemScalar takes 1 vector parameter |
| 4270 | Physics_SetGravityItemScalar doesn't support tiny values between -0.01 and 0.01 |
| 4271 | RandomInt takes 1 parameter |
| 4272 | RandomInt parm must be positive integer. |
| 4273 | RandomFloat takes 1 parameter |
| 4274 | RandomIntRange's second parameter must be greater than the first. |
| 4275 | Scr_RandomFloatRange's second parameter must be greater than the first. |
| 4276 | USAGE: <trigger_use\|trigger_use_touch> UseTriggerRequireLookAt( <bool> ) |
| 4277 | The UseTriggerRequireLookAt command only works on trigger_use and trigger_use_touch entities. |
| 4278 | precache %s '%s' failed |
| 4279 | precacheModel must be called before any wait statements in the gametype or level script |
| 4280 | Model name string is empty |
| 4281 | Can't precache transient models |
| 4282 | precacheCompositeModel must be called before any wait statements in the gametype or level script |
| 4283 | Model name string is empty |
| 4284 | unknown rumble name '%s' |
| 4285 | Incorrect number of parameters. |
| 4286 | unknown rumble name '%s' |
| 4287 | PlayRumbleOnPosition [rumble name] [pos] |
| 4288 | PlayRumbleLoopOnPosition [rumble name] [pos] |
| 4289 | PlayRumbleOnPositionForClient [rumble name] [pos] |
| 4290 | PlayRumbleLoopOnPositionForClient [rumble name] [pos] |
| 4291 | unknown rumble name '%s' |
| 4292 | USAGE: StopRumble( <rumblename> ) The rumble name is required. |
| 4293 | Entity is not an item. |
| 4294 | Ammo count must not be negative |
| 4295 | Ammo count must not be negative |
| 4296 | Ammo count must not be negative |
| 4297 | Value out of range. Allowed values: 0 to %i |
| 4298 | precacheShader must be called before any wait statements in the gametype or level script |
| 4299 | Shader name string is empty |
| 4300 | preacheLeaderboards must be called before any wait statements in the gametype or level script |
| 4301 | precacheString must be called before any wait statements in the gametype or level script |
| 4302 | Invalid <range> value specified for radius damage. |
| 4303 | Invalid damage value specified for radius damage. |
| 4305 | Source Damage vector is invalid : %f %f %f |
| 4306 | Usage: doDamage( <health>, <source position>, <attacker>, <inflictor> ) |
| 4307 | Invalid <range> value specified for radius damage ( parameter 2 ). |
| 4308 | Invalid <max_damage> value specified for radius damage ( parameter 3 ). |
| 4309 | Invalid <min_damage> value specified for radius damage ( parameter 4 ). |
| 4310 | entity is not a grenade or projectile or it already exploded |
| 4311 | Entity is not a player or agent |
| 4312 | only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4313 | USAGE: <player> shellshock(<shellshockname>, <duration>) |
| 4314 | Shellshock '%s' does not exist |
| 4315 | duration %g should be >= 0 |
| 4316 | only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4317 | duration %g should be >= 0 |
| 4318 | end time must be between 0 and 1 |
| 4319 | start time must be between 0 and 1 |
| 4320 | end time must be between 0 and 1 |
| 4321 | start time must be between 0 and 1 |
| 4322 | end time must be between 0 and 1 |
| 4323 | start time must be between 0 and 1 |
| 4324 | Incorrect number of parameters |
| 4325 | IsVFXFile: invalid effect id %d |
| 4326 | forward and up vectors are the same direction or exact opposite directions |
| 4327 | effect name must start with 'fx/' or 'vfx/' |
| 4328 | loadFx must be called before any wait statements in the level script, or on an already loaded effect |
| 4330 | Scr_PlayFX: invalid effect id %d |
| 4331 | Incorrect number of parameters |
| 4332 | Entity number %i is not a client |
| 4333 | effect id %i is invalid |
| 4334 | cannot use " characters in tag names |
| 4335 | tag '%s' does not exist on entity with model '%s' |
| 4336 | Incorrect number of parameters |
| 4337 | Incorrect number of parameters |
| 4338 | No longer support arrays of player, please specify a player entity |
| 4339 | Entity number %i is not a client |
| 4340 | effect id %i is invalid |
| 4341 | cannot use " characters in tag names |
| 4342 | tag '%s' does not exist on entity with model '%s' |
| 4343 | Incorrect number of parameters |
| 4344 | Incorrect number of parameters |
| 4345 | No longer support arrays of player, please specify a player entity |
| 4346 | Entity number %i is not a client |
| 4347 | Incorrect number of parameters |
| 4348 | effect id %i is invalid |
| 4349 | cannot use " characters in tag names |
| 4350 | tag '%s' does not exist on entity with model '%s' |
| 4351 | Incorrect number of parameters |
| 4352 | Entity number %i is not a client |
| 4353 | effect id %i is invalid |
| 4354 | cannot use " characters in tag names |
| 4355 | cannot use " characters in tag names |
| 4356 | tag must be a client tag that is in ncsclienttags.txt |
| 4357 | tag must be a client tag that is in ncsclienttags.txt |
| 4358 | Incorrect number of parameters |
| 4359 | Entity number %i is not a client |
| 4360 | effect id %i is invalid |
| 4361 | Incorrect number of parameters |
| 4362 | Radius is negative |
| 4363 | Radius is negative |
| 4364 | Inner radius is outside the outer radius |
| 4365 | Incorrect number of parameters |
| 4366 | Radius is negative |
| 4367 | Radius is negative |
| 4368 | Inner radius is outside the outer radius |
| 4369 | Incorrect number of parameters |
| 4370 | Radius is negative |
| 4371 | Radius is negative |
| 4372 | Inner radius is outside the outer radius |
| 4373 | Jitter is too big - max radius is %.2f |
| 4374 | Maximum jitter is less than minimum jitter |
| 4375 | visionset %s has not been precached, include with precache_vision |
| 4376 | Invalid parameters. |
| 4377 | GetNodeNumber not called on pathnode |
| 4378 | Incorrect usage for SetVisionParams() |
| 4379 | entity %i is not a player or player-controlled agent |
| 4380 | Invalid vision set override for VisionSetAlternate. |
| 4381 | USAGE: VisionSetAlternate( <stage>, <duration>, <optional override> ) |
| 4382 | Incorrect use of getnumparts() |
| 4383 | GetNumParts() used with an empty model name! |
| 4384 | Incorrect usage for getpartname() |
| 4385 | index out of range (0 - %d) |
| 4386 | bad model |
| 4387 | GetAnimName() expects exactly one parameter |
| 4388 | GetAnimName() expects a primitive animation as its parameter |
| 4389 | usage: timeMS = LookupSoundLength( <alias>, <findLongestVariant> ); |
| 4390 | entity has no model defined (classname '%s') |
| 4391 | tag '%s' does not exist in model '%s'%s(or any attached submodels) |
| 4393 | usage: SoundSetTimeScaleFactor( <channelname>, <lerp> ) |
| 4394 | uknown channel: %s |
| 4395 | time scale factor must be between 0 and 1 - %f |
| 4396 | ViewModelOutlineEnable called on an ent that's not a player. |
| 4398 | ViewModelOutlineDisable called on an ent that's not a player. |
| 4408 | SetNVGAreaLightEffect(). Only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4409 | SetNVGAreaLightEffect(). USAGE: <player> SetNVGAreaLightEffect( <use alternate> ) |
| 4410 | AnimScriptSetInputParamReplicationStatus(). Only valid on players; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4411 | AnimScriptSetInputParamReplicationStatus(). USAGE: <player> AnimScriptSetInputParamReplicationStatus( <replicate> ) |
| 4412 | AnimScriptSetParachuteState(). Only valid on players; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4413 | AnimScriptSetInputParamReplicationStatus(). USAGE: <player> AnimScriptSetInputParamReplicationStatus( <replicate> ) |
| 4414 | Invalid parachute state supplied to AnimScriptSetParachuteState |
| 4415 | AnimScriptExitParachuteState(). Only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4416 | AnimScriptExitParachuteState(). Too many parameters. USAGE: <player> AnimScriptExitParachuteState() |
| 4417 | AnimScriptEnterVehicle(). Only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4418 | AnimScriptEnterVehicle(). USAGE: <player> AnimScriptEnterVehicle() |
| 4419 | AnimScriptExitVehicle(). Only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4420 | AnimScriptExitVehicle(). USAGE: <player> AnimScriptEnterVehicle() |
| 4421 | GetLeftStickX(). Only valid on players; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4422 | GetLeftStickX(). USAGE: x = <player> GetLeftStickX() |
| 4423 | GetLeftStickY(). Only valid on players; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4424 | GetLeftStickY(). USAGE: y = <player> GetLeftStickY() |
| 4425 | GetRightStickX(). Only valid on players; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4426 | GetRightStickX(). USAGE: x = <player> GetRightStickX() |
| 4427 | GetRightStickY(). Only valid on players; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4428 | GetRightStickY(). USAGE: y = <player> GetRightStickY() |
| 4429 | PlayAnimScriptEvent(). Only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4430 | PlayAnimScriptEvent(). USAGE: <player> PlayAnimScriptEvent(<eventname>, <optional scene_scriptedanimtype>) |
| 4431 | PlayAnimScriptSceneEvent(). Invalid event name '%s' |
| 4432 | PlayAnimScriptSceneEvent(). Attempting to play anim event '%s', but it is not a scene anim, use PlayAnimScriptEvent for this |
| 4433 | StopAnimScriptEvent(). Only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4434 | StopAnimScriptEvent() takes no arguments |
| 4435 | PlayAnimScriptEvent(). Only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4436 | PlayAnimScriptEvent(). USAGE: <player> PlayAnimScriptEvent(<eventname>, <optional scriptedanimtype>) |
| 4437 | PlayAnimScriptEvent(). Invalid event name '%s' |
| 4438 | PlayAnimScriptEvent(). Attempting to play a scene anim event '%s', use PlayAnimScriptSceneEvent for this |
| 4439 | PlayViewModelAnim(). Invalid number of parameters |
| 4440 | PlayViewModelAnim() missing xanim name |
| 4441 | PlayGestureViewmodel(). Self must be a player |
| 4442 | You cannot play a scripted viewmodel animation when a high priority weapon is set. Player is most likely in the process of switching, or trying to switch, to a high priority weapon. |
| 4443 | MP Anim [%s] needs to be pre-cached in order to work with PlayViewModelAnim() |
| 4444 | PlayViewModelAnim(). Takes No parameters |
| 4445 | PlayGestureViewmodel(). Self must be a player |
| 4446 | IsViewModelAnimPlaying() takes no parameters |
| 4447 | IsViewModelAnimPlaying() self must be a player |
| 4448 | too many parameters |
| 4449 | SetDemeanorViewmodel() missing demeanor state name |
| 4450 | SetDemeanorViewmodel() invalid state name. Valid states are: 'normal', 'safe', 'relaxed'. |
| 4451 | self must be a player |
| 4452 | SetDemeanorViewmodel() invalid gesture asset name. |
| 4453 | too many parameters |
| 4454 | self must be a player |
| 4455 | PlayGestureViewmodel(). Invalid number of parameters |
| 4456 | PlayGestureViewmodel(). Invalid blend time |
| 4457 | PlayGestureViewmodel(). Invalid start time |
| 4458 | PlayGestureViewmodel() missing gesture asset name |
| 4459 | PlayGestureViewmodel(). Self must be a player |
| 4460 | PlayGestureViewmodel() invalid gesture asset name. |
| 4461 | Gesture '%s' %s. |
| 4462 | PlayGestureViewmodel(). Invalid number of parameters |
| 4463 | PlayGestureViewmodel(). Invalid blend time |
| 4464 | PlayGestureViewmodel(). Invalid start time |
| 4465 | ForcePlayGestureViewmodel(). Cancel transitions is available only when stopAllGestures is true and a valid blendtime is provided. |
| 4466 | PlayGestureViewmodel() missing gesture asset name |
| 4467 | PlayGestureViewmodel(). Self must be a player |
| 4468 | PlayGestureViewmodel() invalid gesture asset name. |
| 4469 | Gesture '%s' %s. |
| 4470 | StopGestureViewmodel() too many parameters |
| 4471 | StopGestureViewmodel() missing gesture asset name. |
| 4472 | StopGestureViewmodel(). Invalid 'out' time |
| 4473 | StopGestureViewmodel(). Invalid 'out' time use 0 as the default. |
| 4474 | self must be a player |
| 4475 | StopGestureViewmodel() invalid gesture asset name. |
| 4476 | too many parameters |
| 4477 | GetGestureAnimLength() missing gesture asset name |
| 4478 | self must be a player |
| 4479 | GetGestureAnimLength() invalid gesture asset name. |
| 4480 | GScr_GetGestureStartTime(): invalid parameters. |
| 4481 | GScr_GetGestureStartTime() missing gesture asset name |
| 4482 | GScr_GetGestureStartTime() invalid gesture section. |
| 4483 | self must be a player |
| 4484 | GScr_GetGestureStartTime() invalid gesture asset name. |
| 4485 | GScr_GetGestureStartTime() invalid gesture section. Valid values are: 'in', 'loop', and 'out'. |
| 4486 | IsGestureLooped() too many parameters |
| 4487 | IsGestureLooped() missing gesture asset name. |
| 4488 | IsGestureLooped() self must be a player. |
| 4489 | IsGestureLooped() invalid gesture asset name. |
| 4490 | IsGesturePlaying() too many parameters |
| 4491 | IsGesturePlaying() missing gesture asset name. |
| 4492 | IsGesturePlaying() self must be a player. |
| 4493 | IsGesturePlaying() invalid gesture asset name. |
| 4494 | self must be a player |
| 4495 | GScr_GetGestureNotetrackTimes() wrong number of parameters |
| 4496 | GScr_GetGestureNotetrackTimes() missing gesture asset name |
| 4497 | GScr_GetGestureNotetrackTimes() self must be a player |
| 4498 | GScr_GetGestureNotetrackTimes() invalid gesture asset name. |
| 4502 | Incorrect number of parameters |
| 4503 | Incorrect number of parameters |
| 4504 | Incorrect number of parameters |
| 4505 | Invalid f-stop value |
| 4506 | Focus distance must be >= 0 |
| 4507 | Focus speed must be >= 0 |
| 4508 | Aperture speed must be >= 0 |
| 4509 | Incorrect number of parameters |
| 4510 | Invalid f-stop value |
| 4511 | Focus distance must be >= 0 |
| 4512 | Incorrect number of parameters |
| 4513 | Incorrect number of parameters |
| 4514 | Unknown lens profile mode. |
| 4515 | USAGE: <player> viewkick <force 0-127> <source position> [optional]<bool displayIndicator> |
| 4516 | viewkick: damage %g < 0 |
| 4517 | Expecting 6 arguments |
| 4518 | lower-right X and Y coordinates must be both south and east of upper-left X and Y coordinates in terms of the northyaw |
| 4519 | SetGlareGrimeMaterial is gen4 only (use is_gen4()). |
| 4520 | array is too large (%d > %d), need to increase sortable array size |
| 4521 | element %i of array: type %s is not an object |
| 4522 | element %i of array: object's origin property is not a vector |
| 4523 | Function can only be called on a 'light' entity; actual classname is '%s' |
| 4524 | intensity must be >= 0 |
| 4525 | intensity must be >= 0 |
| 4526 | cannot directly set the origin on AI. Use the teleport command instead. |
| 4527 | origin being set to NAN. |
| 4528 | Cannot set an entity's origin while it is playing a delta animation. Call scriptmodelclearanim first. |
| 4529 | cannot directly set the angles on AI. Use the teleport command instead. |
| 4530 | Cannot set an entity's angles while it is playing a delta animation. Call scriptmodelclearanim first. |
| 4531 | Can't find/allocate ID for tag '%s' |
| 4532 | SetWaterSheeting called on an ent that's not a player or agent. |
| 4533 | entity %i is not a player or player-controlled agent |
| 4534 | Player's controls are already linked. |
| 4535 | Remote turret control is not supported in this game mode |
| 4536 | entity type '%s' is not a turret |
| 4537 | Remote turret control is not supported in this game mode |
| 4538 | entity type '%s' is not a turret |
| 4539 | SetEntityOwner() cannot be called on a client |
| 4540 | MakeVehicleSolidCapsule must be called on a vehicle entity. |
| 4541 | Usage: MakeVehicleSolidCapsule( <radius>, <midz>, <height> ). |
| 4542 | MakeVehicleSolidSphere must be called on a vehicle entity. |
| 4543 | Usage: MakeVehicleSolidSphere( <radius> ). |
| 4576 | GetCollision() requires exactly one argument. |
| 4577 | GetCollision() unknown stance argument provided. |
| 4582 | ControlAgent() functionality is disbled |
| 4586 | RestoreControlAgent() functionality is disbled |
| 4587 | SetSolid() called on entity without a playerState. |
| 4588 | NotifyOnPlayerUpdate can only be set on clients. |
| 4589 | Cannot control an entity that isn't a vehicle or helicopter |
| 4592 | IsVehicleTurretFiring must be called on a turret entity |
| 4593 | MakeCollideWithItemClip must be called on an entity. |
| 4594 | MakeCollideWithItemClip is currently limited to missile entities. |
| 4595 | Usage: MakeCollideWithItemClip () or MakeCollideWithItemClip (enable). |
| 4596 | ERROR: SpawnRagdollConstraint(), specified entity is not a corpse. |
| 4597 | ERROR: SpawnRagdollConstraint(), specified corpse entity is not in ragdoll. |
| 4598 | ERROR: SpawnRagdollConstraint(), invalid hitLocation provided. |
| 4599 | ERROR: SpawnRagdollConstraint(), invalid partName provided. |
| 4600 | MagicBullet invalid parameters |
| 4601 | Owner must be a player, actor, or agent. |
| 4602 | MagicBullet called with unknown weapon name %s |
| 4603 | MagicBullet called with unknown weapon |
| 4604 | MagicBullet() does not work with grenade-type weapons. |
| 4605 | MagicBullet() does not work with script-type weapons. |
| 4609 | MagicBullet() does not work with shield-type weapons. |
| 4610 | MagicBullet() does not work with charge shield-type weapons. |
| 4611 | MagicBullet() does not work with location select-type weapons. |
| 4612 | MagicBullet() does not work with equipment deploy-type weapons. |
| 4613 | MagicBullet() invoked with a client entity as the bullet weapon 'owner'. This is not supported. Specify a different owner entity. |
| 4614 | MagicBullet(): Unhandled projectile weapClass. |
| 4615 | MagicBullet(): Unhandled weapType "%s". |
| 4616 | Unknown weapon name "%s" |
| 4617 | Unknown weapon specified for GetWeaponFlashTagname |
| 4618 | not a player entity |
| 4619 | not a player entity |
| 4620 | "%s" is not a valid weapon hud icon type to override |
| 4621 | SetEMPJammed: Entity must be a player entity |
| 4622 | Only script model/movers are allowed to be passed to this function. |
| 4623 | Reflection probe entities are not allowed to be passed to this function. |
| 4624 | Client weapon entities are not allowed to be passed to this function. |
| 4625 | Server weapon entities are not allowed to be passed to this function. |
| 4626 | Avatar entities are not allowed to be passed to this function. |
| 4627 | Only non-scriptable entities are allowed to be passed to this function. |
| 4628 | Wrong number of parameters to GetGlass( targetname ) |
| 4629 | GetGlass used with more than one piece of glass ("targetname" = "%s") |
| 4630 | Wrong number of parameters to GetGlassArray( targetname ) |
| 4631 | Wrong number of parameters to GetGlassOrigin( glassID ) |
| 4632 | Invalid glass ID %i |
| 4633 | Wrong number of parameters to IsGlassDestroyed( glassID ) |
| 4634 | Invalid glass ID %i |
| 4635 | Wrong number of parameters to DestroyGlass( glassID, direction ) |
| 4636 | Invalid glass ID %i |
| 4637 | Wrong number of parameters to DeleteGlass( glassID ) |
| 4638 | Invalid glass ID %i |
| 4639 | SetSlowMotion requires at least 1 parameter. |
| 4640 | LerpFOVByPreset is not supported in this gamemode |
| 4641 | LerpFOVByPreset must have only one parameter. |
| 4642 | Invalid preset name is passed into LerpFOVByPreset. |
| 4643 | Wrong number of parameters to GetEntChannelName( <index> ) |
| 4644 | Invalid channel index %i |
| 4645 | script_brushmodel must have DYNAMICPATH set to disconnect/connect paths |
| 4646 | entity of type '%s' cannot disconnect/connect paths. |
| 4647 | DisconnectNodePair() functionality not currently supported. Similar functionality will be provided for links in the near future. |
| 4648 | ConnectNodePair() functionality not currently supported. Similar functionality will be provided for links in the near future. |
| 4649 | Incorrect number of parameters |
| 4650 | badplace_delete called with name "" |
| 4651 | BadPlace: Unable to allocate repulsor. Exceeded max (%d)? |
| 4652 | Global bad place system must be active to use this function |
| 4653 | Incorrect BadPlace_Global() call. |
| 4654 | Error creating BadPlace, see log for details |
| 4655 | element %u of array: type %s is not an entity |
| 4656 | Failed to find script string for exploder name '%s' |
| 4657 | ActivateClientExploder is not supported in this game mode |
| 4658 | Incorrect number of parameters: GScr_Obituary( victim, attacker, weapon, weapon, <optional_obitPlayers> ) |
| 4659 | element %i of array: type %s is not an entity |
| 4660 | entity %i is not a player |
| 4661 | StopClientExploder is not supported in this game mode |
| 4662 | Incorrect number of parameters: StopClientExploder( exploder_num, <optional_player>, <optional_kill> ) |
| 4663 | entity %i is not a player |
| 4664 | Wrong number of parameters to TrajectoryCalculateInitialVelocity()! |
| 4665 | Wrong number of parameters to TrajectoryCalculateMinimumVelocity()! |
| 4666 | Wrong number of parameters to TrajectoryCalculateExitAngle()! |
| 4667 | Wrong number of parameters to TrajectoryEstimateDesiredInAirTime()! |
| 4668 | Wrong number of parameters to TrajectoryComputeDeltaHeightAtTime()! |
| 4669 | Wrong number of parameters to TrajectoryCanAttemptAccurateJump()! |
| 4670 | illegal call to drawSoundShape() |
| 4671 | sendScriptUsageAnalysisData: Must have two int parameters. First indicates report to log, the second to black box server. |
| 4672 | GetEnt() requires two arguments (name, key) |
| 4673 | GetEnt() key must be a generic entity key |
| 4674 | Requires both an origin and a radius if one or the other is provided |
| 4675 | key '%s' does not internally belong to entities |
| 4676 | key '%s' does not internally belong to generic entities |
| 4677 | key '%s' does not internally belong to entities |
| 4678 | key '%s' does not internally belong to generic entities |
| 4679 | key '%s' does not internally belong to scriptables |
| 4680 | Requires both an origin and a radius if one or the other is provided |
| 4681 | key '%s' does not internally belong to scriptables |
| 4682 | Failed to spawn standalone instance. See log for details. |
| 4683 | Must be called on objects of class 'Scriptable' |
| 4684 | Must be called on a standalone dynamic scriptable |
| 4685 | Must be called on a standalone (non-entity) scriptable |
| 4686 | Failed to free standalone instance. See log for details. |
| 4687 | '%s' - Invalid CSpline Id '%i' |
| 4688 | '%s' - Invalid CSpline Node Index '%i' for Spline '%i' |
| 4689 | '%s' - Invalid CSpline Lambda '%f' for Index '%i' for Spline '%i' |
| 4690 | GetCSplineId was unable to find targetname: %s |
| 4691 | GetCSplineId found multiple splines with the same targetname: %s |
| 4692 | USAGE: GetCSplinePointCount(splineId) |
| 4693 | USAGE: GetCSplineLength(splineId) |
| 4694 | USAGE: GetCSplineTargetname(splineId) |
| 4695 | USAGE: GetCSplinePointId(splineId, splineNodeIndex) |
| 4696 | USAGE: GetCSplinePointLabel(splineId, splineNodeIndex) |
| 4697 | USAGE: GetCSplinePointString(splineId, splineNodeIndex) |
| 4698 | USAGE: GetCSplinePointTargetname(splineId, splineNodeIndex) |
| 4699 | USAGE: GetCSplinePointTarget(splineId, splineNodeIndex) |
| 4700 | USAGE: GetCSplinePointTension(splineId, splineNodeIndex) |
| 4701 | USAGE: GetCSplinePointPosition(splineId, splineNodeIndex) |
| 4702 | USAGE: GetCSplinePointCorridorDims(splineId, splineNodeIndex) |
| 4703 | USAGE: GetCSplinePointTangent(splineId, splineNodeIndex) |
| 4704 | USAGE: GetCSplinePointDistToNextPoint(splineId, splineNodeIndex) |
| 4705 | USAGE: CalcCSplinePosition(splineId, splineNodeIndex, lambda) |
| 4706 | USAGE: CalcCSplineTangent(splineId, splineNodeIndex, lambda) |
| 4707 | USAGE: CalcCSplineCorridor(splineId, splineNodeIndex, lambda) |
| 4708 | USAGE: CalcCSplineClosestPoint(splineId, point) |
| 4709 | USAGE: ProhibitRecording( prohibit ) |
| 4710 | USAGE: StopRecording( discard ) |
| 4711 | USAGE: SetVideoMetadata( title, description, comment, copyright ) |
| 4712 | USAGE: ProhibitScreenshots( prohibit ) |
| 4713 | USAGE: SetScreenshotMetadata( game, title, comment ) |
| 4718 | SetMusicState: Incorrect number of parameters |
| 4719 | entity %i is not a player or player-controlled agent |
| 4720 | SetPlayerMusicState: Incorrect number of parameters |
| 4721 | SetAudioTriggerState: Incorrect number of parameters |
| 4722 | SetAudioZoneState: Bad state id name: %s |
| 4723 | SetAudioZoneState: Bad state name: %s |
| 4724 | DisableAudioTrigger: Incorrect number of parameters |
| 4725 | Enable/DisableAudioTrigger: Bad state id name: %s |
| 4726 | SetGlobalSoundContext: Incorrect number of parameters |
| 4727 | Enable/DisablePASpeaker: Speaker %s not found |
| 4728 | entity %i is not a player or player-controlled agent |
| 4729 | EnablePlayerBreathSystem: Incorrect number of parameters |
| 4730 | EnablePASpeaker: Incorrect number of parameters |
| 4731 | DisablePASpeaker: Incorrect number of parameters |
| 4732 | SetDead: Incorrect number of parameters. |
| 4733 | SetDead: You cannot use this method on players, agents and corpses. |
| 4734 | ArchetypeAssetExists called with incorrect number of parameters. |
| 4735 | GScr_ArchetypeGetAlias called with incorrect number of parameters. |
| 4736 | GScr_ArchetypeGetRandomAlias called with incorrect number of parameters. |
| 4737 | insufficient space in array for all the indices for this alias |
| 4738 | ArchetypeGetAliases called with incorrect number of parameters. |
| 4739 | GScr_ArchetypeHasState called with incorrect number of parameters. |
| 4740 | SetUmbraPortalState() called with incorrect number of parameters. Usage: SetUmbraPortalState( <gate name>, <0/1> ) |
| 4741 | FrontEndSceneCameraChange() called with incorrect number of parameters. |
| 4742 | FrontEndSceneFovChange() called with incorrect number of parameters. |
| 4743 | FrontEndSceneCameraRequiredCharacter() called with too many parameters. |
| 4744 | FrontEndSceneCameraRequiredCharacter() CharacterId must a valid client index. |
| 4745 | Expected parameter <bink name> |
| 4746 | Shouldn't specify a looping parameter when the bink name is blank |
| 4747 | GetUnarchivedDebugDvar( <dvar>, <default> ) takes either one or two parameters |
| 4748 | unable to find cloth type '%s' |
| 4749 | NearNode must be called on an AI |
| 4750 | entity %d must be a sentient! |
| 4751 | USAGE: self SetSurfaceType( <surface type name> ) |
| 4752 | SetSurfaceType is only valid on players, agents, or actors. |
| 4753 | unable to find surface type '%s' |
| 4754 | SetScriptableDamageOwner() must be called on a Scriptable |
| 4755 | SetScriptableDamageOwner() must be called on a Scriptable |
| 4756 | self entity must have an attached scriptable |
| 4757 | self not an ENTITY_CLASS_GENTITY or ENTITY_CLASS_SCRIPTABLE |
| 4758 | GetScriptableHasPart() missing partName |
| 4759 | GetScriptablePartHasState() missing partName |
| 4760 | GetScriptablePartHasState() missing stateName |
| 4761 | GetScriptablePartStateHasEvent() missing partName |
| 4762 | GetScriptablePartStateHasEvent() missing stateName |
| 4764 | GetScriptablePartStateHasEvent() unsupported event '%s' |
| 4765 | GetScriptablePartStateHasEvent() could not find part '%s' |
| 4766 | GetScriptablePartStateHasEvent() could not find state '%s' in part '%s' |
| 4767 | GetScriptablePartStateField() missing partName |
| 4768 | GetScriptablePartStateField() missing stateName |
| 4769 | GetScriptablePartStateField() missing fieldName |
| 4770 | GetScriptablePartStateField() could not find part '%s' |
| 4771 | GetScriptablePartStateField() could not find state '%s' in part '%s' |
| 4772 | GetScriptablePartStateField() unsupported field '%s' for state '%s' in part '%s' |
| 4773 | GetScriptablePartStateEventField() missing partName |
| 4774 | GetScriptablePartStateEventField() missing stateName |
| 4775 | GetScriptablePartStateEventField() missing eventName |
| 4776 | GetScriptablePartStateEventField() missing fieldName |
| 4777 | GetScriptablePartStateEventField() could not find part '%s' |
| 4778 | GetScriptablePartStateEventField() could not find state '%s' in part '%s' |
| 4779 | GetScriptablePartStateEventField() unsupported event '%s' |
| 4780 | GetScriptablePartStateEventField() could not find event '%s' in state '%s' in part '%s' |
| 4781 | GetScriptablePartStateEventField() unsupported field '%s' for event '%s' in state '%s' in part '%s' |
| 4782 | SetScriptablePartState() missing partName |
| 4783 | SetScriptablePartState() missing stateName |
| 4784 | SetScriptablePartState() could not set part '%s' to state '%s' |
| 4785 | SetScriptablePartZeroState_Hack() missing stateName |
| 4787 | SetScriptablePartZeroState_Hack() could not get def for scriptable instance %d |
| 4788 | SetScriptablePartZeroState_Hack() could not get part def 0 for scriptable instance %d |
| 4789 | SetScriptablePartZeroState_Hack() could not get state def for part 0 of scriptable instance %d |
| 4790 | SetScriptablePartZeroState_Hack() could not set part 0 to state '%s' for scriptable instance %d |
| 4791 | GetScriptablePartState() missing partName |
| 4792 | GetScriptablePartState() could not get the state name for part %s |
| 4793 | GetScriptablePartState() got part %s, but it had no Script Id set - check the scriptable |
| 4795 | Part name cannot be empty |
| 4796 | Failed to get part def for name '%s' |
| 4797 | StopSliding is only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4798 | USAGE: <player> StopSliding() |
| 4799 | SetEntitySoundContext: Incorrect number of parameters |
| 4800 | entity %i is not a player or player-controlled agent |
| 4801 | Incorrect number of parameters |
| 4802 | entity %i is not a player or player-controlled agent |
| 4803 | Incorrect number of parameters |
| 4804 | Invalid client trigger override priority level: %s |
| 4805 | entity %i is not a player or player-controlled agent |
| 4806 | Incorrect number of parameters |
| 4807 | SetClientTriggerAudioZonePartial: bad parameter: '%s' |
| 4808 | entity %i is not a player or player-controlled agent |
| 4809 | Invalid client trigger override priority level: %s |
| 4810 | Invalid client trigger override priority level: %s |
| 4811 | Incorrect number of parameters |
| 4812 | entity %i is not a player or player-controlled agent |
| 4813 | Incorrect number of parameters |
| 4814 | entity %i is not a player or player-controlled agent |
| 4815 | Invalid client trigger override priority level: %s |
| 4818 | SetSoundSubmix: Incorrect number of parameters - expect: duckname [,fadeTime [,scale]] |
| 4819 | SetSoundSubmix: bad duck name: '%s' |
| 4820 | ScaleSoundSubmix: Incorrect number of parameters - expect: submixname, scale |
| 4821 | ScaleSoundSubmix: bad submix name: '%s' |
| 4822 | ClearSoundSubmix: Incorrect number of parameters - expect: duckname [, fadeTime] |
| 4823 | ClearSoundSubmix: bad duck name: '%s' |
| 4824 | ClearAllSoundSubmixes: Incorrect number of parameters - expect: none or optional [fadeTime] |
| 4825 | SetChargeMeleeHudVisible() must be called on a valid entity |
| 4826 | SetChargeMeleeHudVisible() must be called on a player or agent |
| 4827 | getDebugEye must be called on an AI or player, not on a '%s' |
| 4828 | GetPlayerLightLevel not called on player character. |
| 4829 | GetPlayerLightLevel cannot be called on a player bot. |
| 4830 | GetPlayerLightLevel only supports %i players (requested for %i) |
| 4831 | Incorrect number of parameters. |
| 4832 | Motion warp duration (%i ms) must be at least one frame (%i ms). |
| 4833 | Max tracked entities exceeded. |
| 4834 | Incorrect number of parameters. |
| 4835 | Motion warp duration (%i ms) must be at least one frame (%i ms). |
| 4836 | Max tracked entities exceeded. |
| 4837 | entity type '%s' is not a turret |
| 4838 | Incorrect number of parameters. |
| 4844 | TriggerEnable was not called on an entity. |
| 4845 | EnableTriggerForPlayer was not called on an entity of trigger type. |
| 4846 | TriggerDisable was not called on an entity. |
| 4847 | TriggerDisable was not called on an entity of trigger type. |
| 4848 | IsTriggerEnabled was not called on an entity. |
| 4849 | IsTriggerEnabled was not called on an entity of trigger type. |
| 4850 | Skydive_BeginFreefall(). Only valid on players; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4851 | Skydive_DeployParachute(). Only valid on players; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4852 | Skydive_SetDeploymentStatus(). Only valid on players; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 4853 | Missing parameter: player Skydive_SetDeploymentStatus( <enabled> ); |
| 4854 | entity %i is not a script_brushmodel or script_model or scriptable or dropped item (which are the only script entities supported by this physics function) |
| 4855 | not an entity |
| 4856 | entity %i is not a script_brushmodel or script_model or scriptable or dropped item or vehicle (which are the only script entities supported by this physics function) |
| 4857 | not an entity |
| 4858 | entity %i is not a player or AI (which are the only script entities supported by this physics function) |
| 4859 | not an entity |
| 4860 | entity %i is not a Physics Volume (which is the entity type supported by this physics function) |
| 4861 | not an entity |
| 4862 | Physics_GetBodyId: No physics object found |
| 4863 | Physics_GetBodyId: No physics instance found |
| 4864 | Physics_GetBodyId: Index out of range. There are %d bodies in the physics asset. |
| 4865 | Physics_GetBodyId: No body found |
| 4866 | Physics_GetPlayerGroundLinVel: Not called on a player |
| 4867 | Physics_GetPlayerGroundLinVel: Should be on a moving platform and we aren't |
| 4868 | Physics_GetPlayerGroundLinVel: Moving platform couldn't be found |
| 4869 | Physics_GetPlayerGroundLinVel: Moving platform has no physics object |
| 4870 | Physics_GetPlayerGroundLinVel: Currently, we only support moving platforms with 1 serverside body |
| 4871 | Physics_GetBodyId: No body found |
| 4872 | Physics_CreateInstance: An object with physics must either be granted physics control or be server authoritative |
| 4873 | Physics_CreateInstance: Entity is not a script mover |
| 4874 | Physics_CreateInstance: No physics object found |
| 4875 | Physics_CreateInstance: Physics already instantiated |
| 4876 | Physics_CreateInstance: Entity has no dobj |
| 4877 | Physics_CreateInstance: Entity has no xmodel |
| 4878 | Physics_CreateInstance: Entity has no physics asset |
| 4879 | Physics_CreateInstance: Physics failed to create |
| 4880 | Physics_TakeControl: If you set position, you must set impulse too |
| 4881 | Physics_TakeControl: Entity is not a script mover |
| 4882 | Physics_TakeControl: No physics object found |
| 4883 | Physics_TakeControl: No physics instance found - physics probably hasn't been instantiated |
| 4884 | Physics_TakeControl: No physics bodies in instance |
| 4885 | Physics_TakeControl: Entity has no dobj |
| 4886 | Physics_TakeControl: Entity has no xmodel |
| 4887 | Physics_TakeControl: Entity has no physics asset |
| 4888 | Physics_TakeControl: Invalid rigid body found in instance |
| 4889 | Physics_ApplyImpulse: Entity is not a script mover nor vehicle |
| 4890 | Physics_ApplyImpulse: No physics object found |
| 4891 | Physics_ApplyImpulse: No physics instance found - physics probably hasn't been instantiated |
| 4892 | Physics_ApplyImpulse: No physics bodies in instance |
| 4893 | Physics_ApplyImpulse: Invalid rigid body found in instance |
| 4894 | PhysicsScript_GetCharacterCollisionCapsule: Entity doesn't appear to be a player, actor, agent or fakeactor |
| 4895 | PhysicsScript_RegisterForCollisionCallback entity %i using model %s at position (%.2f,%.2f,%.2f) doesn't have any physics to register a callback for. This is commonly caused by missing physAsset or a transient model. |
| 4896 | Physics_VolumeEnable should have 1 param - Physics_VolumeEnable( <enable> ) |
| 4897 | Physics_VolumeCreate: Parameter 0 enable must be the enable flag - see usage |
| 4898 | Physics_VolumeAffectCharacters should have 1 param - Physics_VolumeAffectCharacters( <enable> ) |
| 4899 | Physics_VolumeAffectCharacters: Parameter 0 enable must be the enable flag - see usage |
| 4900 | Physics_VolumeAffectMissiles should have 1 param - Physics_VolumeAffectMissiles( <enable> ) |
| 4901 | Physics_VolumeAffectMissiles: Parameter 0 enable must be the enable flag - see usage |
| 4902 | Physics_VolumeSetActivator should have 1 param - Physics_VolumeSetActivator( <enable> ) |
| 4903 | Physics_VolumeSetActivator: Parameter 0 enable must be the enable flag - see usage |
| 4904 | Physics_VolumeSetAsGravityScalar should have 2 params - Physics_VolumeSetAsGravityScalar( <enable>, <scale> ) |
| 4905 | Physics_VolumeSetAsGravityScalar: Parameter 0 enable must be the enable flag - see usage |
| 4906 | Physics_VolumeSetAsGravityScalar: Parameter 1 scale must be the scale - see usage |
| 4907 | Physics_VolumeSetAsGravityScalar: Entity is a gravity vector - you can't also make it a gravity scalar |
| 4908 | Physics_VolumeSetAsGravityScalar: Entity is a direction force applier - you can't also make it a gravity scalar |
| 4909 | Physics_VolumeSetAsGravityScalar: Entity is a focal force applier - you can't also make it a gravity scalar |
| 4910 | Physics_VolumeSetAsGravityVector should have 2 params - Physics_VolumeSetAsGravityVector( <enable>, <vector> ) |
| 4911 | Physics_VolumeSetAsGravityVector: Parameter 0 enable must be the enable flag - see usage |
| 4912 | Physics_VolumeSetAsGravityVector: Parameter 1 gravity must be the direction - see usage |
| 4913 | Physics_VolumeSetAsGravityVector: Entity is a gravity scalar - you can't also make it a directional force applier |
| 4914 | Physics_VolumeSetAsGravityVector: Entity is a direction force applier - you can't also make it a gravity scalar |
| 4915 | Physics_VolumeSetAsGravityVector: Entity is a focal force applier - you can't also make it a gravity scalar |
| 4916 | Physics_VolumeSetAsDirectionalForce should have 3 or 4 params - Physics_VolumeSetAsDirectionalForce( <enable>, <direction>, <force>, <maxspeed> ) |
| 4917 | Physics_VolumeSetAsDirectionalForce: Parameter 0 enable must be the enable flag - see usage |
| 4918 | Physics_VolumeSetAsDirectionalForce: Parameter 1 direction must be the direction - see usage |
| 4919 | Physics_VolumeSetAsDirectionalForce: Parameter 2 force must be the force - see usage |
| 4920 | Physics_VolumeSetAsDirectionalForce: Parameter 3 maxspeed must be the maximum speed - see usage |
| 4921 | Physics_VolumeSetAsDirectionalForce: Entity is a gravity scalar - you can't also make it a directional force applier |
| 4922 | Physics_VolumeSetAsDirectionalForce: Entity is a gravity vector - you can't also make it a directional force applier |
| 4923 | Physics_VolumeSetAsDirectionalForce: Entity is a focal force applier - you can't also make it a directional force applier |
| 4924 | Physics_VolumeSetAsFocalForce should have 3 or 4 params - Physics_VolumeSetAsFocalForce( <enable>, <point>, <force>, <maxspeed> ) |
| 4925 | Physics_VolumeSetAsFocalForce: Parameter 0 enable must be the enable flag - see usage |
| 4926 | Physics_VolumeSetAsFocalForce: Parameter 1 point must be the point - see usage |
| 4927 | Physics_VolumeSetAsFocalForce: Parameter 2 force must be the force - see usage |
| 4928 | Physics_VolumeSetAsDirectionalForce: Parameter 3 maxspeed must be the maximum speed - see usage |
| 4929 | Physics_VolumeSetAsFocalForce: Entity is a gravity scalar - you can't also make it a focal force applier |
| 4930 | Physics_VolumeSetAsFocalForce: Entity is a gravity vector - you can't also make it a focal force applier |
| 4931 | Physics_VolumeSetAsFocalForce: Entity is a direction force applier - you can't also make it a focal force applier |
| 4932 | Physics_GetBodyLinVel: Invalid Body Id |
| 4933 | Physics_GetBodyAngVel: Invalid Body Id |
| 4934 | Physics_GetBodyLinAngVel: Invalid Body Id |
| 4935 | Physics_CreateContents: Parameter 0 must be an array of strings |
| 4936 | Physics_CreateContents: Parameter 0 is an array but at least one entry is not a string |
| 4937 | Physics_CreateContents: Parameter 0 is an array but entry %i %s is not a contents string |
| 4938 | Physics_AABBBroadphaseQuery: Parameter 0 aabbMin must be the aabb min vector - see usage |
| 4939 | Physics_AABBBroadphaseQuery: Parameter 1 aabbMax must be the aabb max vector - see usage |
| 4940 | Physics_AABBBroadphaseQuery: Parameter 2 contents must be the contents - see usage |
| 4941 | Physics_AABBBroadphaseQuery: Parameter 2 contents must be non 0 |
| 4942 | Physics_AABBBroadphaseQuery: Parameter 3 ignore array is not invalid, an entity or an array |
| 4943 | Physics_AABBBroadphaseQuery: ignore array is too large (%d > %d) |
| 4944 | Physics_AABBBroadphaseQuery: element %i of ignore array: type %s is not an entity |
| 4945 | Physics_AABBQuery: Parameter 0 aabbMin must be the aabb min vector - see usage |
| 4946 | Physics_AABBQuery: Parameter 1 aabbMax must be the aabb max vector - see usage |
| 4947 | Physics_AABBQuery: Parameter 2 contents must be the contents - see usage |
| 4948 | Physics_AABBQuery: Parameter 2 contents must be non 0 |
| 4949 | Physics_AABBQuery: Parameter 3 ignore array is not invalid, an entity or an array |
| 4950 | Physics_AABBQuery: ignore array is too large (%d > %d) |
| 4951 | Physics_AABBQuery: element %i of ignore array: type %s is not an entity |
| 4952 | Physics_AABBQuery: Parameter 4 collectionType must be the collection type - see usage |
| 4954 | Physics_AABBQuery: Parameter 4 collectionType is closest - there isn't a real concept of closest for aabb overlapping |
| 4955 | Physics_Raycast: Parameter 0 start must be the start vector - see usage |
| 4956 | Physics_Raycast: Parameter 1 end must be the end vector - see usage |
| 4957 | Physics_Raycast: Parameter 2 contents must be the contents - see usage |
| 4958 | Physics_Raycast: Parameter 2 contents must be non 0 |
| 4959 | Physics_Raycast: Parameter 3 ignore array is not invalid, an entity or an array |
| 4960 | Physics_Raycast: ignore array is too large (%d > %d) |
| 4961 | Physics_Raycast: element %i of ignore array: type %s is not an entity |
| 4962 | Physics_Raycast: Parameter 4 detail must be the detail flag - see usage |
| 4963 | Physics_Raycast: Parameter 5 collectionType must be the collection type - see usage |
| 4965 | Physics_Raycast: Parameter 6 'ignoreClutter', if provided, must be a flag indicating whether to ignore entities marked as clutter - see usage |
| 4966 | Physics_RaycastEnts: Parameter 0 start must be the start vector - see usage |
| 4967 | Physics_RaycastEnts: Parameter 1 end must be the end vector - see usage |
| 4968 | Physics_RaycastEnts: Parameter 2 contents must be the contents - see usage |
| 4969 | Physics_RaycastEnts: Parameter 2 contents must be non 0 |
| 4970 | Physics_RaycastEnts: Parameter 3 must contain some entities |
| 4971 | Physics_RaycastEnts: Parameter 3 entities array is not invalid, an entity or an array |
| 4972 | Physics_RaycastEnts: entity array is too large (%d > %d) |
| 4973 | Physics_RaycastEnts: element %i of entities array: type %s is not an entity |
| 4974 | Physics_RaycastEnts: Parameter 4 detail must be the detail flag - see usage |
| 4975 | Physics_RaycastEnts: Parameter 5 collectionType must be the collection type - see usage |
| 4977 | Physics_RaycastEnts: Entity %i does not have physics |
| 4978 | Physics_RaycastEnts: Entity %i has invalid rigid body %i(%i) |
| 4979 | Physics_RaycastEnts: Entity %i does not have physics |
| 4980 | Physics_RaycastEnts: Entity %i has invalid rigid body %i(%i) |
| 4981 | Physics_Spherecast: Parameter 0 start must be the start vector - see usage |
| 4982 | Physics_Spherecast: Parameter 1 end must be the end vector - see usage |
| 4983 | Physics_Spherecast: Parameter 2 radius must be the radius - see usage |
| 4984 | Physics_Spherecast: Parameter 3 contents must be the contents - see usage |
| 4985 | Physics_Spherecast: Parameter 3 contents must be non 0 |
| 4986 | Physics_Spherecast: Parameter 4 ignore array is not invalid, an entity or an array |
| 4987 | Physics_Spherecast: ignore array is too large (%d > %d) |
| 4988 | Physics_Spherecast: element %i of ignore array: type %s is not an entity |
| 4989 | Physics_Spherecast: Parameter 5 collectionType must be the collection type - see usage |
| 4991 | Physics_Spherecast: Parameter 6 startCollectionType must be the startCollectionType - see usage |
| 4993 | PhysicsScript_Capsulecast: Parameter 0 start must be the start vector - see usage |
| 4994 | PhysicsScript_Capsulecast: Parameter 1 end must be the end vector - see usage |
| 4995 | PhysicsScript_Capsulecast: Parameter 2 radius must be the radius - see usage |
| 4996 | PhysicsScript_Capsulecast: Parameter 3 halfheight must be the half height of the capsule - see usage |
| 4997 | PhysicsScript_Capsulecast: Parameter 3 halfheight must be the at least the size of the radius |
| 4998 | PhysicsScript_Capsulecast: Parameter 4 angles must be the angles vector - see usage |
| 4999 | PhysicsScript_Capsulecast: Parameter 5 contents must be the contents - see usage |
| 5000 | PhysicsScript_Capsulecast: Parameter 5 contents must be non 0 |
| 5001 | PhysicsScript_Capsulecast: Parameter 6 ignore array is not invalid, an entity or an array |
| 5002 | PhysicsScript_Capsulecast: ignore array is too large (%d > %d) |
| 5003 | PhysicsScript_Capsulecast: element %i of ignore array: type %s is not an entity |
| 5004 | PhysicsScript_Capsulecast: Parameter 7 collectionType must be the collection type - see usage |
| 5006 | PhysicsScript_Capsulecast: Parameter 8 startCollectionType must be the startCollectionType - see usage |
| 5008 | PhysicsScript_Charactercast: Parameter 0 start must be the start vector - see usage |
| 5009 | PhysicsScript_Charactercast: Parameter 1 end must be the end vector - see usage |
| 5010 | PhysicsScript_Charactercast: Parameter 2 character must be an entity - see usage |
| 5011 | PhysicsScript_Charactercast: Parameter 2 character must be a valid entity - see usage |
| 5012 | PhysicsScript_Charactercast: Parameter 2 character must be a player or ai entity - see usage |
| 5013 | PhysicsScript_Charactercast: Parameter 3 ground clearance must be the clearance distance - see usage |
| 5014 | PhysicsScript_Charactercast: Parameter 3 ground clearance is too high - the capsule has collapsed |
| 5015 | PhysicsScript_Charactercast: Parameter 4 angles must be the angles vector - see usage |
| 5016 | PhysicsScript_Charactercast: Parameter 5 contents must be the contents - see usage |
| 5017 | PhysicsScript_Charactercast: Parameter 5 contents must be non 0 |
| 5018 | PhysicsScript_Charactercast: Parameter 6 ignore array is not invalid, an entity or an array |
| 5019 | PhysicsScript_Charactercast: ignore array is too large (%d > %d) |
| 5020 | PhysicsScript_Charactercast: element %i of ignore array: type %s is not an entity |
| 5021 | PhysicsScript_Charactercast: Parameter 7 collectionType must be the collection type - see usage |
| 5023 | PhysicsScript_Charactercast: Parameter 8 startCollectionType must be the startCollectionType - see usage |
| 5025 | PhysicsScript_Shapecast: Parameter 0 start must be the start vector - see usage |
| 5026 | PhysicsScript_Shapecast: Parameter 1 end must be the end vector - see usage |
| 5027 | PhysicsScript_Shapecast: Parameter 2 entity must be the entity - see usage |
| 5028 | PhysicsScript_Shapecast: Parameter 2 entity could not be found |
| 5029 | PhysicsScript_Shapecast: Parameter 2 entity didn't have physics |
| 5030 | PhysicsScript_Shapecast: Parameter 2 entity didn't have physics |
| 5031 | PhysicsScript_Shapecast: Parameter 2 entity physics must have only 1 rigid body |
| 5032 | PhysicsScript_Shapecast: Parameter 2 entity physics rigid body is invalid |
| 5033 | PhysicsScript_Shapecast: Parameter 2 entity physics rigid body has no shape |
| 5034 | PhysicsScript_Capsulecast: Parameter 3 angles must be the angles vector - see usage |
| 5035 | PhysicsScript_Shapecast: Parameter 4 contents must be the contents - see usage |
| 5036 | PhysicsScript_Shapecast: Parameter 4 contents must be non 0 |
| 5037 | PhysicsScript_Shapecast: Parameter 5 ignore array is not invalid, an entity or an array |
| 5038 | PhysicsScript_Shapecast: ignore array is too large (%d > %d) |
| 5039 | PhysicsScript_Shapecast: element %i of ignore array: type %s is not an entity |
| 5040 | PhysicsScript_Shapecast: Parameter 6 collectionType must be the collection type - see usage |
| 5042 | PhysicsScript_Shapecast: Parameter 6 startCollectionType must be the startCollectionType - see usage |
| 5044 | Physics_QueryPoint: Parameter 0 position must be the position vector - see usage |
| 5045 | Physics_QueryPoint: Parameter 1 distance must be the distance - see usage |
| 5046 | Physics_QueryPoint: Parameter 2 contents must be the contents - see usage |
| 5047 | Physics_QueryPoint: Parameter 2 contents must be non 0 |
| 5048 | Physics_QueryPoint: Parameter 3 ignore array is not invalid, an entity or an array |
| 5049 | Physics_QueryPoint: ignore array is too large (%d > %d) |
| 5050 | Physics_QueryPoint: element %i of ignore array: type %s is not an entity |
| 5051 | Physics_QueryPoint: Parameter 4 collectionType must be the collection type - see usage |
| 5053 | Physics_GetClosestPointToSphere: Parameter 0 position must be the position vector - see usage |
| 5054 | Physics_GetClosestPointToSphere: Parameter 1 radius must be the radius - see usage |
| 5055 | Physics_GetClosestPointToSphere: Parameter 2 distance must be the distance - see usage |
| 5056 | Physics_GetClosestPointToSphere: Parameter 3 contents must be the contents - see usage |
| 5057 | Physics_GetClosestPointToSphere: Parameter 3 contents must be non 0 |
| 5058 | Physics_GetClosestPointToSphere: Parameter 4 ignore array is not invalid, an entity or an array |
| 5059 | Physics_GetClosestPointToSphere: ignore array is too large (%d > %d) |
| 5060 | Physics_GetClosestPointToSphere: element %i of ignore array: type %s is not an entity |
| 5061 | Physics_GetClosestPointToSphere: Parameter 5 collectionType must be the collection type - see usage |
| 5063 | Physics_GetClosestPointToCapsule: Parameter 0 position must be the position vector - see usage |
| 5064 | Physics_GetClosestPointToCapsule: Parameter 1 radius must be the radius - see usage |
| 5065 | Physics_GetClosestPointToCapsule: Parameter 2 halfheight must be the half height of the capsule - see usage |
| 5066 | Physics_GetClosestPointToCapsule: Parameter 2 halfheight must be the at least the size of the radius |
| 5067 | Physics_GetClosestPointToCapsule: Parameter 3 angles must be the angles vector - see usage |
| 5068 | Physics_GetClosestPointToCapsule: Parameter 4 distance must be the distance - see usage |
| 5069 | Physics_GetClosestPointToCapsule: Parameter 5 contents must be the contents - see usage |
| 5070 | Physics_GetClosestPointToCapsule: Parameter 5 contents must be non 0 |
| 5071 | Physics_GetClosestPointToCapsule: Parameter 6 ignore array is not invalid, an entity or an array |
| 5072 | Physics_GetClosestPointToCapsule: ignore array is too large (%d > %d) |
| 5073 | Physics_GetClosestPointToCapsule: element %i of ignore array: type %s is not an entity |
| 5074 | Physics_GetClosestPointToCapsule: Parameter 7 collectionType must be the collection type - see usage |
| 5076 | Physics_GetClosestPointToCharacter: Parameter 0 position must be the position vector - see usage |
| 5077 | Physics_GetClosestPointToCharacter: Parameter 1 character must be an entity - see usage |
| 5078 | Physics_GetClosestPointToCharacter: Parameter 1 character must be a valid entity - see usage |
| 5079 | Physics_GetClosestPointToCharacter(): Suit def not found! |
| 5080 | Physics_GetClosestPointToCharacter(): Unsupported stand %s |
| 5081 | Physics_GetClosestPointToCharacter: Parameter 1 character must be a player or ai entity - see usage |
| 5082 | Physics_GetClosestPointToCharacter: Parameter 2 ground clearance must be the clearance distance - see usage |
| 5083 | Physics_GetClosestPointToCharacter: Parameter 2 ground clearance is too high - the capsule has collapsed |
| 5084 | Physics_GetClosestPointToCharacter: Parameter 3 angles must be the angles vector - see usage |
| 5085 | Physics_GetClosestPointToCharacter: Parameter 4 distance must be the distance - see usage |
| 5086 | Physics_GetClosestPointToCharacter: Parameter 5 contents must be the contents - see usage |
| 5087 | Physics_GetClosestPointToCharacter: Parameter 5 contents must be non 0 |
| 5088 | Physics_GetClosestPointToCharacter: Parameter 6 ignore array is not invalid, an entity or an array |
| 5089 | Physics_GetClosestPointToCharacter: ignore array is too large (%d > %d) |
| 5090 | Physics_GetClosestPointToCharacter: element %i of ignore array: type %s is not an entity |
| 5091 | Physics_GetClosestPointToCharacter: Parameter 7 collectionType must be the collection type - see usage |
| 5093 | Physics_GetClosestPoint: Parameter 0 position must be the position vector - see usage |
| 5094 | Physics_GetClosestPoint: Parameter 1 entity must be the entity - see usage |
| 5095 | Physics_GetClosestPoint: Parameter 1 entity could not be found |
| 5096 | Physics_GetClosestPoint: Parameter 1 entity didn't have physics |
| 5097 | Physics_GetClosestPoint: Parameter 1 entity didn't have physics |
| 5098 | Physics_GetClosestPoint: Parameter 1 entity physics must have only 1 rigid body |
| 5099 | Physics_GetClosestPoint: Parameter 1 entity physics rigid body is invalid |
| 5100 | Physics_GetClosestPoint: Parameter 1 entity physics rigid body has no shape |
| 5101 | Physics_GetClosestPoint: Parameter 2 angles must be the angles vector - see usage |
| 5102 | Physics_GetClosestPoint: Parameter 3 distance must be the distance - see usage |
| 5103 | Physics_GetClosestPoint: Parameter 4 contents must be the contents - see usage |
| 5104 | Physics_GetClosestPoint: Parameter 4 contents must be non 0 |
| 5105 | Physics_GetClosestPoint: Parameter 5 ignore array is not invalid, an entity or an array |
| 5106 | Physics_GetClosestPoint: ignore array is too large (%d > %d) |
| 5107 | Physics_GetClosestPoint: element %i of ignore array: type %s is not an entity |
| 5108 | Physics_GetClosestPoint: Parameter 6 collectionType must be the collection type - see usage |
| 5110 | physics_getsurfacetypefromflags: Parameter 0 must be an integer |
| 5111 | Physics_GetBodyMass: Invalid Body Id |
| 5112 | Physics_GetBodyDynamicMass: Invalid Body Id |
| 5113 | Physics_GetBodyCenterOfMass: Invalid Body Id |
| 5114 | Physics_SetBodyCenterOfMass: Invalid Body Id |
| 5115 | Physics_GetBodyAABB: Invalid Body Id |
| 5116 | Physics_VolumeCreate should have 2 or 3 params - Physics_VolumeCreate( <position>, <radius>, <height> ) |
| 5117 | Physics_VolumeCreate: Parameter 0 position must be the position - see usage |
| 5118 | Physics_VolumeCreate: Parameter 1 radius must be the radius - see usage |
| 5119 | Physics_VolumeCreate: Parameter 2 height must be the height - see usage |
| 5120 | Physics_SetBodyLinVel: Invalid parameter count - check usage |
| 5121 | Physics_SetBodyLinVel: Invalid Body Id |
| 5122 | Physics_SetBodyLinVel: Invalid Velocity |
| 5123 | Physics_SetBodyAngVel: Invalid parameter count - check usage |
| 5124 | Physics_SetBodyAngVel: Invalid Body Id |
| 5125 | Physics_SetBodyAngVel: Invalid Velocity |
| 5126 | Physics_SetBodyLinAngVel: Invalid parameter count - check usage |
| 5127 | Physics_SetBodyLinAngVel: Invalid Body Id |
| 5128 | Physics_SetBodyLinAngVel: Invalid Linear Velocity |
| 5129 | Physics_SetBodyLinAngVel: Invalid Angular Velocity |
| 5130 | Orient to is only for vehicles. |
| 5131 | This vehicle is not being used by a player |
| 5132 | entity %i references an animset that has not been loaded |
| 5133 | animation state %s does not exist in animset %s |
| 5134 | animation entry %d is not a part of the state %s in animset %s |
| 5135 | animation entry %s is not a part of the state %s in animset %s |
| 5136 | entry must be either int or string |
| 5137 | entity %i is not a player or agent |
| 5138 | not an entity |
| 5139 | %s is not a valid anim class |
| 5140 | Entity %d is not a using animation states. Use "agent EnableAnimState( true )" |
| 5141 | entity %i is not a player or agent |
| 5142 | not an entity |
| 5143 | Trying to enable animation state machine without a default class to assign. "default_anim" must be present. |
| 5144 | could not find the specified animation entry |
| 5145 | entity %i references an animset that has not been loaded |
| 5146 | animation state %s does not exist in animset %s |
| 5147 | Unable to find alias %s in state %s in animset %s |
| 5148 | not an entity |
| 5149 | entity %i references an animset that has not been loaded |
| 5150 | animation state %s does not exist in animset %s |
| 5151 | not an entity |
| 5152 | entity %i references an animset that has not been loaded |
| 5153 | state %s does not exist in animset %s |
| 5154 | animation entry %d is not part of state %s in animset %s |
| 5155 | animation entry %s is not part of state %s in animset %s |
| 5156 | malformed state %s in animset %s |
| 5157 | entry must be either int, string, or undefined (to select at random) |
| 5158 | entity %i is not a player or agent |
| 5159 | not an entity |
| 5160 | could not find the specified animation entry |
| 5161 | not currently supported with animsets |
| 5162 | could not find the specified animation entry |
| 5163 | entity %i references an animset that has not been loaded |
| 5164 | entity %i is not a player or agent |
| 5165 | not an entity |
| 5166 | %d attachment '%s' variation index %d is invalid. Model variation count: %d. |
| 5167 | Trying to set more than one attachment in slot %d on weapon |
| 5180 | Trying to set the same other attachment (%s) more than once on weapon |
| 5181 | Unknown attachment name '%s' specified for weapon '%s' |
| 5182 | Unknown attachment name '%s' specified for weapon '%s' |
| 5183 | Too many attachment indices specified (%d > %d) |
| 5184 | Attachment index specified is not an integer |
| 5185 | Too many attachments specified (%d > %d) |
| 5186 | Attachment name specified is not a string |
| 5187 | Different number of attachment names and indices specified (%d != %d) |
| 5188 | Weapon field %s is read-only |
| 5189 | Weapon field %s cannot be read |
| 5190 | Too many allocated script weapons. Limit is %d. See console log for details. |
| 5191 | Not a weapon object |
| 5192 | Not a weapon object |
| 5193 | Cannot set the attachments on null weapon |
| 5194 | Specified reticle %d on weapon '%s' that does not have a scope attachment |
| 5195 | Specified reticle %d is out of bounds for weapon '%s' with scope '%s'. Maximum allowed reticle index for scope '%s' is %d |
| 5196 | Specified camo '%s' not found for weapon '%s' |
| 5197 | Tried to set camo '%s' on a weapon %s that doesn't support camo. |
| 5198 | Invalid loot variant specified: %d does not index the supported %d variants for weapon '%s' |
| 5199 | Trying to create a weapon in alternate mode that has no underbarrel attachment with an alternate fire |
| 5200 | Invalid weapon parameter specified. Weapon must be a weapon object |
| 5201 | Invalid weapon parameter specified. Weapon must be a weapon object |
| 5202 | Invalid weapon parameter specified. Weapon must be a undefined, weapon object, or weapon string |
| 5203 | Cannot set the attachments on null weapon |
| 5204 | Trying to set underbarrel attachment without alt fire mode on a weapon in alt mode |
| 5205 | Unknown other attachment '%s' specified for weapon %s |
| 5206 | Other attachment '%s' cannot be removed from weapon %s. The attachment is not present |
| 5207 | Unknown other attachment '%s' specified for weapon %s |
| 5208 | Cannot set the camo on null weapon |
| 5209 | Invalid camo %s specified for weapon %s. Camo name does not exist. |
| 5210 | Tried to set camo %s on a weapon %s that doesn't support camo. |
| 5211 | Invalid camo specified for weapon %s. Camo must be specified using a string name |
| 5212 | Cannot set the reticle on null weapon |
| 5213 | Invalid reticle name '%s' specified for weapon %s |
| 5214 | Invalid reticle specified for weapon %s. Reticle must be specified using an integer or string of the format 'scope01' |
| 5215 | Specified reticle 'scope%d' is out of bounds for weapon %s. Maximum allowed reticle is %d |
| 5216 | Too many target marker groups are in use. |
| 5217 | Attempted to access an invalid target marker group slot. |
| 5218 | '%s' is not a valid target marker widget. Please make sure it is included in the correct ncsluistrings.txt file. |
| 5219 | Too many entities in this target marker group. |
| 5220 | Entity must be a player or a bot. |
| 5221 | unknown team '%s' |
| 5222 | Entity must be a player or a bot. |
| 5223 | unknown team '%s' |
| 5224 | Exceeded maximum number of script objects (parents): %d |
| 5225 | Exceeded maximum number of script variables (children): %d |
| 5226 | exceeded maximum number of array values for variable %d (see console log for more info) |
| 5227 | cannot set field of %s |
| 5228 | %s is not a field object |
| 5229 | size cannot be applied to %s |
| 5231 | ~ cannot be applied to "%s" |
| 5233 | cannot cast %s to bool |
| 5235 | type %s is not a float |
| 5236 | %s is not a field object |
| 5237 | pair '%s' and '%s' has unmatching types '%s' and '%s' |
| 5239 | Cannot cast %s to string |
| 5240 | Cannot cast %s to string |
| 5241 | cannot concat "%s" and "%s" - max string length exceeded |
| 5242 | divide by 0 |
| 5243 | divide by 0 |
| 5244 | divide by 0 |
| 5245 | divide by 0 |
| 5246 | divide by 0 |
| 5247 | array index %d out of range |
| 5248 | %s is not an array index |
| 5249 | array index %d out of range |
| 5250 | %s is not an array index |
| 5251 | string index %d out of range |
| 5252 | %s is not a string index |
| 5253 | vector index %d out of range |
| 5254 | %s is not a vector index |
| 5255 | %s is not an array |
| 5256 | %s is not an array, string, or vector |
| 5257 | read-only array cannot be changed |
| 5258 | string characters cannot be individually changed |
| 5259 | vector components cannot be individually changed |
| 5260 | %s is not an array |
| 5261 | %s is not an array |
| 5262 | read-only array cannot be changed |
| 5263 | %s is not an array |
| 5264 | %s is not an array |
| 5265 | array index %d out of range |
| 5266 | %s is not an array index |
| 5267 | not an array key |
| 5268 | not an array key |
| 5269 | Not an FXEntity |
| 5270 | Incorrect number of parameters |
| 5271 | Incorrect number of parameters |
| 5272 | Not valid to call this with looping fxents. |
| 5273 | Incorrect number of parameters |
| 5275 | Attempted to access an invalid head icon slot. |
| 5276 | '%s' is not a valid head icon image. Please make sure it is included as precache_headicon in zone_source. |
| 5277 | '%s' is not a valid head icon image. Please make sure it is included as precache_headicon in zone_source. |
| 5278 | '%s' is not a valid head icon image. Please make sure it is included as precache_headicon in zone_source. |
| 5279 | '%s' is not a valid head icon image. Please make sure it is included as precache_headicon in zone_source. |
| 5280 | Entity must be a player or a bot. |
| 5281 | unknown team '%s' |
| 5282 | SetHeadIconNaturalDistance( index, distance ) should be called with a distance < 32000 |
| 5283 | SetHeadIconMaxDistance( index, distance ) should be called with a distance < 32000 |
| 5284 | Entity must be a player or a bot. |
| 5285 | unknown team '%s' |
| 5286 | Entity must be a player or a bot. |
| 5287 | unknown team '%s' |
| 5288 | Creating a 3D Navigator on entity '%s' at location %s outside of all Nav Volumes |
| 5290 | GetLobbySquadIndex(). Only valid on players; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 5291 | Illegal call to BotNodeAvailableToTeam(). A path node and team is required. |
| 5292 | Illegal call to BotNodeAvailableToTeam(). Only supporting 'axis' and 'allies' right now |
| 5293 | Illegal call to BotNodeAvailableToTeam(). Only supporting 'axis' and 'allies' right now |
| 5294 | IsNodeOccupied needs to be modified to work with node team ownership |
| 5295 | illegal call to GetNodeOwner() |
| 5296 | GetNodeOwner needs to be modified to work with node team ownership |
| 5297 | Too many Hud Outline assets loaded, maximum is %i |
| 5298 | TriggerSoundStart is only valid on a SoundEntity |
| 5299 | Invalid SoundEntity entnum |
| 5300 | dangerousAerialVehicleScriptMover can only be set on a scriptmover entity |
| 5301 | invalid SetSpawnWeapon: %s |
| 5302 | You can't drop alternate fire weapon '%s'. Drop the primary weapon instead. |
| 5303 | Scavenger Item "%s" does not have the 'scavenger' inventory type. |
| 5308 | spawnFx called with invalid effect id |
| 5309 | spawnFx called with (0 0 0) up direction |
| 5310 | spawnFx called with (0 0 0) forward direction |
| 5311 | spawnFxForClient called with invalid effect id |
| 5312 | spawnFx called with invalid client entity |
| 5313 | spawnFx called with (0 0 0) up direction |
| 5314 | spawnFx called with (0 0 0) forward direction |
| 5315 | playLoopedFx called with invalid effect id |
| 5316 | playLoopedFx called with (0 0 0) up direction |
| 5317 | playLoopedFx called with (0 0 0) forward direction |
| 5318 | playLoopedFx called with repeat < 0.001 seconds |
| 5319 | Weapon '%s' does not support hybrid toggle |
| 5320 | Weapon '%s' does not have an equipped weapon state |
| 5321 | Weapon '%s' does not support hybrid toggle |
| 5322 | Weapon '%s' does not have an equipped weapon state; therefore the scope state will not be modified |
| 5323 | Player does not have weapon '%s'. Give the weapon first. |
| 5324 | Player does not have weapon '%s'. Give the weapon first. |
| 5325 | Player does not have weapon '%s'. Give the weapon first. |
| 5326 | Player does not have weapon '%s'. Give the weapon first. |
| 5327 | Invalid inventory type for weapon '%s'. Primary or Model Only inventory type is required. |
| 5328 | Weapon '%s' must have a valid slot. |
| 5329 | Player does not have weapon '%s'. Give the weapon first. |
| 5330 | Invalid inventory type for weapon '%s'. Primary inventory type is required. |
| 5331 | Player does not have weapon '%s'. Give the weapon first. |
| 5332 | Invalid inventory type for weapon '%s'. offhand inventory type is required. |
| 5333 | Player does not have weapon '%s'. Give the weapon first. |
| 5334 | You cannot set the high priority weapon to invalid weapon. If your intention is to clear the high priority weapon please call ClearHighPriorityWeapon. Weapon passed in: %s |
| 5335 | You cannot set the high priority weapon to a weapon which does not exist in the player's inventory. Please verify the script implementation. Weapon passed in: %s |
| 5336 | You cannot set the high priority weapon when a scripted viewmodel animation is playing. Weapon passed in: %s |
| 5337 | You have tried to clear the high priority weapon when there is no high priority weapon set. Please verify the script implementation. Weapon passed in: %s |
| 5338 | Turret weapon '%s' without useTurretViewmodelAnims enabled can be used but cannot be given to player. |
| 5339 | Weapon '%s' does not have a view model. |
| 5340 | Weapon '%%s' does not have attachments that support scope %d |
| 5341 | You can't directly give alt-fire weapons '%s'. |
| 5342 | Unable to equip weapon (%s). Check weapon class and model. |
| 5343 | %s is not a valid offhand weapon |
| 5344 | Weapon %s must be of weaponType script or grenade to work with giveAndFireOffhand |
| 5345 | Unable to equip weapon (%s) for giveandfireoffhand. |
| 5346 | %s is not a valid camo for weapon %s |
| 5347 | %s is not currently equipped |
| 5348 | playFx called with (0 0 0) forward direction |
| 5349 | playFx called with (0 0 0) up direction |
| 5350 | Trying to remove attachment %s in slot %d from weapon %%s that does not have that scope |
| 5363 | Trying to remove other attachment %s from weapon %%s that does not have that other attachment |
| 5364 | CG_ParseFixedCameraToken: unknown camera orientation index %d |
| 5365 | CG_ParsePanCameraToken: unknown camera orientation index %d |
| 5366 | CG_ParseRotateCameraToken: unknown camera orientation index %d |
| 5367 | CG_ParseZoomCameraToken: unknown camera orientation index %d |
| 5368 | CG_ParseCinematicCameraCutToken: unknown camera movement index %d |
| 5369 | Scene %s: Exceeded max <vision> settings (%d) |
| 5370 | Scene %s: Exceeded max <vision> settings (%d) |
| 5371 | Scene %s: invalid <vision> token '%s' |
| 5374 | Scene %s: Exceeded max <sound> settings (%d) |
| 5375 | Scene %s: invalid <sound> token '%s' |
| 5376 | Scene %s: Exceeded max <fov> settings (%d) |
| 5377 | Scene %s: invalid <fov> token '%s' |
| 5378 | Couldn't load cinematic camera scene: %s |
| 5379 | Scene %s: already parsed <version> as (%d) |
| 5380 | Scene %s: <version> must be first parameter. |
| 5381 | Scene %s: <slowmo> has been deprecated |
| 5382 | Scene '%s': Exceeded max camera cuts: %d |
| 5383 | Scene '%s': token '%s', exceeded max characters (%d) |
| 5384 | Scene '%s': invalid token '%s' |
| 5385 | spawnFx called with (0 0 0) up direction |
| 5386 | spawnFx called with (0 0 0) forward direction |
| 5387 | playLoopedFx called with (0 0 0) up direction |
| 5388 | playLoopedFx called with (0 0 0) forward direction |
| 5389 | playLoopedFx called with repeat < 0.001 seconds |
| 5390 | A scriptmover entity can only have one sentient type (dangerous aerial vehicle or harmless aerial vehicle) |
| 5391 | harmlessAerialVehicleScriptMover can only be set on a scriptmover entity |
| 5392 | A scriptmover entity can only have one sentient type (dangerous aerial vehicle or harmless aerial vehicle) |
| 5394 | Execution '%s' requires a valid default prop weapon name to be supplied to GiveExecution(). |
| 5413 | Omnvar loader/interner category capacity exceeded; the known limit is 80 entries per category |
| 5433 | File-handle slot table exhausted; the known table has 256 slots |
| 5476 | LUI Main/init error, usually wrapping an earlier Lua/LUI failure |
| 5536 | DCache read failed during fastfile content read |
| 5612 | DCache xfileVersion did not match the expected transientFileType version |
| 5617 | Pending DB-zone queue exceeded its maximum of 1,928 entries |
| 5635 | Client %i did not immediately transition to an execution %s animation. Currently playing %s. Check ASM state transitions. |
| 5636 | Invalid entity to clear streaming origin |
| 5638 | Loading CG SoundEnts: locaClientNum out of range (%i, MAX = %i) |
| 5639 | TriggerSoundStart requires 2 parameters |
| 5640 | '%s' does has not have a valid index (%i) |
| 5641 | TriggerSoundStop is only valid on a SoundEntity |
| 5642 | Invalid SoundEntity entnum |
| 5653 | LUI_Interface_GetTextPages: page overflow |
| 5654 | element %u of bot score flags array is not a string |
| 5655 | AgentSetFavoriteEnemy: entity is not sentient. |
| 5656 | Cannot create capsule with radius larger than half the height |
| 5657 | entity somehow has the wrong entity number: entref.entum = %u, pSelf->s.number = %i |
| 5658 | Unable to find start node for traverse. |
| 5659 | Failed to properly start negotiation. |
| 5660 | AI cannot start traverse when there is no path. |
| 5661 | All stances disallowed for bot (path) |
| 5662 | All stances disallowed for bot (behavior) |
| 5666 | ClearSoundSubmix: Incorrect fadeTime parameters %.2f- expect >= 0 (or %.2f for asset default) |
| 5667 | ClearAllSoundSubmixes: Incorrect fadeTime parameters %.2f- expect >= 0 (or %.2f for asset default) |
| 5669 | Usage: ScaleSoundVolume( <volume>, [blendTimeMs] ) |
| 5670 | ScaleSoundVolume is only valid on a SoundEntity |
| 5671 | Invalid SoundEntity entnum |
| 5672 | ScaleSoundVolume - volume cannot be NAN. |
| 5673 | Usage: ScaleSoundPitch( <pitch>, [blendTimeMs] ) |
| 5674 | ScaleSoundPitch is only valid on a SoundEntity |
| 5675 | Invalid SoundEntity entnum |
| 5676 | ScaleSoundPitch - pitch cannot be NAN. |
| 5680 | Missing information for GScr_MatchDataFillPlayerHeader. |
| 5684 | GScr_MatchDataFillPlayerHeader - unable to move to players array in matchdata |
| 5685 | Deadzone must be less than 1.0 and >= 0.0 |
| 5686 | Deadzones must be less than 1.0 and >= 0.0 |
| 5687 | Deadzones must be less than 1.0 and >= 0.0 |
| 5688 | Deadzone must be less than 1.0 and >= 0.0 |
| 5689 | Deadzones must be less than 1.0 and >= 0.0 |
| 5690 | Deadzones must be less than 1.0 and >= 0.0 |
| 5691 | Unknown facial state %s |
| 5692 | Not enough space allocated in DynEnt Spatial partition for MyChanges. Full rebuild is required. |
| 5693 | Not enough dynEnt client data allocated for MyChanges. Full rebuild is required. |
| 5694 | Not enough dynEnt client data pose parts allocated for MyChanges. Full rebuild is required. |
| 5695 | Not enough dynEnt client data pose parts allocated for MyChanges. Full rebuild is required. |
| 5696 | [SPAWNGROUP_LOOT] found invalid rarity level '%d' (max %d) in table '%s'. |
| 5698 | SV_SKEL_MEMORY_SIZE exceeded |
| 5699 | Online_MatchData_RecordBreadcrumbData - error accessing playerInfo for clientNum %d |
| 5700 | Invalid number of parameters. |
| 5701 | Invalid parameter name. |
| 5702 | No model exists. |
| 5703 | Byte value must be between 0 and 255 |
| 5704 | Invalid value. Must be an int between 0 and 255 |
| 5709 | IsAlliedSentient() requires two sentient entity parameters |
| 5710 | IsAlliedSentient() requires two valid entities that are both sentient |
| 5711 | PlayerASM: Cannot use animation from state '%s' since it is excluded from pack maps. |
| 5712 | [SPAWNGROUP_LOOT] Failed to pick an item type from the item type list using chance logic |
| 5713 | Battle Royale Circle entities are not allowed to be passed to this function. |
| 5716 | entity type '%s' is not a turret |
| 5717 | entity type '%s' is not a turret |
| 5718 | entity type '%s' is not a turret |
| 5719 | entity type '%s' is not a turret |
| 5720 | entity type '%s' is not a turret |
| 5721 | entity type '%s' is not a turret |
| 5722 | entity type '%s' is not a turret |
| 5724 | Trying to move two non-default dynEntityLists. If this is MyChanges, cannot change dynentity lists without restarting map. |
| 5725 | Cannot setup door on non-2D navigator |
| 5727 | Cannot setup door open on AI without a path. |
| 5728 | Unsupported door-open lookup type %s |
| 5729 | Player is not linked. |
| 5730 | NodesVisible() with the <nopeek> option requires extra path data to be compiled for this map |
| 5731 | Missing parameter: player Skydive_SetForceThirdPersonStatus( <enabled> ); |
| 5732 | OnScrCmd_SetupShootStyleAdditive(): shotStyle %d is incompatible with numShots %d. |
| 5733 | OnScrCmd_SetupShootStyleAdditive(): numShots %d out of range (0-7). |
| 5736 | Changing the game type is not supported while the server is running |
| 5737 | Not a valid accessory asset: %s. |
| 5738 | Cannot give accessory in alt mode. |
| 5739 | Null weapon provided to GiveAccessory(). |
| 5740 | USAGE: <entity> ClearAccessory() |
| 5741 | Weapon provided to GiveAccessory() must be of weaponType 'model only'. Please check the weapon and attachment assets. |
| 5742 | Weapon provided to GiveExecution() must be of weaponType 'model only' or 'bullet'. Please check the weapon and attachment assets. |
| 5743 | Weapon provided to GiveExecution() must be of inventoryType 'model only'. |
| 5744 | Weapon provided to GiveAccessory() must be of inventoryType 'model only'. |
| 5745 | Player does not have an accessory equipped. |
| 5746 | ent %d is not running an asm! |
| 5747 | ent %d is not currently running asm %s! |
| 5748 | Challenge event buffer is corrupted. |
| 5749 | ScriptMoverClearDistanceFade() must be called on a script mover |
| 5751 | ReportChallengeUserEvent: array parameter %d is too long |
| 5752 | ReportChallengeUserEvent: invalid entry %d in parameter %d -- %s is not a string or number |
| 5753 | ReportChallengeUserEvent: invalid parameter %d is not an array, string or number |
| 5754 | Failed to reset ASM state in ForceTeleport() |
| 5755 | GScr_MainMP_SetPlayerCorpseDone only valid for player corpses. |
| 5764 | Invalid parameter count, requires 4 floats (pitch_min, pitch_max, yaw_min, yaw_max ) |
| 5765 | Invalid parameter count, requires 6 floats (pitch_min, pitch_max, yaw_min, yaw_max, neutral_pitch, neutral_yaw) |
| 5767 | Anim node '%s' LOD (%d) should be less than or equal to that of its parent (%d) |
| 5769 | Invalid argument specified to SetSticker function. Second argument must be a string. Usage: weaponObj = weaponObj SetSticker( <slot index>, <sticker material> ); |
| 5770 | Invalid argument specified to SetSticker function. Sticker slot index must be in the range of [0, 3]. Usage: weaponObj = weaponObj SetSticker( <slot index>, <sticker material> ); |
| 5771 | Invalid argument specified to SetSticker function. Sticker material cannot be empty string. Usage: weaponObj = weaponObj SetSticker( <slot index>, <sticker material> ); |
| 5772 | Cannot clear sticker on null weapon |
| 5773 | Invalid argument specified to ClearSticker function. First argument must be an integer. Usage: weaponObj = weaponObj ClearSticker( <slot index> ); |
| 5774 | Invalid argument specified to ClearSticker function. Sticker slot index must be in the range of [0, 3]. Usage: weaponObj = weaponObj ClearSticker( <slot index> ); |
| 5775 | Cannot clear stickers on null weapon |
| 5778 | CapTurnRate: Incorrect number of parameters ( %d ). |
| 5783 | PlayerASM animset '%s' and patch '%s' does not support addons. |
| 5784 | PlayerASM animset '%s' and patch '%s' does not support alias addons. |
| 5785 | Weapon %s does not have an offhand class set thus cannot be assigned to an offhand slot for client %d |
| 5786 | Weapon %s does not have a valid offhand weapon type thus cannot be assigned to an offhand slot for client %d |
| 5787 | Weapon %s does not have the inventory type offhand thus cannot be assigned to an offhand slot for client %d |
| 5788 | Weapon %s does not exist in the client inventory thus cannot be assigned to an offhand slot for client %d |
| 5789 | GetClosestReachablePointOnNavmesh : invalid entity. |
| 5790 | GetClosestReachablePointOnNavmesh : Entity %d does not have a navigator. |
| 5791 | Cannot set transient soundbank without a soundbanklist asset. |
| 5792 | Transient soundbank '%s' not found in soundbanklist asset. |
| 5793 | entity %i is not a player or player-controlled agent |
| 5794 | entity %i is not a player or player-controlled agent |
| 5795 | entity %i is not a player or player-controlled agent |
| 5796 | incorrect number of parameters |
| 5797 | must be >= 2 or equal to 0 |
| 5798 | must be <= 8 |
| 5808 | Physics Worker Error - please check tty for details |
| 5809 | CL_TransientsCollisionMP_HasGridSetupForClientStreamView: unexpected mode 'CL_TRANSIENTS_COLLISION_MP_TRANSIENTMODE_COUNT' |
| 5810 | ScriptableSv_LinkReservedScriptable: Could not find a free slot for requested scriptable %i%i |
| 5814 | Customization Setup Validation Error (%s). See console for details |
| 5815 | Unsupported block compression type (oodle): %d |
| 5816 | Unknown block compression type: %d |
| 5817 | Scriptable %s tried to find bone %s in model %s and failed |
| 5818 | Scriptable %s tried to find bone %s but had no model |
| 5819 | Loot (Client): Mismatching checksum between server and client table '%s' (%x vs %x) |
| 5820 | Scriptable checksum validation failed. See log for details |
| 5821 | ScriptableClSP_ArchiveState: mismatch in g_scriptableCl_fixedWorldMap.indicesCount. Scriptable defs have likely changed. |
| 5822 | ScriptableClSP_ArchiveState: mismatch in s_scriptableCl_reverseFixedWorldMapCount. Scriptable defs have likely changed. |
| 5823 | ScriptableClSP_ArchiveState: mismatch in s_scriptableCl_reverseReservedWorldMapCount. Scriptable defs have likely changed. |
| 5824 | Entity is not a sound_transient_soundbanks |
| 5825 | Tournament: Request tournament state failed - could not allocate a task!. |
| 5826 | Tournament: Leave all tournaments failed - could not allocate a task!. |
| 5827 | Tournament: Ack join tournaments failed - could not allocate a task!. |
| 5828 | Tournament: matchmaking ready up failed - could not allocate a task!. |
| 5829 | Tournament: submit match results failed - could not allocate a task!. |
| 5830 | Tournament: matchmaking rejoin failed - could not allocate a task!. |
| 5832 | Cannot set an entity's position while it is playing a delta animation. Call scriptmodelclearanim first. |
| 5833 | total time must be positive |
| 5841 | Failed to CG_Weapon_AddParachuteModel: Model limit (%d) exceeded with model '%s'. |
| 5842 | Failed to CG_Weapon_AddViewmodelAccessory: Model limit (%d) exceeded. |
| 5843 | OneLevelSkipList failed to allocate a new page, no free pages left! |
| 5844 | OneLevelSkipList failed to add link -- LINK_COUNT=%d, might be misconfigured |
| 5847 | Can call it on a Suspended Vehicle |
| 5848 | not an entity |
| 5849 | StartPathNodes: Times array is not the same length as points array |
| 5850 | StartPathNodes: Element %d of times array is not a float |
| 5851 | RCPlane set property needs a parameter |
| 5852 | RCPlane set property must be called on a physics RCPlane vehicle |
| 5853 | Can't suspend vehicle that a player is using |
| 5854 | Not an entity |
| 5855 | Trying to call WakeUpVehicle() on an entity that is not a Script Model. '%s' |
| 5856 | Must pass in a player |
| 5858 | This scriptable is no longer in use. |
| 5859 | Can only set the origin on a reserved scriptable. Scriptable is map-based. |
| 5860 | Can only set the origin on standalone scriptables. Scriptable is attached to an entity. |
| 5861 | G_Utils_IsNameCompositeModel_FastFile: Conflicting name for xmodel/xcompositemodel: %s |
| 5863 | ScriptableSv_ArchiveState: mismatch in g_svScriptableRuntimePartStatesCount (%u vs %u). Scriptable defs have likely changed. |
| 5864 | ScriptableSv_ArchiveState: Could not find def '%s' for scriptable %i. |
| 5865 | CL_TransientsCommonMP_GetCommonMPType(): Invalid TransientFileType %d |
| 5866 | AI field %s must be > 0, value: %g |
| 5868 | Cannot change a 'willNeverChange' entity |
| 5869 | showOnlyToClient error: param must be a client entity |
| 5871 | No matchdata def defined |
| 5873 | ReportChallengeUserEvent: challenge event buffer full |
| 5874 | ReportChallengeUserEvent: challenge event buffer full |
| 5875 | ReportChallengeUserEvent: challenge event buffer full |
| 5876 | ReportChallengeUserEvent: challenge event buffer full |
| 5877 | ReportChallengeUserEvent: challenge event buffer full |
| 5878 | ReportChallengeUserEvent: challenge event buffer full |
| 5879 | ReportChallengeUserEvent: array parameter %d is too long |
| 5880 | ReportChallengeUserEvent: array parameter %d is too long |
| 5881 | ReportChallengeUserEvent: challenge event buffer full |
| 5886 | DenseGrid::Read: mismatch for numCells: was %d, new value is %d. |
| 5887 | DenseGrid::Read: mismatch for numPages: was %d, new value is %d. |
| 5888 | Could not look up "%s" in playerdata.ddl |
| 5889 | Could not look up "%d" entry of "%s" array in playerdata.ddl |
| 5893 | Challenge event buffer is corrupted. |
| 5894 | AnimScripted entities are not supported in this game mode |
| 5900 | USAGE: <entity> GiveAccessory( <accessory>, <accessory_weapon> ) |
| 5901 | USAGE: <player> SetCarryObject( <carryObjectName> ) |
| 5902 | Can't find carry object '%s' |
| 5903 | Cannot get cover angle limits for 3D node %d at %s |
| 5904 | Exceeded max number of elements (%d). |
| 5905 | Some element of array is neither a vector nor an entity. |
| 5906 | GetStairsStateAtDist: Entity %d must have a navigator. |
| 5907 | StairsWithinDistance: Entity %d must have a navigator. |
| 5909 | Unable to find valid traverse within %.f units down the path. |
| 5913 | Bad call to GetVector(). Pass in a mandatory origin parameter. |
| 5914 | Bad call to DebugAxis(). ColorScale must be a multiplier float between 0.0 and 2.0 |
| 5915 | Bad call to DebugAxis(). Mandatory parameters are the origin and angles. |
| 5916 | entity type '%s' is not a turret |
| 5917 | entity type '%s' is not a turret |
| 5918 | ScriptMoverDistanceFade() must be called on a script mover |
| 5919 | ScriptMoverDistanceFade() cannot be called on scriptmovers that are brushmodels |
| 5921 | usage: VignetteSetParams( <intensity>, <squareAspectRatio>, <scaleX>, <scaleY>, <falloff>, <falloffStart>, <boxSizeX>, <boxSizeY>, <offsetX>, <offsetY>, <optional:lerpDuration> ) |
| 5922 | intensity must be between -1 and 1 - %f |
| 5923 | Not enough parameters. |
| 5924 | GetScriptableInstance() must be called on an entity linked to a scriptable |
| 5925 | Scriptable is not linked to an entity |
| 5926 | Entity must be a player |
| 5927 | Entity must be a player |
| 5928 | SetSoundSubmix: Incorrect fadeTime parameters %.2f- expect >= 0 (or %.2f for asset default) |
| 5929 | SetSoundSubmix: Incorrect scale parameters %.2f- expect [%.2f,%.2f]) |
| 5930 | ScaleSoundSubmix: Incorrect scale parameters %.2f- expect [%.2f,%.2f]) |
| 5931 | Hud Outline name not found (%s)! Make sure you add your Hud Outline asset to your level csv file! |
| 5932 | GScr_Skydive_SetBaseJumpingStatus(). Only valid on players; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 5933 | Missing parameter: player GScr_Skydive_SetBaseJumpingStatus( <enabled> ); |
| 5934 | Skydive_Interrupt(). Only valid on players; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 5935 | Skydive_SetForceThirdPersonStatus(). Only valid on players; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 5936 | unable to spawn br circle |
| 5938 | Cannot set the sticker on null weapon |
| 5939 | Invalid argument specified to SetSticker function. First argument must be an integer. Usage: weaponObj = weaponObj SetSticker( <slot index>, <sticker material> ); |
| 5957 | State %s does not have default (rifle) anim |
| 5959 | GetScriptableInstanceFromIndex requires an index to be passed in. |
| 5960 | GetScriptableInstanceFromIndex requires a valid loot scriptable index to be passed in. |
| 5963 | ShowOnlyToPlayer only works on non-player entities |
| 5964 | Invalid override setup specifies an incorrect attachment slot %d |
| 5966 | DObjProceduralBones_Create: not enough memory for procedural bones on server entity %d (alloc size %zu, model '%s') |
| 5968 | %s %s: camera asset '%s' must be of profile "First Person View" to be used as a viewmodel-attached camera. |
| 5982 | Too many carry object assets loaded, maximum is %i |
| 5983 | expected '%s', but reached end of file |
| 5984 | expected '%s', found '%s' instead |
| 5985 | '%s': node type already declared as '%s' |
| 5987 | boolean values must be either 'true' or 'false' |
| 5988 | '%s' is not a valid vec3 value |
| 5989 | '%s' is not a valid vec3 value |
| 5990 | unrecognized node parameter '%s' |
| 5991 | Game parameter field '%*s' not recognized |
| 5992 | invalid game parameter '%s' |
| 5993 | setting of node parameter '%s' from script is not supported |
| 5994 | LOD level out of range (found %d, max is %d) |
| 5995 | expected valid LOD level, found '%s' instead |
| 5996 | invalid sync group number '%s'. Valid range is 0..%d |
| 5997 | unrecognized property '%s' |
| 5998 | duplicate animation |
| 5999 | Sync group '%d' specified by node '%s' is different from the inherited sync group '%d'. |
| 6000 | LOD level '%d' for node '%s' should be less than inherited LOD level '%d' |
| 6001 | Graft node '%s' is expected to be empty. |
| 6002 | duplicate animation |
| 6003 | found '%s' but expected alias identifier |
| 6004 | found '%s' but expected alias identifier |
| 6005 | found '%s' but expected identifier or <parameter> |
| 6006 | too many LOD levels (maximum is %d) |
| 6007 | LOD distance '%.2f' must be greater than previous (%.2f) |
| 6008 | expected positive LOD distance value, found '%s' instead |
| 6009 | found '%s' but expected 'force_cs_anim_index_match' or 'lod_distances'. |
| 6010 | IsAnimationTimerBiggerThan() Expected a number as parameter. |
| 6011 | IsAnimationTimerBiggerThan() Expected a parameter number between 0.0 and 1.0. |
| 6014 | Scriptable Parsing: Change buffer overflow (%zu) |
| 6015 | Scriptable Parsing: Received invalid change (%zu) |
| 6016 | Tournament: matchmaking register failed - could not allocate a task!. |
| 6029 | DCache fastfile is out of date (version %d, expecting %d), use '+set purgeDCache 1' to clear cache. |
| 6030 | DCache fastfile is newer than client executable (version %d, expecting %d), use '+set purgeDCache 1' to clear cache. |
| 6031 | Fastfile is out of date (XFILE_FF_HEADER_VERSION %d, expecting %d) |
| 6032 | Fastfile is newer than client executable (XFILE_FF_HEADER_VERSION %d, expecting %d) |
| 6033 | FF magic was invalid |
| 6034 | %s is out of date (XFILE_VERSION %d, expecting %d) |
| 6035 | %s is newer than client executable (XFILE_VERSION %d, expecting %d) |
| 6036 | Unable to open |
| 6037 | Unable to read header |
| 6038 | Unable to read header |
| 6039 | XFILE_FD_HEADER_VERSION mismatch. Has '%d', wanted '%d' |
| 6040 | XFILE_DIFF_VERSION version mismatch. Has '%d', wanted '%d' |
| 6041 | Patch header data mismatch between current and prior patch |
| 6042 | Patch header data mismatch between base FF '%s' and current patch |
| 6043 | Invalid mark type specified: %s. See console log for details. |
| 6044 | Invalid mark type specified: %s. See console log for details. |
| 6046 | EnableStateLookAt requires the first optional argument to be an integer |
| 6047 | EnableStateLookAt requires the second optional argument to be an integer |
| 6048 | EnableScriptedLookAt requires the optional argument to be an integer |
| 6049 | GetAdjustedExitDirection takes between 1 and 4 args. |
| 6050 | Cloth legs model doesn't have valid cloth node with xanimClothLCN param in %s |
| 6052 | Player legs doesn't have valid cloth node with xanimClothLCN param in %s |
| 6054 | Animset %s state %s - unable to find alias %s |
| 6055 | Animset %s state %s - unable to find alias %s |
| 6058 | unloadallinfilintrotransients has %d parameters. There should be exactly zero. |
| 6060 | LoadInfilTransient couldn't find transient '%s' listed in the infil csv files for this map |
| 6061 | LoadInfilTransient cannot load intro infil '%s' during gameplay. |
| 6062 | UnloadInfilTransient couldn't find transient '%s' listed in the infil csv files for this map |
| 6063 | Material name string is empty |
| 6064 | Material '%s' is not precached. |
| 6073 | BG_PlayerASM_ClearSyncGroup() Cannot sync node '%s' since the sync data is specified on one of its parents ('%s'). |
| 6074 | unable to spawn map circle |
| 6075 | Map Circle entities are not allowed to be passed to this function. |
| 6076 | Usage: SpawnMapCircle( <x>, <y>, <initial_radius>, [owner] ) |
| 6077 | GScr_Weapon_Create called with invalid weapon. See console log for details. |
| 6078 | DLog_RecordEvent field '%s' is ambiguous, could be a row or a value array! |
| 6080 | Cannot call PlayerHide with both <forceTransmitToAllClients> and <forceTransmitToNoClients> set to true |
| 6081 | Cannot call PlayerHide with both <forceTransmitToAllClients> and <forceTransmitToNoClients> set to true |
| 6082 | not a player entity |
| 6083 | not a player entity |
| 6084 | entity %i is not a player or player-controlled agent |
| 6085 | entity %i is not a player or player-controlled agent |
| 6087 | Hud Outline name not found (%s); ensure the Hud Outline asset is included in the level CSV |
| 6088 | Hud Outline name not found (%s); ensure the Hud Outline asset is included in the level CSV |
| 6089 | Hud Outline name not found (%s)! Make sure you add your Hud Outline asset to your level csv file! |
| 6090 | GScr_SpawnScriptable payload must be between 0 and 65535 |
| 6091 | Camera name not found (%s). Call CameraDefault() if you want to disable a camera. |
| 6092 | Invalid mark type specified: %s. See console log for details. |
| 6093 | Invalid mark player specified. Not a player entity |
| 6094 | Invalid mark team specified: %s. |
| 6095 | Invalid mark player specified. Array entry is not player entity. |
| 6096 | Invalid mark team specified. Array entry is not a string. |
| 6097 | Invalid mark team specified: %s. |
| 6098 | Invalid mark filter specified. Can't provide player entities when in team-filtering mode |
| 6099 | Invalid parameter array type for filtering |
| 6100 | Invalid mark filter specified. Value is not a team/player or array of teams/players. |
| 6102 | ShootCustomWeapon: Sorry, tagflash override does not work properly with weapon (%s) with attachments/etc. |
| 6103 | Failed to open loopback connection for local player |
| 6104 | G_SaveSP_ReadTypeFields: ReadSize too large (%d) / G_SaveSP_ReadTypeFields: %s |
| 6106 | Invalid mark type specified: %s. See console log for details. |
| 6107 | Invalid mark type specified: %s. See console log for details. |
| 6108 | Invalid mark type specified: %s. See console log for details. |
| 6109 | Invalid mark type specified: %s. See console log for details. |
| 6110 | Scriptable_MyChangesRestoreScriptableLinks: Can't find scriptableDef %s, did it get removed or did its name change? |
| 6123 | ActivateNightVisionBlind: This method must be called on players. |
| 6124 | Found [%d] bad vehicle collmap entities that must be fixed |
| 6125 | SetSquadIndex() only valid for private matches, public matches squads are assigned via the matchmaker |
| 6126 | GetSquadIndex(). Only valid on players; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 6127 | USAGE: <player> GetSquadIndex() |
| 6128 | Could not create match data context. Match data may be too big so we can't load it! |
| 6130 | numTiles should be >= 1 |
| 6131 | GetNodesOnPath() found an invalid node in the newPath of length %i, see array output above. |
| 6132 | Invalid suit |
| 6133 | Suit height is undefined |
| 6134 | Ran out of BTs. Mem leak? |
| 6135 | index %i is an illegal callout marker ping local pool index. Valid indices are 0 to %i |
| 6138 | Vehicle_SetHeldWeaponVisibility(). Only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 6139 | Vehicle_SetHeldWeaponVisibility(). Only valid on players or agents which are playing vehicle animations via the AnimScriptEnterVehicle call; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 6140 | Vehicle_SetHeldWeaponVisibility(). USAGE: <player> Vehicle_SetHeldWeaponVisibility( isVisible ) |
| 6141 | Vehicle_SetStowedWeaponVisibility(). Only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 6142 | Vehicle_SetStowedWeaponVisibility(). Only valid on players or agents which are playing vehicle animations via the AnimScriptEnterVehicle call; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 6143 | Vehicle_SetStowedWeaponVisibility(). USAGE: <player> Vehicle_SetStowedWeaponVisibility( isVisible ) |
| 6144 | Mismatching Omnvnar checksum: %x vs %x |
| 6145 | Mismatching client count: %i vs %i |
| 6146 | Mismatching agent count: %i vs %i |
| 6147 | Physics_Raycast: Parameter 3 must be an entity. |
| 6148 | SetNightVisionBlindWeight must be called on a player |
| 6149 | Usage: level.player SetNightVisionBlindWeight( <weight> ) |
| 6150 | Vehicle_AllowPlayerStandOn - must be called on a vehicle. |
| 6151 | CloneBrushmodelToScriptmodel will not work properly on an entity with a model defined (will not properly create physics for the entity) |
| 6152 | invalid SetSpawnWeapon, does not support alt mode: %s |
| 6153 | SetMapCircleStyleIndex() called on an entity that is not a map circle. |
| 6154 | Usage: SetMapCircleStyleIndex( <idx> ) |
| 6155 | SetMapCircleIconIndex() called on an entity that is not a map circle. |
| 6156 | Usage: SetMapCircleIconIndex( <idx> ) |
| 6157 | Trying to retrieve the last ground position for a player that has not spawned or was never on the ground. Test HasLastGroundOrigin first. |
| 6158 | weight arg can only be used with non-solid obstacles. |
| 6159 | weight arg cannot be used with any other blockage flags. |
| 6160 | weight arg cannot be used with team blockage flags. |
| 6161 | weight arg can only be used with non-solid obstacles. |
| 6162 | weight arg cannot be used with any blockage flags |
| 6163 | weight arg cannot be used with team blockage flags |
| 6166 | ScriptableSv_ArchiveState: '%s' Unsupported stream buffer change from %d to %d. |
| 6167 | ScriptableClSP_ArchiveState: '%s' Unsupported stream buffer change from %d to %d. |
| 6168 | SetPlayerAdvancedUAVDot(): Only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 6170 | invalid entry index %d for animset %s state %s |
| 6171 | Player ASM max number of states (%d) reached for asset '%s'. Increase PLAYERASM_MAX_NUM_STATES. |
| 6172 | Script mover type 'script_arms' are only spawnable through script |
| 6173 | Attempted to use script mover animations with non-script mover entity %u with eType %u. |
| 6174 | Script mover animations not supported for entity %u with script mover type '%s'. |
| 6175 | VehiclePinOnMiniMap: function requires 1 parameter. |
| 6177 | String or undefined expected. |
| 6179 | UpdateCoverExposeType called with invalid cover node. |
| 6180 | only allowed to disable stepouts if we're in fixednode. |
| 6182 | Invalid scriptable index |
| 6183 | Invalid scriptable index |
| 6184 | Invalid scriptable index |
| 6185 | Invalid scriptable index |
| 6186 | Invalid scriptable index |
| 6187 | Invalid scriptable index |
| 6188 | Invalid scriptable index |
| 6189 | ScriptableDoorOpen - Bad openAwayFromPosition input |
| 6190 | ScriptableDoorOpen - Bad openDir input |
| 6191 | Invalid scriptable index |
| 6192 | GScr_GetPlayerIP takes no parameters |
| 6193 | Dynamic weapon attachment icon table row count (%u) exceeds code limit for BG_MAX_DYNAMIC_WEAPON_ATTACHMENT_ICONS (%u). Increase the limit or implement IWH-196468. |
| 6194 | Scriptable Archive: Bad Save Version. read %d vs have %d |
| 6196 | GetMissileVelocity() invoked on invalid or non-missile entity. |
| 6197 | GetMissileVelocity() unsupported Trajectory Type %i for entity %i |
| 6200 | Physics Worker Error - please check tty for details |
| 6203 | Cannot call Vehicle_GetArray() with parameters |
| 6204 | Vehicle_GetInputValue - must be called with the input control. |
| 6205 | Vehicle_GetInputValue - control must be [0..%d] |
| 6206 | Vehicle_GetInputValue() must be called on a physics enabled vehicle |
| 6207 | DenseGrid::Read: reading memory file before the DenseGrid has been initialized. |
| 6208 | DenseGrid::Read: bad value for pageCount: read %d, max is %d. |
| 6209 | DenseGrid::Read: bad value for pageSize: read %d, max %d. |
| 6210 | DenseGrid::Read: mismatch for occupiedCellCount: expected %d, read %d. |
| 6211 | DenseGrid::Read: mismatch for occupiedPageCount: expected %d, read %d. |
| 6212 | SetVehicleSeatAnimConditionals(): Only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 6213 | SetVehicleSeatAnimConditionals() USAGE: <player> SetVehicleSeatAnimConditionals( <vehicletype>, <seat> ) |
| 6214 | SetVehicleSeatAnimConditionals() USAGE: <player> SetVehicleSeatAnimConditionals( <vehicletype>, <seat> ) |
| 6215 | SetVehicleSeatAnimConditionals(): cannot assign a seat of index %i! Must be a 0-9 index. |
| 6216 | SetVehicleSeatAnimConditionals(): could not find server def index for vehicle with asset name: %s |
| 6217 | SetVehicleSeatAnimConditionals(): could not find VehicleDef for vehicle of type: %s |
| 6218 | GScr_SendClientNetworkTelemetry must be called on a client |
| 6219 | GScr_MainMP_StartRagdollFromVehicleHit - Must only be called on player corpse. |
| 6220 | GScr_MainMP_StartRagdollFromVehicleHit - Must be called with a physics vehicle entity. |
| 6221 | LoadCustomizationPlayerView failed to get client state for client %u. |
| 6222 | LoadCustomizationPlayerView failed to collect view weapons for client %u. |
| 6223 | SetHideNameplate is only valid for clients. |
| 6224 | Spawn() with 'script_arms' expects 5 arguments, given %d. Usage: Spawn( "script_arms", <origin>, <spawnflags>, <forceNoCollision>, <character entity> ) |
| 6225 | Spawn() with 'script_arms' called with invalid client id %d (level max is currently %d) |
| 6226 | SetOnWallAnimConditional(): Only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 6239 | Scriptable Archive: Bad Save Version. read %d vs have %d |
| 6246 | UpdateCoverExposeType exceeded max possible pose count. |
| 6247 | Script mover type 'script_arms' are not allowed in SP! |
| 6250 | '%s' is not a valid objective image. Please make sure it is included as precache_objectiveicon in zone_source. |
| 6251 | Can't set the origin directly when we have a parent entity |
| 6252 | ScriptableSetParentEntity not supported in game mode |
| 6253 | Failed to set parent entity. See log for details. |
| 6254 | ScriptableClearParentEntity not supported in game mode |
| 6255 | G_PlayerSpawns_Save_SpawnCharacters: iterator out of range (%i, MAX = %i) |
| 6256 | G_PlayerSpawns_Save_SpawnViewers: iterator out of range (%i, MAX = %i) |
| 6257 | G_PlayerSpawns_Save_InfluencePoints: iterator out of range (%i, MAX = %i) |
| 6258 | G_PlayerSpawns_Save_SpawnGlobals: iterator out of range (%i, MAX = %i) |
| 6261 | SetClientKillstreakIndexes: not allowed on front end server |
| 6262 | SetClientKillstreakIndexes: entity must be a player entity |
| 6263 | SetClientKillstreakIndexes: the slot index value %i is greater than max value %i |
| 6264 | SetClientKillstreakIndexes: the killstreak index value %i is greater than max value %i |
| 6265 | ResetClientKillstreakIndexes: not allowed on front end server |
| 6266 | ResetClientKillstreakIndexes: entity must be a player entity |
| 6267 | SetClientKillstreakAvailability: not allowed on front end server |
| 6268 | SetClientKillstreakAvailability: entity must be a player entity |
| 6269 | SetClientKillstreakAvailability: killstreak index %i is greater than max value %i |
| 6270 | SetClientKillstreakAvailability: availability value %i is lower than min value %i |
| 6271 | SetClientKillstreakAvailability: availability value %i is greater than max value %i |
| 6272 | ResetClientKillstreakAvailability: not allowed on front end server |
| 6273 | ResetClientKillstreakAvailability: entity must be a player entity |
| 6274 | SetPowerAmmo: not allowed on front end server |
| 6275 | SetPowerAmmo: entity must be a player entity |
| 6276 | SetPowerAmmo: power slot input is empty |
| 6277 | SetPowerAmmo: ammo value %i cannot be negative |
| 6278 | SetPowerAmmo: power slot input is invalid |
| 6279 | SetPowerProgress: not allowed on front end server |
| 6280 | SetPowerProgress: entity must be a player entity |
| 6281 | SetPowerProgress: power slot input is empty |
| 6282 | SetPowerProgress: progress value %i cannot be negative |
| 6283 | SetPowerProgress: progress value %i cannot be higher than %i |
| 6284 | SetPowerProgress: power slot input is invalid |
| 6285 | A scriptable index is required when adding a ping on any scriptable based marker pool |
| 6286 | An entity number is required when adding a ping on any entity based marker pool |
| 6288 | GScr_MainMP_StartRagdollFromVehicleHit - Must only be called on player corpse. |
| 6289 | GScr_MainMP_StartRagdollFromVehicleHit - Must be called with a physics vehicle entity. |
| 6290 | Weapon must have an active-underbarrel attachment when used in alternate mode |
| 6291 | SetNightVisionBlindWeight must be called on a player |
| 6292 | HudOutlineDisableForClient() could not be applied because there are currently too many hud outline settings. Can we reduce color and depth test variations? |
| 6293 | HudOutlineEnableForClients could not be applied because too many HUD-outline settings exist |
| 6294 | HudOutlineDisableForClients could not be applied because too many HUD-outline settings exist |
| 6295 | HudOutlineEnable could not be applied because too many HUD-outline settings exist |
| 6296 | HudOutlineDisable() could not be applied because there are currently too many hud outline settings. Can we reduce color and depth test variations? |
| 6297 | Server arms entities are not allowed to be passed to this function. |
| 6298 | GScr_SpawnScriptable payload needs to be between 0 and 65535. |
| 6299 | SpawnCustomWeaponScriptable failed, see log for details. |
| 6300 | StreamSetMaterialTouchUntilLoaded can only be called on players |
| 6303 | EnableSpawnPointByIndex - Tried to enable a spawnpoint of index %i, which does not exist. |
| 6306 | array is too large (%d > %d), need to increase sortable array size |
| 6307 | element %i of array: type %s is not an object |
| 6308 | element %i of array: object's origin property is not a vector |
| 6310 | Usage: IncrementPersistentStat( player, stateName, increment ) |
| 6311 | Persistent-stat increment must be an integer or float |
| 6312 | Persistent stat must be an integer or float to increment |
| 6313 | SetMLGDamageDone: not allowed on front end server |
| 6314 | SetMLGDamageDone: entity must be a player entity |
| 6315 | SetMLGDamageDone: damage done %i cannot be negative |
| 6324 | Ent %i did not find an appropriate state in ASM %s for this execution. Error code %i. |
| 6325 | Exhausted texture pool! |
| 6326 | Function only valid on agent corpses. |
| 6329 | registerParty is deprecated. Please use self getPartyID() |
| 6330 | Table name for lookup cannot be empty |
| 6331 | Table name for lookup cannot be empty |
| 6332 | Table name for lookup cannot be empty |
| 6333 | Table name for lookup cannot be empty |
| 6334 | Table name for lookup cannot be empty |
| 6335 | Table name for lookup cannot be empty |
| 6336 | Table name for lookup cannot be empty |
| 6337 | Bone %s could not be found in DObj with model %s |
| 6338 | Persistent-stat increment must be an integer or float |
| 6340 | Not a valid origin or angle. |
| 6341 | msg overflowed |
| 6342 | SetMlgMessageSent not allowed on front end server |
| 6343 | SetMlgMessageSent: Incorrect number of parameters. |
| 6344 | SetMlgMessageSent: entity must be a player entity |
| 6349 | AnimTreeParser_SummarizeNode(): Parameter '%s' is not included in list of shared parameters for AI tree ('%s'). This will break SP saved games. |
| 6351 | ent does not have a valid anim tree. |
| 6352 | ent does not have a valid anim tree. |
| 6353 | ent does not have a valid anim tree. |
| 6354 | Invalid mark filter specified. Not a player entity. |
| 6355 | Invalid mark filter specified. Not a player entity. |
| 6356 | UpdateMLGAmmoInfo: not allowed on front end server |
| 6357 | UpdateMLGAmmoInfo: entity must be a player entity |
| 6360 | Online_ErrorReporting: Fatal OER: %llu_%d |
| 6361 | Incorrect number of parameters. <bool> |
| 6362 | SetBeingRevived: entity must be a player entity |
| 6363 | GetBeingRevived not allowed on front end server |
| 6364 | Incorrect number of parameters. |
| 6365 | GetBeingRevived: entity must be a player entity |
| 6368 | '%s' is not a valid objective image. Please make sure it is included as precache_objectiveicon in zone_source. |
| 6369 | Impact type 'none' must be the %ith entry in impacttypes.txt. |
| 6370 | Too many impact types listed in impacttypes.txt. Current code limit is %i; speak with an engineer to increase. |
| 6371 | DCache_TOC_RemotePurgeCacheRequest: %s |
| 6372 | Incorrect number of parameters. <loadoutType>, <itemName>/<rawValue> |
| 6373 | SetIsUsingSpecialist: entity must be a player entity |
| 6374 | SetMLGDominationPoint: not allowed on front end server |
| 6375 | SetMLGDominationPoint: entity must be a player entity |
| 6377 | Illegal call to BotSetScriptGoal(). Goal cannot be (0,0,0) |
| 6379 | Patch Collision can't find physics asset %s |
| 6380 | Patch Collision can't find collod asset %s |
| 6381 | Patch Collision can't use transient physics assets and %s is transient |
| 6382 | Patch Collision can't use transient collod assets and %s is transient |
| 6383 | GetFollowedPlayer must be called on a player. |
| 6384 | GetFollowedPlayer must be called on a follower. |
| 6385 | DCache_IO_ReadBlock: %s |
| 6386 | SetSticker slot parameter must be an integer |
| 6387 | SetSticker material parameter must be a string |
| 6388 | SetSticker slot index must be 0 or 1 |
| 6389 | SetSticker material name cannot be empty |
| 6390 | ClearSticker slot parameter must be an integer |
| 6391 | ClearSticker slot index must be 0 or 1 |
| 6392 | GetSticker slot parameter must be an integer |
| 6393 | GetSticker slot index must be 0 or 1 |
| 6394 | UpdateCurrentWeapon: not allowed on front end server |
| 6395 | UpdateCurrentWeapon: entity must be a player entity |
| 6396 | G_LoadGame: DynEntPose (id %d Basis %d) numParts changed (%d != %d) |
| 6397 | GScr_SetGameBattlePlayerStats( xuid, team, score ) requires 3 parameters |
| 6398 | GScr_SetGameBattleMatchStats( mapname, gametype, victor, alliesScore, axisScore, matchStart, matchEnd ) requires 7 parameters |
| 6399 | SetCamo name parameter must be a string |
| 6400 | SetCamo name cannot be empty |
| 6401 | Cannot find a valid vehicle camouflage index for string '%s' |
| 6402 | Number of sides must be greater than 2 and no more than %d |
| 6403 | distFromCenter ( %.f ) must be greater than 0 |
| 6404 | weight arg can only be used with non-solid obstacles. |
| 6405 | weight arg cannot be used with any other blockage flags. |
| 6406 | weight arg cannot be used with team blockage flags. |
| 6407 | Invalid entity index %d before SV_Game_SendServerCommand |
| 6408 | Invalid entity index %d before SV_Game_SendServerCommand |
| 6409 | Invalid entity index %d before SV_Game_SendServerCommand |
| 6410 | Invalid entity index %d before SV_Game_SendServerCommand |
| 6411 | Invalid entity index %d before SV_Game_SendServerCommand |
| 6412 | Invalid entity index %d before SV_Game_SendServerCommand |
| 6413 | Invalid entity index %d before SV_Game_SendServerCommand |
| 6414 | Invalid entity index %d before SV_Game_SendServerCommand |
| 6415 | Invalid entity index %d before SV_Game_SendServerCommand |
| 6416 | Invalid entity index %d before SV_Game_SendServerCommand |
| 6419 | Invalid entity index %d before SV_Game_SendServerCommand |
| 6420 | Invalid entity index %d before SV_Game_SendServerCommand |
| 6421 | Invalid entity index %d before SV_Game_SendServerCommand |
| 6422 | Invalid entity index %d before SV_Game_SendServerCommand |
| 6424 | PlayerASM secondary finger-pose animation '%s' must have the same parent as primary animation '%s' |
| 6425 | Invalid scriptable index |
| 6426 | Invalid scriptable index |
| 6427 | AnimScriptSelfRevivingDoneEvent(): only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 6428 | AnimScriptBuddyRevivingDoneEvent(): only valid on players or agents; called on entity %i at %.0f %.0f %.0f classname %s targetname %s |
| 6429 | SetLastStandEnabled not allowed on front end server |
| 6430 | SetLastStandEnabled: Incorrect number of parameters. |
| 6431 | SetLastStandEnabled: entity must be a player entity |
| 6432 | An inventory slot is required when adding a ping on any inventory slot |
| 6433 | CL_Streaming exceeded capacity CL_STREAMING_TOTAL_IMAGE_COUNT (%u) by %u required images. |
| 6434 | IsTacMapActive method is only supported for players. |
| 6435 | SendRequestGamerProfileCmd : This function must be called by a player |
| 6436 | USAGE: <entity> SendRequestGamerProfileCmd( <profile_cmd> ) |
| 6437 | Array contains mismatching types: %s != %s |
| 6438 | PlayerASM: animset '%s' has state '%s' and alias '%s' that contains addon alias '%s' referencing an anim inside a blendspace node. This might lead to issues where the blendspace does not turn off. Use the blendspace node directly instead. |
| 6440 | Preset name is too long to fit in the buffer |
| 6441 | CG_LoadViewModelAnimTrees: unsupported saved anim version (%d). |
| 6442 | Attempted to access an invalid head icon slot. |
| 6443 | Attachment '%s' has %u variants. This exceeded the code limit on number of variants, 'BG_WEAPONS_SETUP_MAX_VARIANT_NUM' (%u). |
| 6446 | LUI: Out of elements. |
| 6448 | SetAdditionalStreamPos has invalid player entity argument |
| 6449 | SetAdditionalStreamPos has invalid viewMode argument |
| 6450 | SetAdditionalStreamPos has invalid player entity argument |
| 6453 | Incorrect number of parameters. <loadoutType>, <itemName>/<rawValue> |
| 6454 | SetHasSpecialistBonus: entity must be a player entity |
| 6455 | Scriptable %d is trying to assign invalid weapon handle index %d |
| 6490 | Currently only supported on script model entities. |
| 6491 | MagicBullet called with an invalid spread value %f |
| 6492 | Invalid facial state %s |
| 6493 | Tournament: Starting join communication channel failed - DW services not ready for controller %i. |
| 6494 | ent %d is not running an asm! |
| 6495 | Unable to find state %s in animset %s |
| 6496 | Unable to find anim in state %s in animset %s |
| 6497 | GetNodeOffset_Code called on a despawned pathnode |
| 6498 | GetNodeOffset_Code not called on a pathnode |
| 6499 | nodes of type NODE_COVER_3D are not currently supported by GetNodeOffset_Code. |
| 6500 | ClientCharacter reached max number of (%d) anim trees |
| 6501 | IW8 SP supports a maximum of %i accessory assets. Failed to give accessory: %s. |
| 6502 | StopMusicState: Incorrect number of parameters |
| 6503 | entity %i is not a player or player-controlled agent |
| 6504 | Invalid entity index %d before SV_Game_SendServerCommand |
| 6505 | StopPlayerMusicState: Incorrect number of parameters |
| 6506 | SetMusicState: 2nd parameter is an incorrect type. |
| 6507 | SetMusicState: 2nd parameter is an incorrect type. |
| 6508 | SetMusicState: Out of memory for playlist array. |
| 6509 | SetMusicState: Array entry is not a string. |
| 6510 | SetPlayerMusicState: 2nd parameter is an incorrect type. |
| 6511 | SetPlayerMusicState: 2nd parameter is an incorrect type. |
| 6512 | SetPlayerMusicState: Out of memory for playlist array. |
| 6513 | SetPlayerMusicState: Array entry is not a string. |
| 6516 | This ping does not have a required entity set for setting a zOffset. |
| 6520 | Invalid suffix '%s' found on impact type '%s' in iwimpacts.txt. Please do not use this suffix as part of an impact type definition. |
| 6526 | ScriptCompile: MAX_SCR_COMPILE_FILE_LIST (%u) exceeded when adding far_function_count. See console log. |
| 6527 | Invalid parameter count to setoverridearchetype |
| 6528 | invalid priority string |
| 6529 | Invalid parameter count to setoverridearchetype |
| 6530 | invalid priority string |
| 6531 | Usage: PlaySound( <aliasname>, [ignoreplayer], [soundSourceEntity] ); Wrong number of parameters: %d / invalid priority string |
| 6532 | entity %i is not a player |
| 6533 | SetScriptMoverKillCam requires offset up/back arguments for offset-explosive type |
| 6534 | TriggerPortableRadarPing(): Invalid perk name. |
| 6535 | Not supported for front-end server |
| 6536 | unknown team '%s' |
| 6537 | GScr_SendCollectedClientAnticheatData must be called on a client |
| 6538 | SendClientMatchDataForClient not allowed on front end server |
| 6539 | SendClientMatchDataForClient: entity must be a player entity |
| 6540 | GetQuestPoints(): Must specify a point type. |
| 6541 | GetQuestPoints(): Must specify at least a maximum radius when providing an origin. |
| 6542 | Usage: GetLootSpawnPoint( <pointIndex> ) |
| 6543 | Requested point index %d is invalid, valid range is [0, %d) |
| 6544 | Usage: DisableLootSpawnPoint( <pointIndex> ) |
| 6545 | DisableLootSpawnPoint - Requested point index %d is invalid, valid range is [0, %d) |
| 6546 | DisableLootSpawnPoint - Requested point index %d is not a cache or quest point |
| 6549 | VerifyBunkerCode() USAGE: VerifyBunkerCode( <bunkerIndex>, <bunkerCode> ) |
| 6550 | GetAltBunkerIndexForName() USAGE: GetAltBunkerIndexForName( <bunkerScriptableInstanceName> ) |
| 6551 | PredictStreamPos has invalid viewMode argument |
| 6552 | entity %i is not a player or player-controlled agent |
| 6553 | missing argument for %s |
| 6554 | Invalid body specified. %s is not from the body customization table |
| 6555 | Invalid head specified. %s is not from the head customization table |
| 6556 | SetBeingRevived not allowed on front end server |
| 6557 | Cannot begin GameStart while another game start is in progress. Cur:[%d, %d, %s, %s] Next:[%s,%s] |
| 6558 | Cannot begin GameStart is not allowed to start. [%s, %s] |
| 6559 | EnablePhysicsMoverPush must be called on a moving platform |
| 6560 | EnablePhysicsMoverPush must be called on a server-authoritative-physics moving platform |
| 6561 | Expecting 5 arguments |
| 6562 | Incorrect number of parameters: ActivateClientExploder( exploder_num, <optional_player>, <optional_startTime> ) |
| 6563 | element %i of array: type %s is not an entity |
| 6564 | entity %i is not a player |
| 6565 | Failed to spawn standalone instance. See log for details. |
| 6566 | GetScriptableLootCacheContents must be given a lootInstance. |
| 6567 | GScr_GetScriptCacheContents: must be called with a valid <setName> |
| 6568 | GScr_GetScriptCacheContents: invalid cacheIndex value |
| 6569 | Owner entity %i is not a player |
| 6570 | SetMapCircleColorIndex() called on an entity that is not a map circle. |
| 6571 | Usage: SetMapCircleColorIndex( <idx> ) |
| 6572 | Array contains mismatching types: %s != %s |
| 6573 | Array contains mismatching types: %s != %s |
| 6574 | Array contains value that is not a comparable: %s |
| 6575 | Key array exceeds maximum sort size: %d > %d |
| 6576 | Invalid sort direction specified: %s is not "%s" or "%s" |
| 6577 | Cannot push player on non-2D navigator |
| 6578 | <playerList> must be an array. |
| 6579 | <playerList> array is too large (%d > %d) |
| 6580 | element %i of <playerList> array is not a vector |
| 6581 | <clusterList> must be an array |
| 6582 | <clusterList> array is too large (%d > %d) |
| 6583 | element %i of <clusterList> array: type %s is not an object |
| 6584 | element %i of <clusterList> array has no origin vector |
| 6585 | <mapBounds> must be an array |
| 6586 | <mapBounds> must be an array of two vectors |
| 6587 | <mapBounds> must be an array of two vectors |
| 6588 | Usage: ComputeDropBagPositions( <playerList>, <clusterList>, <mapBounds>, <minDist>, <maxDist>, <invalidateRadius>, <spotsPerCluster>, <spotDistanceFromCenter>, [circleOrigin], [circleRadius] ); |
| 6591 | Physics_Raycast: Parameter 7 'allowScriptables', if provided, must be a flag indicating whether to allow scriptables - see usage |
| 6592 | Physics_Spherecast: Parameter 7 'allowScriptables', if provided, must be a flag indicating whether to allow scriptables - see usage |
| 6597 | [SPAWNGROUP_LOOT] Overflowed number of spawned items, increase SPAWNGROUP_LOOT_MAX_ITEM_SPAWNS |
| 6598 | [SPAWNGROUP_LOOT] No items in set '%s' available of type '%s' from ratity %d to %d |
| 6599 | [SPAWNGROUP_LOOT] Failed to find any remaining item in the set '%s' matching type '%s' and rarity between %d and %d for spawning |
| 6600 | [SPAWNGROUP_LOOT] Rarity range in set '%s' specified has no items from %d to %d |
| 6601 | [SPAWNGROUP_LOOT] Failed to pick an item type from the item type list using chance logic |
| 6602 | [SPAWNGROUP_LOOT] Overflowed number of available cache contents, increase SPAWNGROUP_LOOT_MAX_CACHE_ITEMS |
| 6603 | [SPAWNGROUP_LOOT] Overflowed number of available caches, increase SPAWNGROUP_LOOT_MAX_CACHES |
| 6604 | GetPlayerGpadEnabled method is only supported for players. / GetPlayerWarTrackPassengerEnabled method is only supported for players. |
| 6605 | GetClientOmnvar - '%s' not found |
| 6606 | GetClientOmnvar - Type for paramater 1 not recognized |
| 6607 | GetUseHoldKBMProfile method is only supported for players. |
| 6608 | [SPAWNGROUP_LOOT] chance '%s' is invalid size for set '%s', needs to be zero to %d |
| 6609 | [SPAWNGROUP_LOOT] chance '%s' is invalid size for set '%s', needs to be zero to %d / [SPAWNGROUP_LOOT] rarityIndex '%s' is invalid size for set '%s', needs to be zero to %d |
| 6610 | [SPAWNGROUP_LOOT] table '%s' is empty |
| 6611 | [SPAWNGROUP_LOOT] there too many unique cacheSets in table '%s', please increase SPAWNGROUP_LOOT_MAX_CACHE_SETS |
| 6612 | [SPAWNGROUP_LOOT] there are two references to zone '%s' in '%s' in table '%s' |
| 6613 | [SPAWNGROUP_LOOT] rarity color value '%d' out of range (0-255) in table '%s'. |
| 6614 | [SPAWNGROUP_LOOT] overflowed cache type count. |
| 6615 | [SPAWNGROUP_LOOT] The zone map table '%s' has no zones in it |
| 6616 | [SPAWNGROUP_LOOT] There are no global zones in table '%s' |
| 6619 | can only use a turret |
| 6621 | VehicleShowOnMinimapForClient: Called on an invalid entity. Entity needs to be a vehicle. |
| 6622 | VehicleShowOnMinimapForClient: Entity must be a player or a bot. |
| 6623 | An objective index is required when adding a ping on any gsc objective based marker pool |
| 6624 | GScr_GetScriptCacheContents: no loot table currently loaded |
| 6625 | VehicleShowOnMinimapForClient: Function requires 2 parameters. |
| 6626 | Calling SetMoverOptimized on entity that is not a mover. Entity number %d |
| 6633 | Calling SetMoverAntilagged on entity that is not a mover. Entity number %d |
