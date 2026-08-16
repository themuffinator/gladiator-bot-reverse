# Retail function map

Address-keyed correspondence between the retail `gladiator.dll` and this
reconstruction. It replaces the exact-name overlap figure in
[source_parity_measurement.md](source_parity_measurement.md) with a per-routine
index, which is item 2 of that document's "Next measurement improvements".

## Sources

| Column | Origin |
| --- | --- |
| DLL address | `dev_tools/gladiator.dll.bndb_hlil.txt`, function headers in `.text` below `0x10044000` |
| HLIL lines | line span of that routine in the same dump, for `Read` with `offset`/`limit` |
| Retail name | per-function `// gladiator.dll: START..END` annotations in `ref/gladiator-bot-restored/botlib/` |
| Translation unit | the reference file carrying that annotation, i.e. the original 1999 TU |
| Cited at | every place in `src/` that names the address in a comment |

The address citation is a coverage signal, not a correspondence proof: this tree
uses normalized module-local names and frequently implements a routine without
naming its address. An uncited row means "not yet audited at address
granularity", not "missing".

## Coverage

| Measure | Count |
| --- | ---: |
| Retail botlib routines below `0x10044000` | 816 |
| ... paired with a named routine in the reference tree | 757 |
| ... cited by address somewhere in `src/` | 100 |
| CRT / thunk / unpaired routines (out of scope) | 59 |

| Translation unit | Routines | Cited | HLIL lines | Primary reconstruction |
| --- | ---: | ---: | ---: | --- |
| `be_aas_reach.c` | 36 | 3 | 6727 | `src/botlib/aas/aas_reach.c` |
| `be_ai2_dmq2.c` | 62 | 22 | 5257 | `src/botlib/ai/ai_dm.c` |
| `l_precomp.c` | 71 | 1 | 4362 | `src/botlib/precomp/l_precomp.c` |
| `be_aas_bspq2.c` | 46 | 11 | 4211 | `src/botlib/aas/aas_map.c` |
| `be_ai_chat.c` | 51 | 2 | 3609 | `src/botlib/ai_chat/ai_chat.c` |
| `be_ai_move.c` | 45 | 4 | 2590 | `src/botlib/ai_move/bot_move.c` |
| `be_ai2_dmnet.c` | 27 | 1 | 2340 | `src/botlib/interface/bot_interface.c` |
| `l_script.c` | 38 | 3 | 1882 | `src/botlib/precomp/l_script.c` |
| `be_aas_sample.c` | 24 | 3 | 1472 | `src/botlib/aas/aas_map.c` |
| `be_ai_goal.c` | 30 | 1 | 1413 | `src/botlib/ai_goal/bot_goal.c` |
| `be_ai_weight.c` | 19 | 0 | 1360 | `src/botlib/ai_weight/ai_weight.c` |
| `be_aas_route.c` | 24 | 2 | 1115 | `src/botlib/aas/aas_route.c` |
| `be_aas_main.c` | 26 | 6 | 980 | `src/botlib/aas/aas_main.c` |
| `be_aas_move.c` | 12 | 2 | 971 | `src/botlib/aas/aas_move.c` |
| `be_aas_cluster.c` | 15 | 1 | 841 | `src/botlib/aas/aas_cluster.c` |
| `be_aas_file.c` | 7 | 1 | 801 | `src/botlib/aas/aas_map.c` |
| `l_utils.c` | 18 | 3 | 760 | `src/botlib/common/l_utils.c` |
| `l_struct.c` | 9 | 0 | 737 | `src/botlib/common/l_struct.c` |
| `be_aas_entity.c` | 20 | 2 | 731 | `src/botlib/aas/aas_map.c` |
| `be_aas_debug.c` | 12 | 7 | 588 | `src/botlib/aas/aas_debug.c` |
| `be_ai2_main.c` | 21 | 6 | 568 | `src/botlib/interface/bot_interface.c` |
| `be_aas_sound.c` | 18 | 2 | 540 | `src/botlib/aas/aas_sound.c` |
| `be_ai_char.c` | 8 | 2 | 533 | `src/botlib/ai_character/bot_character.c` |
| `be_ai_weap.c` | 11 | 1 | 497 | `src/botlib/ai_weapon/bot_weapon.c` |
| `be_interface.c` | 30 | 4 | 474 | `src/botlib/interface/botlib_interface.c` |
| `be_ea.c` | 25 | 4 | 266 | `src/botlib/ea/ea_main.c` |
| `l_libvar.c` | 13 | 2 | 261 | `src/botlib/common/l_libvar.c` |
| `be_aas_optimize.c` | 8 | 1 | 254 | `src/botlib/aas/aas_optimize.c` |
| `be_aas_routealt.c` | 3 | 1 | 220 | `src/botlib/aas/aas_route.c` |
| `be_aas_light.c` | 6 | 0 | 163 | `src/botlib/aas/aas_sound.c` |
| `l_log.c` | 7 | 1 | 129 | `src/botlib/common/l_log.c` |
| `l_memory.c` | 10 | 1 | 125 | `src/botlib/common/l_memory.c` |
| `l_crc.c` | 5 | 0 | 74 | `src/botlib/common/l_crc.c` |

## Routines by translation unit

### `be_aas_bspq2.c`

Primary reconstruction: `src/botlib/aas/aas_map.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x10003010` | `?` | 4631-4640 | - |
| `0x10003080` | `sub_10003080` | 4641-4646 | `src/botlib/aas/aas_map.c:220`, `src/botlib/aas/aas_map.h:116` |
| `0x100030a0` | `sub_100030A0` | 4647-4692 | - |
| `0x100031b0` | `sub_100031B0` | 4693-4706 | - |
| `0x100031f0` | `sub_100031F0` | 4707-4724 | - |
| `0x10003240` | `sub_10003240` | 4725-4740 | - |
| `0x10003280` | `sub_10003280` | 4741-4758 | - |
| `0x100032d0` | `sub_100032D0` | 4759-4783 | - |
| `0x10003360` | `CM_PointLeafnum` | 4784-4821 | - |
| `0x10003420` | `sub_10003420` | 4822-4831 | - |
| `0x10003460` | `sub_10003460` | 4832-4847 | - |
| `0x100034d0` | `?` | 4848-4909 | - |
| `0x10003680` | `AAS_EntityCollision` | 4910-5229 | `src/botlib/aas/aas_map.c:903`, `src/botlib/aas/aas_map.c:909` (+2) |
| `0x10003bf0` | `sub_10003BF0` | 5230-5251 | - |
| `0x10003c90` | `CM_TraceThroughBrush` | 5252-5595 | - |
| `0x10004310` | `CM_TraceThroughLeaf` | 5596-6551 | `src/botlib/aas/aas_map.c:1270`, `src/botlib/aas/aas_map.c:1401` |
| `0x10005640` | `?` | 6552-6566 | - |
| `0x100056d0` | `sub_100056D0` | 6567-6618 | - |
| `0x100057a0` | `sub_100057A0` | 6619-6727 | `src/botlib/aas/aas_map.c:223`, `src/botlib/aas/aas_map.c:1897` (+2) |
| `0x10005a10` | `sub_10005A10` | 6728-6736 | `src/botlib/aas/aas_map.c:222` |
| `0x10005a60` | `AAS_DecompressVis` | 6737-6776 | - |
| `0x10005b30` | `AAS_InPVS` | 6777-6847 | - |
| `0x10005c60` | `AAS_inPVS` | 6848-6854 | - |
| `0x10005c90` | `sub_10005C90` | 6855-6860 | - |
| `0x10005cc0` | `sub_10005CC0` | 6861-6867 | - |
| `0x10005cf0` | `?` | 6868-6938 | - |
| `0x10005e60` | `?` | 6939-7021 | `src/botlib/aas/aas_map.c:1099`, `src/q2bridge/aas_translation.h:72` |
| `0x10006090` | `AAS_UnlinkFromBSPLeaves` | 7022-7051 | `src/botlib/aas/aas_map.c:10108` |
| `0x10006100` | `AAS_BoxOnPlaneSide2` | 7052-7117 | - |
| `0x10006210` | `AAS_BSPLinkEntity` | 7118-7206 | `src/botlib/aas/aas_map.c:10109` |
| `0x100063d0` | `?` | 7207-7338 | - |
| `0x10006600` | `?` | 7339-7473 | - |
| `0x10006760` | `AAS_ValueForBSPEpairKey` | 7474-7526 | - |
| `0x100067e0` | `AAS_VectorForBSPEpairKey` | 7527-7549 | - |
| `0x100068a0` | `FloatForKey` | 7550-7560 | - |
| `0x100068e0` | `AAS_IntForBSPEpairKey` | 7561-7571 | - |
| `0x10006920` | `AAS_FreeBSPEntities` | 7572-7608 | - |
| `0x100069a0` | `AAS_ParseBSPEntities` | 7609-7840 | `src/botlib/aas/aas_map.c:43` |
| `0x10006d10` | `?` | 7841-8015 | - |
| `0x10007150` | `sub_10007150` | 8016-8033 | - |
| `0x100071e0` | `?` | 8034-8167 | - |
| `0x10007460` | `?` | 8168-8452 | - |
| `0x10007980` | `AAS_DumpBSPData` | 8453-8590 | - |
| `0x10007c40` | `sub_10007C40` | 8591-8622 | - |
| `0x10007d30` | `AAS_LoadBSPFile` | 8623-8880 | `src/botlib/aas/aas_map.c:8448` |
| `0x100085f0` | `sub_100085F0` | 8881-8887 | `src/botlib/interface/bot_interface.c:2679`, `src/botlib/interface/bot_interface.c:2679` |

### `be_aas_cluster.c`

Primary reconstruction: `src/botlib/aas/aas_cluster.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x10008620` | `AAS_RemoveClusterAreas` | 8888-8905 | - |
| `0x10008660` | `AAS_UpdatePortal` | 8906-8966 | - |
| `0x100087e0` | `AAS_FloodClusterAreas_r` | 8967-9043 | - |
| `0x100089e0` | `AAS_FloodClusterReachabilities` | 9044-9085 | - |
| `0x10008ac0` | `AAS_NumberClusterPortals` | 9086-9109 | - |
| `0x10008b40` | `AAS_FindClusters` | 9110-9154 | - |
| `0x10008c80` | `AAS_CreatePortals` | 9155-9189 | - |
| `0x10008d40` | `AAS_ConnectedAreas_r` | 9190-9237 | - |
| `0x10008e20` | `AAS_ConnectedAreas` | 9238-9267 | - |
| `0x10008eb0` | `AAS_FloodAreas_r` | 9268-9328 | `src/botlib/aas/aas_cluster.c:496` |
| `0x10008ff0` | `AAS_CheckAreaForPossiblePortals` | 9329-9614 | - |
| `0x10009570` | `AAS_FindPossiblePortals` | 9615-9629 | - |
| `0x100095c0` | `AAS_RemoveAllPortals` | 9630-9649 | - |
| `0x10009610` | `AAS_TestPortals` | 9650-9681 | - |
| `0x100096e0` | `AAS_InitClustering` | 9682-9743 | - |

### `be_aas_debug.c`

Primary reconstruction: `src/botlib/aas/aas_debug.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x10009860` | `AAS_ClearShownDebugLines` | 9744-9759 | - |
| `0x100098b0` | `AAS_DebugLine` | 9760-9782 | - |
| `0x10009950` | `AAS_DrawPermanentCross` | 9783-9808 | `src/botlib/aas/aas_debug.c:153` |
| `0x10009a10` | `?` | 9809-9901 | `src/botlib/aas/aas_debug.c:118`, `src/botlib/aas/aas_debug.c:203` |
| `0x10009cb0` | `?` | 9902-9991 | `src/botlib/aas/aas_debug.c:118`, `src/botlib/aas/aas_debug.c:256` |
| `0x10009ed0` | `?` | 9992-10047 | - |
| `0x1000a0a0` | `AAS_ShowArea` | 10048-10172 | `src/botlib/aas/aas_debug.c:329` |
| `0x1000a370` | `AAS_DrawCross` | 10173-10196 | - |
| `0x1000a400` | `AAS_PrintTravelType` | 10197-10202 | `src/botlib/aas/aas_debug.c:461` |
| `0x1000a420` | `AAS_DrawArrow` | 10203-10254 | - |
| `0x1000a5e0` | `?` | 10255-10303 | `src/botlib/aas/aas_debug.c:519` |
| `0x1000a810` | `?` | 10304-10343 | `src/botlib/aas/aas_debug.c:613` |

### `be_aas_entity.c`

Primary reconstruction: `src/botlib/aas/aas_map.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x1000a920` | `AAS_UpdateEntity` | 10344-10446 | `src/botlib/aas/aas_map.c:10106` |
| `0x1000abe0` | `AAS_EntityInfo` | 10447-10466 | - |
| `0x1000acb0` | `AAS_EntityOrigin` | 10467-10485 | - |
| `0x1000ad40` | `AAS_EntityModelindex` | 10486-10496 | - |
| `0x1000ad90` | `AAS_EntityRenderFX` | 10497-10508 | - |
| `0x1000ade0` | `AAS_EntityModelNum` | 10509-10520 | - |
| `0x1000ae30` | `AAS_OriginOfMoverWithModelNum` | 10521-10544 | - |
| `0x1000aea0` | `?` | 10545-10567 | - |
| `0x1000af30` | `AAS_EntityBSPData` | 10568-10590 | - |
| `0x1000afd0` | `AAS_DropToFloor` | 10591-10617 | - |
| `0x1000b090` | `AAS_ResetEntityLinks` | 10618-10636 | - |
| `0x1000b0e0` | `AAS_InvalidateEntities` | 10637-10656 | - |
| `0x1000b130` | `AAS_BestReachableLinkArea` | 10657-10691 | `src/botlib/aas/aas_reach.c:1193` |
| `0x1000b1b0` | `AAS_BestReachableEntityArea` | 10692-10698 | - |
| `0x1000b1f0` | `?` | 10699-10751 | - |
| `0x1000b300` | `AAS_BestReachableArea` | 10752-10867 | - |
| `0x1000b640` | `InFieldOfVision` | 10868-10933 | - |
| `0x1000b750` | `BotEntityVisible` | 10934-11039 | - |
| `0x1000baa0` | `sub_1000BAA0` | 11040-11067 | - |
| `0x1000bb30` | `AAS_NextBSPEntity` | 11068-11094 | - |

### `be_aas_file.c`

Primary reconstruction: `src/botlib/aas/aas_map.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x1000bba0` | `?` | 11095-11381 | - |
| `0x1000c490` | `AAS_DumpAASData` | 11382-11477 | - |
| `0x1000c670` | `AAS_LoadAASLump` | 11478-11503 | - |
| `0x1000c730` | `AAS_LoadAASFile` | 11504-11735 | - |
| `0x1000ce40` | `AAS_WriteAASLump` | 11736-11750 | - |
| `0x1000cee0` | `AAS_WriteAASFile` | 11751-11860 | - |
| `0x1000d340` | `?` | 11861-11902 | `src/botlib/aas/aas_sound.c:569`, `src/botlib/aas/aas_sound.h:103` |

### `be_aas_light.c`

Primary reconstruction: `src/botlib/aas/aas_sound.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x1000d450` | `sub_1000D450` | 11903-11920 | - |
| `0x1000d4a0` | `sub_1000D4A0` | 11921-11934 | - |
| `0x1000d4e0` | `sub_1000D4E0` | 11935-11968 | - |
| `0x1000d550` | `BotAddPointLight` | 11969-11998 | - |
| `0x1000d5f0` | `?` | 11999-12058 | - |
| `0x1000d770` | `AAS_PointLight` | 12059-12071 | - |

### `be_aas_main.c`

Primary reconstruction: `src/botlib/aas/aas_main.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x1000d7e0` | `AAS_Error` | 12072-12081 | `src/botlib/aas/aas_cluster.c:58` |
| `0x1000d830` | `AAS_StringFromIndex` | 12082-12103 | - |
| `0x1000d8d0` | `AAS_IndexFromString` | 12104-12131 | - |
| `0x1000d960` | `AAS_ModelFromIndex` | 12132-12138 | - |
| `0x1000d990` | `IndexFromModel` | 12139-12144 | - |
| `0x1000d9c0` | `AAS_SoundFromIndex` | 12145-12151 | - |
| `0x1000d9f0` | `AAS_IndexFromSound` | 12152-12157 | - |
| `0x1000da20` | `AAS_ImageFromIndex` | 12158-12164 | - |
| `0x1000da50` | `AAS_IndexFromImage` | 12165-12170 | - |
| `0x1000da80` | `sub_1000DA80` | 12171-12221 | - |
| `0x1000db40` | `sub_1000DB40` | 12222-12269 | - |
| `0x1000dbd0` | `sub_1000DBD0` | 12270-12287 | - |
| `0x1000dc20` | `sub_1000DC20` | 12288-12313 | - |
| `0x1000dcc0` | `sub_1000DCC0` | 12314-12344 | - |
| `0x1000dda0` | `AAS_PresenceTypeBoundingBox` | 12345-12390 | `src/botlib/aas/aas_map.c:559` |
| `0x1000dee0` | `AAS_Initialized` | 12391-12396 | - |
| `0x1000df00` | `AAS_SetInitialized` | 12397-12404 | - |
| `0x1000df30` | `AAS_ContinueInit` | 12405-12447 | - |
| `0x1000e010` | `AAS_StartFrame` | 12448-12488 | `src/botlib/aas/aas_main.c:219` |
| `0x1000e120` | `AAS_Time` | 12489-12494 | - |
| `0x1000e140` | `sub_1000E140` | 12495-12649 | - |
| `0x1000e430` | `sub_1000E430` | 12650-12859 | - |
| `0x1000e880` | `BotLibLoadMap` | 12860-13016 | `src/botlib/aas/aas_map.c:5945` |
| `0x1000ecd0` | `BotLoadMap` | 13017-13041 | - |
| `0x1000edc0` | `sub_1000EDC0` | 13042-13058 | `src/botlib/aas/aas_map.c:9237`, `src/botlib/interface/botlib_interface.c:212` |
| `0x1000ee30` | `AAS_Shutdown` | 13059-13077 | `src/botlib/aas/aas_map.c:9458`, `src/botlib/aas/aas_route.c:327` (+1) |

### `be_aas_move.c`

Primary reconstruction: `src/botlib/aas/aas_move.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x1000eeb0` | `AAS_OnGround` | 13078-13133 | - |
| `0x1000efc0` | `AAS_Swimming` | 13134-13149 | - |
| `0x1000f010` | `?` | 13150-13194 | `src/botlib/aas/aas_move.c:448` |
| `0x1000f130` | `?` | 13195-13234 | - |
| `0x1000f2c0` | `AAS_AgainstLadder` | 13235-13316 | - |
| `0x1000f4d0` | `AAS_WeaponJumpZVelocity` | 13317-13383 | - |
| `0x1000f750` | `?` | 13384-13389 | - |
| `0x1000f780` | `AAS_BFGJumpZVelocity` | 13390-13396 | - |
| `0x1000f7b0` | `AAS_ApplyFriction` | 13397-13437 | - |
| `0x1000f840` | `?` | 13438-13985 | `src/botlib/aas/aas_move.c:804`, `src/botlib/aas/aas_reach.c:2870` (+1) |
| `0x10010690` | `?` | 13986-14024 | - |
| `0x10010780` | `AAS_HorizontalVelocityForJump` | 14025-14060 | - |

### `be_aas_optimize.c`

Primary reconstruction: `src/botlib/aas/aas_optimize.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x10010860` | `AAS_KeepEdge` | 14061-14066 | - |
| `0x10010880` | `AAS_OptimizeEdge` | 14067-14121 | - |
| `0x100109e0` | `AAS_KeepFace` | 14122-14127 | - |
| `0x10010a00` | `AAS_OptimizeFace` | 14128-14176 | - |
| `0x10010b40` | `AAS_OptimizeArea` | 14177-14212 | - |
| `0x10010c10` | `AAS_OptimizeAlloc` | 14213-14236 | - |
| `0x10010d50` | `AAS_OptimizeStore` | 14237-14288 | - |
| `0x10010e90` | `AAS_Optimize` | 14289-14322 | `src/botlib/aas/aas_optimize.c:378` |

### `be_aas_reach.c`

Primary reconstruction: `src/botlib/aas/aas_reach.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x10010f60` | `?` | 14323-14338 | - |
| `0x10010fd0` | `AAS_ShutDownReachabilityHeap` | 14339-14345 | - |
| `0x10010ff0` | `allocated` | 14346-14362 | - |
| `0x10011040` | `AAS_AreaReachability` | 14363-14374 | - |
| `0x10011090` | `AAS_FaceArea` | 14375-14430 | - |
| `0x10011220` | `AAS_AreaVolume` | 14431-14473 | - |
| `0x10011360` | `AAS_AreaGroundFaceArea` | 14474-14504 | - |
| `0x100113f0` | `AAS_FaceCenter` | 14505-14540 | - |
| `0x10011520` | `AAS_FallDamageDistance` | 14541-14549 | - |
| `0x10011560` | `AAS_MaxJumpHeight` | 14550-14556 | - |
| `0x10011590` | `AAS_MaxJumpDistance` | 14557-14564 | - |
| `0x100115d0` | `AAS_AreaCrouch` | 14565-14571 | `src/botlib/aas/aas_map.c:569` |
| `0x10011610` | `AAS_AreaSwim` | 14572-14577 | - |
| `0x10011640` | `AAS_AreaLiquid` | 14578-14584 | - |
| `0x10011670` | `AAS_AreaGrounded` | 14585-14590 | `src/botlib/aas/aas_reach.c:3453` |
| `0x100116a0` | `AAS_AreaLadder` | 14591-14597 | - |
| `0x100116d0` | `sub_100116D0` | 14598-14606 | - |
| `0x10011700` | `AAS_ReachabilityExists` | 14607-14616 | - |
| `0x10011740` | `AAS_NearbySolidOrGap` | 14617-14644 | - |
| `0x10011860` | `AAS_Reachability_Swim` | 14645-14765 | - |
| `0x10011ae0` | `AAS_Reachability_EqualFloorHeight` | 14766-15081 | - |
| `0x10012200` | `AAS_Reachability_Step_Barrier_WaterJump_WalkOffLedge` | 15082-16283 | - |
| `0x10013ba0` | `VectorDistance` | 16284-16293 | - |
| `0x10013bf0` | `VectorBetweenVectors` | 16294-16317 | - |
| `0x10013c70` | `VectorMiddle` | 16318-16326 | - |
| `0x10013cc0` | `AAS_Reachability_Jump` | 16327-17190 | - |
| `0x10014e60` | `?` | 17191-17683 | - |
| `0x10015bb0` | `AAS_Reachability_Teleport` | 17684-18001 | - |
| `0x100160e0` | `AAS_Reachability_Elevator` | 18002-18658 | - |
| `0x10016ba0` | `AAS_Reachability_Grapple` | 18659-19188 | - |
| `0x10017350` | `AAS_SetWeaponJumpAreaFlags` | 19189-20276 | - |
| `0x10017ca0` | `AAS_Reachability_WeaponJump` | 20277-20553 | - |
| `0x100181d0` | `AAS_Reachability_WalkOffLedge` | 20554-20840 | - |
| `0x100187e0` | `AAS_StoreReachability` | 20841-20894 | - |
| `0x10018920` | `AAS_ContinueInitReachability` | 20895-21059 | `src/q2bridge/bridge_config.c:128` |
| `0x10018c70` | `AAS_InitReachability` | 21060-21085 | - |

### `be_aas_route.c`

Primary reconstruction: `src/botlib/aas/aas_route.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x10018d00` | `AAS_InitTravelFlagFromType` | 21086-21094 | - |
| `0x10018dc0` | `AAS_TravelFlagForType` | 21095-21104 | - |
| `0x10018df0` | `AAS_CreateReversedReachability` | 21105-21157 | - |
| `0x10018f50` | `AAS_AreaTravelTime` | 21158-21186 | - |
| `0x10019010` | `AAS_CalculateAreaTravelTimes` | 21187-21297 | - |
| `0x10019230` | `AAS_AllocRoutingCache` | 21298-21303 | - |
| `0x10019260` | `AAS_FreeRoutingCache` | 21304-21309 | - |
| `0x10019280` | `AAS_FreeAllClusterAreaCache` | 21310-21356 | - |
| `0x10019350` | `AAS_InitClusterAreaCache` | 21357-21395 | - |
| `0x100193e0` | `AAS_FreeAllPortalCache` | 21396-21427 | - |
| `0x10019470` | `AAS_InitPortalCache` | 21428-21435 | - |
| `0x100194a0` | `AAS_InitRoutingUpdate` | 21436-21455 | - |
| `0x10019520` | `AAS_InitRouting` | 21456-21467 | - |
| `0x10019550` | `?` | 21468-21475 | `src/botlib/aas/aas_map.c:10111`, `src/botlib/aas/aas_route.c:326` (+2) |
| `0x10019570` | `?` | 21476-21585 | - |
| `0x10019700` | `AAS_UpdateAreaRoutingCache` | 21586-21728 | - |
| `0x10019a90` | `AAS_GetAreaRoutingCache` | 21729-21780 | - |
| `0x10019c00` | `AAS_UpdatePortalRoutingCache` | 21781-21903 | - |
| `0x10019eb0` | `AAS_GetPortalRoutingCache` | 21904-21944 | - |
| `0x10019fa0` | `AAS_AreaTravelTimeToGoalArea` | 21945-22095 | `src/botlib/aas/aas_route.c:2178`, `src/botlib/aas/aas_route.c:3106` |
| `0x1001a2e0` | `AAS_ReachabilityFromNum` | 22096-22109 | - |
| `0x1001a370` | `AAS_NextAreaReachability` | 22110-22135 | - |
| `0x1001a410` | `AAS_RandomGoalArea` | 22136-22216 | - |
| `0x1001a610` | `AAS_RoutingInfo` | 22217-22224 | - |

### `be_aas_routealt.c`

Primary reconstruction: `src/botlib/aas/aas_route.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x1001a650` | `AAS_AltRoutingFloodCluster_r` | 22225-22261 | - |
| `0x1001a720` | `?` | 22262-22427 | - |
| `0x1001ab80` | `sub_1001AB80` | 22428-22447 | `src/botlib/aas/aas_route.c:1158` |

### `be_aas_sample.c`

Primary reconstruction: `src/botlib/aas/aas_map.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x1001ac00` | `AAS_InitAASLinkHeap` | 22448-22493 | - |
| `0x1001ad10` | `AAS_FreeAASLinkHeap` | 22494-22507 | - |
| `0x1001ad50` | `AAS_AllocAASLink` | 22508-22525 | - |
| `0x1001ada0` | `AAS_DeAllocAASLink` | 22526-22541 | - |
| `0x1001ade0` | `AAS_InitAASLinkedEntities` | 22542-22559 | - |
| `0x1001ae30` | `AAS_FreeAASLinkedEntities` | 22560-22571 | - |
| `0x1001ae60` | `AAS_PointAreaNum` | 22572-22609 | - |
| `0x1001af00` | `?` | 22610-22620 | - |
| `0x1001af50` | `AAS_AreaPresenceType` | 22621-22632 | - |
| `0x1001afa0` | `AAS_PointContents` | 22633-22647 | - |
| `0x1001aff0` | `?` | 22648-22730 | - |
| `0x1001b130` | `AAS_AreaEntityCollision` | 22731-22774 | - |
| `0x1001b260` | `?` | 22775-23339 | - |
| `0x1001ba00` | `?` | 23340-23500 | - |
| `0x1001bd40` | `?` | 23501-23564 | - |
| `0x1001bf00` | `AAS_PointInsideFace` | 23565-23627 | - |
| `0x1001c0b0` | `?` | 23628-23677 | - |
| `0x1001c1c0` | `AAS_FacePlane` | 23678-23690 | - |
| `0x1001c210` | `?` | 23691-23721 | - |
| `0x1001c2e0` | `sub_1001C2E0` | 23722-23787 | - |
| `0x1001c3f0` | `AAS_UnlinkFromAreas` | 23788-23817 | `src/botlib/aas/aas_map.c:10107` |
| `0x1001c460` | `AAS_AASLinkEntity` | 23818-23913 | `src/botlib/aas/aas_reach.c:1192` |
| `0x1001c620` | `AAS_LinkEntityClientBBox` | 23914-23933 | `src/botlib/aas/aas_map.c:10108`, `src/q2bridge/aas_translation.h:72` |
| `0x1001c6c0` | `AAS_PlaneFromNum` | 23934-23943 | - |

### `be_aas_sound.c`

Primary reconstruction: `src/botlib/aas/aas_sound.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x1001c6f0` | `sub_1001C6F0` | 23944-23969 | - |
| `0x1001c760` | `sub_1001C760` | 23970-24088 | `src/botlib/aas/aas_sound.c:168` |
| `0x1001cab0` | `sub_1001CAB0` | 24089-24136 | - |
| `0x1001cbe0` | `sub_1001CBE0` | 24137-24152 | - |
| `0x1001cc10` | `sub_1001CC10` | 24153-24167 | - |
| `0x1001cc50` | `sub_1001CC50` | 24168-24201 | - |
| `0x1001ccc0` | `sub_1001CCC0` | 24202-24223 | - |
| `0x1001cd10` | `sub_1001CD10` | 24224-24257 | - |
| `0x1001cd80` | `sub_1001CD80` | 24258-24279 | - |
| `0x1001cdd0` | `sub_1001CDD0` | 24280-24295 | - |
| `0x1001ce20` | `sub_1001CE20` | 24296-24355 | - |
| `0x1001cfa0` | `sub_1001CFA0` | 24356-24402 | - |
| `0x1001d040` | `sub_1001D040` | 24403-24412 | - |
| `0x1001d070` | `sub_1001D070` | 24413-24418 | - |
| `0x1001d0a0` | `?` | 24419-24436 | - |
| `0x1001d140` | `sub_1001D140` | 24437-24486 | - |
| `0x1001d260` | `sub_1001D260` | 24487-24494 | - |
| `0x1001d290` | `sub_1001D290` | 24495-24501 | `src/botlib/aas/aas_sound.c:621`, `src/botlib/interface/botlib_interface.c:297` |

### `be_ai2_dmnet.c`

Primary reconstruction: `src/botlib/interface/bot_interface.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x1001d2b0` | `BotResetNodeSwitches` | 24502-24508 | - |
| `0x1001d2d0` | `BotDumpNodeSwitches` | 24509-24569 | - |
| `0x1001d3a0` | `BotRecordNodeSwitch` | 24570-24583 | - |
| `0x1001d420` | `?` | 24584-24677 | - |
| `0x1001d760` | `?` | 24678-25528 | - |
| `0x1001eae0` | `AIEnter_Intermission` | 25529-25543 | - |
| `0x1001eb50` | `AINode_Intermission` | 25544-25564 | - |
| `0x1001ebd0` | `AIEnter_Observer` | 25565-25574 | - |
| `0x1001ec10` | `AINode_Observer` | 25575-25584 | - |
| `0x1001ec50` | `AIEnter_Stand` | 25585-25593 | - |
| `0x1001ec90` | `AINode_Stand` | 25594-25627 | - |
| `0x1001ed80` | `AIEnter_Respawn` | 25628-25654 | - |
| `0x1001ee40` | `AINode_Respawn` | 25655-25681 | - |
| `0x1001ef00` | `AIEnter_Seek_ActivateEntity` | 25682-25689 | - |
| `0x1001ef40` | `AINode_Seek_ActivateEntity` | 25690-25807 | `src/botlib/interface/bot_interface.c:7233` |
| `0x1001f210` | `AIEnter_Seek_NBG` | 25808-25823 | - |
| `0x1001f290` | `AINode_Seek_NBG` | 25824-26021 | - |
| `0x1001f6e0` | `AIEnter_Seek_LTG` | 26022-26037 | - |
| `0x1001f760` | `AINode_Seek_LTG` | 26038-26258 | - |
| `0x1001fcf0` | `AIEnter_Battle_Fight` | 26259-26267 | - |
| `0x1001fd30` | `AINode_Battle_Fight` | 26268-26378 | - |
| `0x10020050` | `AIEnter_Battle_Chase` | 26379-26388 | - |
| `0x100200a0` | `AINode_Battle_Chase` | 26389-26552 | - |
| `0x100205c0` | `AIEnter_Battle_Retreat` | 26553-26560 | - |
| `0x10020600` | `AINode_Battle_Retreat` | 26561-26715 | - |
| `0x10020ad0` | `AIEnter_Battle_NBG` | 26716-26724 | - |
| `0x10020b10` | `AINode_Battle_NBG` | 26725-26868 | - |

### `be_ai2_dmq2.c`

Primary reconstruction: `src/botlib/ai/ai_dm.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x10020ed0` | `BotEntityInfo` | 26869-26914 | - |
| `0x10020fe0` | `sub_10020FE0` | 26915-26924 | - |
| `0x10021020` | `BotUpdateInventory` | 26925-27036 | `src/botlib/interface/bot_interface.c:861` |
| `0x10021290` | `BotUpdateBattleInventory` | 27037-27126 | `src/botlib/interface/bot_interface.c:958` |
| `0x100214e0` | `sub_100214E0` | 27127-27132 | - |
| `0x10021500` | `BotBattleUseItems` | 27133-27157 | `src/botlib/interface/bot_interface.c:1046` |
| `0x100215e0` | `sub_100215E0` | 27158-27170 | `src/botlib/interface/bot_interface.c:1089` |
| `0x10021650` | `BotCTFCarryingFlag` | 27171-27191 | `src/botlib/interface/bot_interface.c:1119` |
| `0x100216a0` | `BotIsDead` | 27192-27203 | - |
| `0x100216d0` | `BotIsObserver` | 27204-27212 | - |
| `0x100216f0` | `BotIntermission` | 27213-27221 | - |
| `0x10021710` | `sub_10021710` | 27222-27237 | `src/botlib/interface/bot_interface.c:368` |
| `0x10021780` | `EntityIsShooting` | 27238-27249 | `src/botlib/interface/bot_interface.c:388` |
| `0x100217c0` | `stristr` | 27250-27299 | - |
| `0x10021860` | `EasyClientName` | 27300-27476 | `src/botlib/interface/bot_interface.c:4987` |
| `0x10021a90` | `BotCreateWayPoint` | 27477-27523 | - |
| `0x10021b50` | `BotFindWayPoint` | 27524-27540 | - |
| `0x10021b90` | `BotFreeWaypoints` | 27541-27557 | - |
| `0x10021bc0` | `BotValidChatPosition` | 27558-27609 | - |
| `0x10021d80` | `BotChat_EnterGame` | 27610-27654 | - |
| `0x10021e90` | `BotChat_ExitGame` | 27655-27693 | - |
| `0x10021f80` | `BotChat_StartLevel` | 27694-27733 | - |
| `0x10022070` | `BotChat_EndLevel` | 27734-27772 | - |
| `0x10022160` | `BotChat_Death` | 27773-27843 | - |
| `0x100222e0` | `BotChat_Kill` | 27844-27916 | `src/botlib/interface/bot_interface.c:5512` |
| `0x10022470` | `BotChat_Random` | 27917-27999 | `src/botlib/interface/bot_interface.c:5362` |
| `0x10022650` | `BotChatTime` | 28000-28009 | - |
| `0x100226c0` | `BotAggression` | 28010-28056 | `src/botlib/interface/bot_interface.c:1153` |
| `0x100228c0` | `BotWantsToRetreat` | 28057-28077 | `src/botlib/interface/bot_interface.c:1252` |
| `0x10022930` | `BotWantsToChase` | 28078-28092 | `src/botlib/interface/bot_interface.c:1279` |
| `0x10022970` | `BotWantsToHelp` | 28093-28099 | - |
| `0x10022990` | `BotCanAndWantsToRocketJump` | 28100-28133 | `src/botlib/interface/bot_interface.c:1297`, `src/botlib/interface/bot_interface.c:6060` |
| `0x10022a60` | `BotRoamGoal` | 28134-28275 | - |
| `0x10022e10` | `BotAttackMove` | 28276-28546 | - |
| `0x10023510` | `BotCTFTeam` | 28547-28555 | - |
| `0x10023550` | `BotSameTeam` | 28556-28697 | `src/botlib/interface/bot_interface.c:450` |
| `0x100238f0` | `BotNumTeamMates` | 28698-28731 | - |
| `0x10023970` | `BotFindEnemy` | 28732-28924 | `src/botlib/interface/bot_interface.c:549` |
| `0x10023ce0` | `BotAimAtEnemy` | 28925-29203 | - |
| `0x10024590` | `BotCheckAttack` | 29204-29357 | - |
| `0x10024a10` | `BotEntityToActivate` | 29358-29886 | - |
| `0x10024fd0` | `BotSetMovedir` | 29887-29905 | - |
| `0x10025070` | `?` | 29906-30178 | - |
| `0x10025560` | `BotAIBlocked` | 30179-30678 | - |
| `0x100262c0` | `sub_100262C0` | 30679-30716 | `src/botlib/interface/bot_interface.c:6077` |
| `0x100263d0` | `BotCTFRetreatGoals` | 30717-30734 | - |
| `0x10026440` | `BotCTFSeekGoals` | 30735-30819 | `src/botlib/interface/bot_interface.c:4439` |
| `0x10026690` | `TeamPlayIsOn` | 30820-30845 | - |
| `0x10026700` | `BotGetItemTeamGoal` | 30846-30872 | - |
| `0x10026770` | `BotGetMessageTeamGoal` | 30873-30888 | `src/botlib/interface/bot_interface.c:4100` |
| `0x100267e0` | `BotGetTime` | 30889-30926 | - |
| `0x100268d0` | `FindClientByName` | 30927-30970 | - |
| `0x10026990` | `BotGetPatrolWaypoints` | 30971-31060 | `src/botlib/interface/bot_interface.c:4814` |
| `0x10026be0` | `BotAddressedToBot` | 31061-31139 | `src/botlib/interface/bot_interface.c:3776` |
| `0x10026e40` | `BotGPSToPosition` | 31140-31197 | - |
| `0x10026f10` | `BotMatchMessage` | 31198-31835 | - |
| `0x10028650` | `BotCheckConsoleMessages` | 31836-32035 | - |
| `0x100289a0` | `sub_100289A0` | 32036-32056 | - |
| `0x10028a40` | `sub_10028A40` | 32057-32064 | - |
| `0x10028a70` | `BotDeathmatchAI` | 32065-32136 | - |
| `0x10028c30` | `BotSetupDeathmatchAI` | 32137-32181 | `src/botlib/interface/bot_interface.c:2476`, `src/botlib/interface/bot_interface.c:2506` (+1) |
| `0x10028e80` | `?` | 32182-32187 | - |

### `be_ai2_main.c`

Primary reconstruction: `src/botlib/interface/bot_interface.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x10028ea0` | `ClientFromName` | 32188-32244 | - |
| `0x10028f30` | `ClientName` | 32245-32254 | - |
| `0x10028f80` | `ClientSkin` | 32255-32265 | - |
| `0x10028fd0` | `NumBots` | 32266-32272 | - |
| `0x10028ff0` | `AngleDifference` | 32273-32301 | - |
| `0x10029040` | `BotChangeViewAngle` | 32302-32367 | - |
| `0x10029150` | `BotChangeViewAngles` | 32368-32447 | `src/botlib/interface/bot_interface.c:8019` |
| `0x100292e0` | `sub_100292E0` | 32448-32463 | - |
| `0x10029320` | `Export_BotAIFrame` | 32464-32480 | - |
| `0x100293a0` | `?` | 32481-32493 | - |
| `0x10029480` | `BotSetupClient` | 32494-32547 | `src/botlib/ai/ai_character.c:20` |
| `0x10029690` | `BotShutdownClient` | 32548-32576 | - |
| `0x100297b0` | `BotMoveClient` | 32577-32599 | - |
| `0x10029880` | `BotUpdateClient` | 32600-32625 | - |
| `0x10029920` | `BotClientSettings` | 32626-32632 | - |
| `0x10029960` | `BotConsoleMessage` | 32633-32646 | - |
| `0x100299d0` | `BotSettings` | 32647-32660 | - |
| `0x10029a40` | `BotResetState` | 32661-32699 | `src/botlib/interface/bot_interface.c:2609`, `src/q2bridge/update_translator.c:454` (+1) |
| `0x10029c10` | `sub_10029C10` | 32700-32725 | `src/botlib/interface/bot_interface.c:2609` |
| `0x10029c90` | `?` | 32726-32753 | `src/botlib/interface/botlib_interface.c:404`, `src/q2bridge/update_translator.h:42` |
| `0x10029da0` | `BotShutdownLibrary` | 32754-32776 | `src/q2bridge/update_translator.h:42` |

### `be_ai_char.c`

Primary reconstruction: `src/botlib/ai_character/bot_character.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x10029e10` | `BotDumpCharacter` | 32777-33189 | - |
| `0x1002a590` | `sub_1002A590` | 33190-33196 | - |
| `0x1002a5b0` | `CheckCharacteristicIndex` | 33197-33211 | `src/botlib/ai/ai_character.c:27` |
| `0x1002a620` | `Characteristic_Float` | 33212-33230 | - |
| `0x1002a690` | `Characteristic_BFloat` | 33231-33264 | - |
| `0x1002a730` | `Characteristic_Integer` | 33265-33284 | - |
| `0x1002a7a0` | `Characteristic_BInteger` | 33285-33303 | - |
| `0x1002a810` | `Characteristic_String` | 33304-33317 | `src/botlib/ai/ai_character.c:28` |

### `be_ai_chat.c`

Primary reconstruction: `src/botlib/ai_chat/ai_chat.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x1002a880` | `InitConsoleMessageHeap` | 33318-33359 | - |
| `0x1002a9a0` | `AllocConsoleMessage` | 33360-33374 | - |
| `0x1002a9e0` | `FreeConsoleMessage` | 33375-33388 | - |
| `0x1002aa20` | `BotRemoveConsoleMessage` | 33389-33412 | - |
| `0x1002aab0` | `BotQueueConsoleMessage` | 33413-33443 | - |
| `0x1002ab90` | `BotNextConsoleMessage` | 33444-33450 | - |
| `0x1002abb0` | `BotNumConsoleMessages` | 33451-33457 | - |
| `0x1002abd0` | `IsWhiteSpace` | 33458-33470 | - |
| `0x1002ac50` | `UnifyWhiteSpaces` | 33471-33523 | - |
| `0x1002acf0` | `StringContains` | 33524-33597 | - |
| `0x1002ae00` | `StringContainsWord` | 33598-33690 | - |
| `0x1002af30` | `StringReplaceWords` | 33691-33806 | - |
| `0x1002b070` | `BotDumpSynonymList` | 33807-33832 | - |
| `0x1002b110` | `BotLoadSynonyms` | 33833-34149 | - |
| `0x1002b7c0` | `BotReplaceSynonyms` | 34150-34159 | - |
| `0x1002b830` | `BotReplaceWeightedSynonyms` | 34160-34196 | - |
| `0x1002b900` | `BotDumpRandomStringList` | 34197-34223 | - |
| `0x1002b990` | `?` | 34224-34430 | - |
| `0x1002bdd0` | `RandomString` | 34431-34500 | - |
| `0x1002bea0` | `?` | 34501-34542 | - |
| `0x1002bfb0` | `BotFreeMatchPieces` | 34543-34571 | - |
| `0x1002c020` | `BotLoadMatchPieces` | 34572-34738 | - |
| `0x1002c3d0` | `BotFreeMatchTemplates` | 34739-34755 | - |
| `0x1002c410` | `BotLoadMatchTemplates` | 34756-34905 | - |
| `0x1002c800` | `StringsMatch` | 34906-34998 | - |
| `0x1002c930` | `BotFindMatch` | 34999-35077 | - |
| `0x1002ca20` | `BotMatchVariable` | 35078-35098 | - |
| `0x1002cac0` | `BotFindStringInList` | 35099-35152 | - |
| `0x1002cb40` | `BotCheckChatMessageIntegrety` | 35153-35254 | `src/botlib/ai_chat/ai_chat.c:1798` |
| `0x1002ccf0` | `BotCheckReplyChatIntegrety` | 35255-35277 | - |
| `0x1002cd60` | `BotCheckInitialChatIntegrety` | 35278-35299 | - |
| `0x1002cdd0` | `BotLoadChatMessage` | 35300-35393 | - |
| `0x1002cf40` | `?` | 35394-35483 | - |
| `0x1002d1b0` | `BotFreeReplyChat` | 35484-35539 | - |
| `0x1002d270` | `?` | 35540-35792 | - |
| `0x1002d7e0` | `?` | 35793-36231 | - |
| `0x1002df70` | `BotFreeChatFile` | 36232-36244 | - |
| `0x1002dfb0` | `BotFreeChatState` | 36245-36257 | - |
| `0x1002dff0` | `BotLoadChatFile` | 36258-36272 | - |
| `0x1002e060` | `BotConstructChatMessage` | 36273-36470 | `src/botlib/ai_chat/ai_chat.c:7741` |
| `0x1002e3b0` | `BotChooseInitialChatMessage` | 36471-36577 | - |
| `0x1002e510` | `BotInitialChat` | 36578-36623 | - |
| `0x1002e5d0` | `?` | 36624-36702 | - |
| `0x1002e7d0` | `BotReplyChat` | 36703-36837 | - |
| `0x1002ea50` | `BotChatLength` | 36838-36854 | - |
| `0x1002ea80` | `BotEnterChat` | 36855-36884 | - |
| `0x1002eaf0` | `BotSetChatGender` | 36885-36900 | - |
| `0x1002eb30` | `BotSetChatName` | 36901-36912 | - |
| `0x1002eb70` | `BotResetChatAI` | 36913-36921 | - |
| `0x1002ebb0` | `BotSetupChatAI` | 36922-36940 | - |
| `0x1002ec80` | `BotShutdownChatAI` | 36941-36977 | - |

### `be_ai_goal.c`

Primary reconstruction: `src/botlib/ai_goal/bot_goal.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x1002ed20` | `LoadItemConfig` | 36978-37109 | - |
| `0x1002f100` | `ItemWeightIndex` | 37110-37139 | - |
| `0x1002f1a0` | `InitLevelItemHeap` | 37140-37180 | - |
| `0x1002f270` | `AllocLevelItem` | 37181-37194 | - |
| `0x1002f2b0` | `FreeLevelItem` | 37195-37202 | - |
| `0x1002f2e0` | `AddLevelItemToList` | 37203-37216 | - |
| `0x1002f320` | `RemoveLevelItemFromList` | 37217-37234 | - |
| `0x1002f360` | `BotInitLevelItems` | 37235-37421 | - |
| `0x1002f6a0` | `BotGoalName` | 37422-37435 | - |
| `0x1002f6f0` | `BotResetAvoidGoals` | 37436-37444 | - |
| `0x1002f730` | `BotDumpAvoidGoals` | 37445-37476 | - |
| `0x1002f7b0` | `BotAddToAvoidGoals` | 37477-37503 | - |
| `0x1002f820` | `BotAvoidGoalTime` | 37504-37529 | - |
| `0x1002f890` | `BotGetLevelItemGoal` | 37530-37559 | `src/botlib/ai_goal/bot_goal.c:2512` |
| `0x1002fa20` | `BotUpdateEntityItems` | 37560-37781 | - |
| `0x1002fd40` | `BotDumpGoalStack` | 37782-37798 | - |
| `0x1002fd90` | `BotPushGoal` | 37799-37813 | - |
| `0x1002fe00` | `BotPopGoal` | 37814-37826 | - |
| `0x1002fe30` | `BotEmptyGoalStack` | 37827-37834 | - |
| `0x1002fe50` | `BotGetTopGoal` | 37835-37845 | - |
| `0x1002fe80` | `BotGetSecondGoal` | 37846-37856 | - |
| `0x1002feb0` | `BotChooseLTGItem` | 37857-38033 | - |
| `0x10030260` | `BotChooseNBGItem` | 38034-38233 | - |
| `0x10030600` | `BotTouchingGoal` | 38234-38298 | - |
| `0x10030770` | `?` | 38299-38347 | - |
| `0x100308d0` | `BotLoadItemWeights` | 38348-38367 | - |
| `0x10030950` | `BotFreeItemWeights` | 38368-38384 | - |
| `0x10030990` | `BotResetGoalState` | 38385-38394 | - |
| `0x100309d0` | `BotSetupGoalAI` | 38395-38407 | - |
| `0x10030a20` | `BotShutdownGoalAI` | 38408-38420 | - |

### `be_ai_move.c`

Primary reconstruction: `src/botlib/ai_move/bot_move.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x10030a50` | `AngleDiff` | 38421-38449 | - |
| `0x10030aa0` | `BotReachabilityArea` | 38450-38552 | - |
| `0x10030d00` | `BotOnMover` | 38553-38629 | - |
| `0x10030f10` | `MoverDown` | 38630-38662 | - |
| `0x10030fe0` | `BotValidTravel` | 38663-38671 | - |
| `0x10031010` | `BotAddToAvoidReach` | 38672-38724 | - |
| `0x100310e0` | `BotGetReachabilityToGoal` | 38725-38785 | `src/botlib/aas/aas_route.c:2352` |
| `0x10031270` | `BotMovementViewTarget` | 38786-38816 | `src/botlib/ai_move/bot_move.c:440` |
| `0x10031380` | `MoverBottomCenter` | 38817-38844 | - |
| `0x10031450` | `BotGapDistance` | 38845-38914 | - |
| `0x10031650` | `BotCheckBarrierJump` | 38915-39010 | - |
| `0x100318d0` | `BotSwimInDirection` | 39011-39024 | - |
| `0x10031940` | `BotWalkInDirection` | 39025-39133 | - |
| `0x10031be0` | `BotMoveInDirection` | 39134-39144 | - |
| `0x10031c30` | `?` | 39145-39179 | - |
| `0x10031d10` | `BotCheckBlocked` | 39180-39221 | - |
| `0x10031e20` | `BotClearMoveResult` | 39222-39229 | - |
| `0x10031e50` | `BotTravel_Walk` | 39230-39283 | - |
| `0x10031fe0` | `BotFinishTravel_Walk` | 39284-39314 | - |
| `0x100320c0` | `BotTravel_Crouch` | 39315-39339 | - |
| `0x10032190` | `BotTravel_BarrierJump` | 39340-39382 | - |
| `0x100322c0` | `BotFinishTravel_BarrierJump` | 39383-39424 | - |
| `0x100323e0` | `BotTravel_Swim` | 39425-39454 | - |
| `0x100324c0` | `BotTravel_WaterJump` | 39455-39498 | - |
| `0x10032620` | `BotFinishTravel_WaterJump` | 39499-39553 | - |
| `0x100327f0` | `BotTravel_WalkOffLedge` | 39554-39631 | - |
| `0x10032a00` | `BotFinishTravel_WalkOffLedge` | 39632-39657 | - |
| `0x10032ae0` | `BotTravel_Jump` | 39658-39795 | - |
| `0x10032e80` | `BotFinishTravel_Jump` | 39796-39846 | - |
| `0x10032fc0` | `BotTravel_Ladder` | 39847-39879 | - |
| `0x100330e0` | `BotTravel_Teleport` | 39880-39933 | - |
| `0x10033210` | `BotTravel_Elevator` | 39934-40149 | - |
| `0x10033790` | `BotFinishTravel_Elevator` | 40150-40190 | - |
| `0x100338a0` | `GrappleState` | 40191-40264 | - |
| `0x10033a70` | `BotResetGrapple` | 40265-40292 | - |
| `0x10033b00` | `BotTravel_Grapple` | 40293-40472 | - |
| `0x10033ec0` | `BotTravel_RocketJump` | 40473-40529 | - |
| `0x10034070` | `sub_10034070` | 40530-40539 | - |
| `0x100340b0` | `BotFinishTravel_WeaponJump` | 40540-40562 | - |
| `0x10034170` | `BotReachabilityTime` | 40563-40604 | - |
| `0x10034210` | `BotMoveInGoalArea` | 40605-40668 | - |
| `0x100343a0` | `?` | 40669-41011 | - |
| `0x10034af0` | `BotResetAvoidReach` | 41012-41020 | `src/botlib/interface/bot_interface.c:7139` |
| `0x10034b20` | `BotResetLastAvoidReach` | 41021-41048 | `src/botlib/interface/bot_interface.c:6994` |
| `0x10034b90` | `BotResetMoveState` | 41049-41055 | - |

### `be_ai_weap.c`

Primary reconstruction: `src/botlib/ai_weapon/bot_weapon.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x10034bb0` | `LoadWeaponConfig` | 41056-41347 | - |
| `0x10035280` | `WeaponWeightIndex` | 41348-41370 | - |
| `0x10035300` | `BotFreeWeaponWeights` | 41371-41386 | - |
| `0x10035340` | `BotLoadWeaponWeights` | 41387-41407 | - |
| `0x100353c0` | `sub_100353C0` | 41408-41432 | - |
| `0x10035430` | `sub_10035430` | 41433-41457 | - |
| `0x100354b0` | `sub_100354B0` | 41458-41473 | `src/botlib/ai_weapon/bot_weapon.c:829` |
| `0x10035500` | `BotChooseBestFightWeapon` | 41474-41526 | - |
| `0x10035640` | `BotResetWeaponState` | 41527-41537 | - |
| `0x10035680` | `BotSetupWeaponAI` | 41538-41551 | - |
| `0x100356d0` | `BotShutdownWeaponAI` | 41552-41563 | - |

### `be_ai_weight.c`

Primary reconstruction: `src/botlib/ai_weight/ai_weight.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x10035700` | `ReadValue` | 41564-41627 | - |
| `0x10035820` | `ReadFuzzyWeight` | 41628-41684 | - |
| `0x10035960` | `FreeFuzzySeperators_r` | 41685-41706 | - |
| `0x100359b0` | `FreeWeightConfig2` | 41707-41729 | - |
| `0x10035a20` | `ReadFuzzySeperators_r` | 41730-42062 | - |
| `0x10035fa0` | `ReadWeightConfig` | 42063-42354 | - |
| `0x10036570` | `WriteFuzzyWeight` | 42355-42392 | - |
| `0x10036690` | `WriteFuzzySeperators_r` | 42393-42474 | - |
| `0x100368b0` | `?` | 42475-42520 | - |
| `0x100369c0` | `FindFuzzyWeight` | 42521-42577 | - |
| `0x10036a40` | `FuzzyWeight_r` | 42578-42628 | - |
| `0x10036b10` | `FuzzyWeightUndecided_r` | 42629-42702 | - |
| `0x10036c70` | `FuzzyWeight` | 42703-42708 | - |
| `0x10036ca0` | `FuzzyWeightUndecided` | 42709-42715 | - |
| `0x10036cd0` | `EvolveFuzzySeperator_r` | 42716-42780 | - |
| `0x10036df0` | `EvolveWeightConfig` | 42781-42804 | - |
| `0x10036eb0` | `?` | 42805-42883 | - |
| `0x10036f90` | `InterbreedFuzzySeperator_r` | 42884-42918 | - |
| `0x10037020` | `?` | 42919-42942 | - |

### `be_ea.c`

Primary reconstruction: `src/botlib/ea/ea_main.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x10037090` | `EA_Say` | 42943-42948 | - |
| `0x100370c0` | `EA_SayTeam` | 42949-42955 | - |
| `0x100370f0` | `EA_UseItem` | 42956-42961 | - |
| `0x10037120` | `EA_DropItem` | 42962-42968 | - |
| `0x10037150` | `EA_UseInv` | 42969-42974 | - |
| `0x10037180` | `EA_DropInv` | 42975-42981 | - |
| `0x100371b0` | `sub_100371B0` | 42982-42990 | - |
| `0x10037200` | `EA_Command` | 42991-43029 | - |
| `0x100372c0` | `EA_Attack` | 43030-43038 | - |
| `0x100372f0` | `EA_Use` | 43039-43046 | - |
| `0x10037320` | `EA_Respawn` | 43047-43055 | - |
| `0x10037350` | `EA_Jump` | 43056-43072 | - |
| `0x10037390` | `EA_DelayedJump` | 43073-43089 | `src/q2bridge/botlib.h:82` |
| `0x100373d0` | `EA_Crouch` | 43090-43097 | - |
| `0x10037400` | `EA_MoveUp` | 43098-43106 | - |
| `0x10037430` | `EA_MoveDown` | 43107-43114 | - |
| `0x10037460` | `EA_MoveForward` | 43115-43123 | - |
| `0x10037490` | `EA_MoveBack` | 43124-43131 | - |
| `0x100374c0` | `EA_MoveLeft` | 43132-43142 | `src/q2bridge/botlib.h:84` |
| `0x100374f0` | `EA_MoveRight` | 43143-43152 | `src/q2bridge/botlib.h:81` |
| `0x10037520` | `EA_Move` | 43153-43186 | - |
| `0x100375a0` | `EA_View` | 43187-43196 | - |
| `0x100375e0` | `?` | 43197-43217 | - |
| `0x10037660` | `EA_Setup` | 43218-43226 | `src/botlib/interface/botlib_interface.c:404` |
| `0x10037690` | `EA_Shutdown` | 43227-43233 | - |

### `be_interface.c`

Primary reconstruction: `src/botlib/interface/botlib_interface.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x100376b0` | `sub_100376B0` | 43234-43312 | `src/botlib/common/l_crc.c:227`, `src/botlib/common/l_crc.h:17` (+1) |
| `0x100377e0` | `sub_100377E0` | 43313-43327 | `src/botlib/common/l_crc.c:29`, `src/botlib/common/l_crc.c:223` |
| `0x10037820` | `sub_10037820` | 43328-43336 | - |
| `0x10037850` | `sub_10037850` | 43337-43344 | `src/botlib/interface/bot_interface.c:2680` |
| `0x10037880` | `?` | 43345-43355 | - |
| `0x100378c0` | `Sys_MilliSeconds` | 43356-43366 | - |
| `0x10037900` | `ValidClientNumber` | 43367-43379 | - |
| `0x10037950` | `ValidEntityNumber` | 43380-43391 | - |
| `0x100379a0` | `BotLibSetup` | 43392-43401 | - |
| `0x100379e0` | `Export_BotVersion` | 43402-43407 | - |
| `0x10037a00` | `BotSetupMoveAI` | 43408-43433 | - |
| `0x10037bb0` | `Export_BotSetupLibrary` | 43434-43470 | - |
| `0x10037cf0` | `Export_BotShutdownLibrary` | 43471-43496 | - |
| `0x10037da0` | `Export_BotLibVarSet` | 43497-43504 | - |
| `0x10037dd0` | `Export_BotDefine` | 43505-43514 | - |
| `0x10037e10` | `Export_BotLoadMap` | 43515-43535 | - |
| `0x10037f00` | `Export_BotSetupClient` | 43536-43555 | - |
| `0x10037f70` | `Export_BotShutdownClient` | 43556-43568 | - |
| `0x10037fe0` | `Export_BotMoveClient` | 43569-43584 | - |
| `0x10038070` | `Export_BotClientSettings` | 43585-43598 | - |
| `0x100380e0` | `Export_BotSettings` | 43599-43611 | - |
| `0x10038150` | `Export_BotLibStartFrame` | 43612-43623 | - |
| `0x10038190` | `Export_BotUpdateClient` | 43624-43636 | - |
| `0x10038200` | `Export_BotUpdateEntity` | 43637-43649 | - |
| `0x10038270` | `Export_BotAddSound` | 43650-43662 | - |
| `0x100382f0` | `Export_BotAddPointLight` | 43663-43676 | - |
| `0x10038380` | `Export_BotLibAI` | 43677-43689 | - |
| `0x100383f0` | `Export_BotLibConsoleMessage` | 43690-43702 | - |
| `0x10038460` | `Export_Test` | 43703-43708 | - |
| `0x10038480` | `GetBotAPI` | 43709-43737 | `src/q2bridge/botlib.h:230` |

### `l_crc.c`

Primary reconstruction: `src/botlib/common/l_crc.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x100385b0` | `CRC_Init` | 43738-43745 | - |
| `0x100385d0` | `CRC_ProcessByte` | 43746-43756 | - |
| `0x10038620` | `CRC_Value` | 43757-43764 | - |
| `0x10038640` | `CRC_Block` | 43765-43796 | - |
| `0x100386e0` | `?` | 43797-43816 | - |

### `l_libvar.c`

Primary reconstruction: `src/botlib/common/l_libvar.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x10038750` | `LibVarStringValue` | 43817-43859 | - |
| `0x10038810` | `LibVarAlloc` | 43860-43898 | - |
| `0x100388a0` | `LibVarDeAlloc` | 43899-43909 | - |
| `0x100388d0` | `?` | 43910-43923 | - |
| `0x10038910` | `LibVarGet` | 43924-43939 | - |
| `0x10038960` | `LibVarGetString` | 43940-43951 | - |
| `0x10038990` | `LibVarGetValue` | 43952-43962 | `src/botlib/aas/aas_main.c:219` |
| `0x100389c0` | `LibVar` | 43963-44006 | - |
| `0x10038a60` | `LibVarString` | 44007-44013 | - |
| `0x10038a90` | `LibVarValue` | 44014-44019 | - |
| `0x10038ac0` | `LibVarSet` | 44020-44067 | `src/botlib/aas/aas_main.c:221` |
| `0x10038b80` | `LibVarChanged` | 44068-44079 | - |
| `0x10038bb0` | `LibVarSetNotModified` | 44080-44090 | - |

### `l_log.c`

Primary reconstruction: `src/botlib/common/l_log.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x10038be0` | `Log_Open` | 44091-44132 | - |
| `0x10038cf0` | `Log_Close` | 44133-44148 | - |
| `0x10038d60` | `Log_Shutdown` | 44149-44159 | - |
| `0x10038d80` | `Log_Write` | 44160-44174 | `src/botlib/aas/aas_cluster.c:80` |
| `0x10038dd0` | `?` | 44175-44208 | - |
| `0x10038ec0` | `Log_FilePointer` | 44209-44214 | - |
| `0x10038ee0` | `Log_Flush` | 44215-44226 | - |

### `l_memory.c`

Primary reconstruction: `src/botlib/common/l_memory.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x10038f10` | `LinkMemoryBlock` | 44227-44241 | - |
| `0x10038f50` | `UnlinkMemoryBlock` | 44242-44260 | - |
| `0x10038f90` | `GetMemory` | 44261-44275 | - |
| `0x10039000` | `GetClearedMemory` | 44276-44283 | `src/botlib/common/l_crc.c:246` |
| `0x10039040` | `BlockFromPointer` | 44284-44302 | - |
| `0x100390b0` | `FreeMemory` | 44303-44318 | - |
| `0x10039120` | `MemoryByteSize` | 44319-44330 | - |
| `0x10039150` | `PrintUsedMemorySize` | 44331-44338 | - |
| `0x10039190` | `PrintMemoryLabels` | 44339-44349 | - |
| `0x100391c0` | `DumpMemory` | 44350-44361 | - |

### `l_precomp.c`

Primary reconstruction: `src/botlib/precomp/l_precomp.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x10039200` | `SourceError` | 44362-44372 | `src/botlib/aas/aas_sound.c:192`, `src/botlib/ai_chat/ai_chat.c:21` |
| `0x10039270` | `SourceWarning` | 44373-44383 | - |
| `0x100392e0` | `PC_PushIndent` | 44384-44400 | - |
| `0x10039350` | `PC_PopIndent` | 44401-44419 | - |
| `0x100393e0` | `PC_PushScript` | 44420-44439 | - |
| `0x10039460` | `PC_CopyToken` | 44440-44448 | - |
| `0x100394a0` | `PC_FreeToken` | 44449-44454 | - |
| `0x100394c0` | `PC_ReadSourceToken` | 44455-44494 | - |
| `0x100395f0` | `PC_UnreadSourceToken` | 44495-44504 | - |
| `0x10039630` | `PC_ReadDefineParms` | 44505-44756 | - |
| `0x10039a70` | `PC_StringizeTokens` | 44757-44822 | - |
| `0x10039b50` | `PC_MergeTokens` | 44823-44908 | - |
| `0x10039c30` | `PC_NameHash` | 44909-44947 | - |
| `0x10039cb0` | `PC_AddDefineToHash` | 44948-44959 | - |
| `0x10039ce0` | `PC_FindHashedDefine` | 44960-45016 | - |
| `0x10039d70` | `PC_FindDefine` | 45017-45070 | - |
| `0x10039df0` | `PC_FindDefineParm` | 45071-45125 | - |
| `0x10039e70` | `PC_FreeDefine` | 45126-45154 | - |
| `0x10039ee0` | `PC_AddBuiltinDefines` | 45155-45215 | - |
| `0x1003a000` | `PC_ExpandBuiltinDefine` | 45216-45350 | - |
| `0x1003a2d0` | `PC_ExpandDefine` | 45351-45560 | - |
| `0x1003a690` | `PC_ExpandDefineIntoSource` | 45561-45577 | - |
| `0x1003a710` | `PC_ConvertPath` | 45578-45633 | - |
| `0x1003a7a0` | `PC_Directive_include` | 45634-45796 | - |
| `0x1003ab10` | `PC_ReadLine` | 45797-45851 | - |
| `0x1003abd0` | `PC_WhiteSpaceBeforeToken` | 45852-45859 | - |
| `0x1003ac00` | `PC_ClearTokenWhiteSpace` | 45860-45869 | - |
| `0x1003ac30` | `PC_Directive_undef` | 45870-45964 | - |
| `0x1003ade0` | `PC_Directive_define` | 45965-46298 | - |
| `0x1003b320` | `PC_DefineFromString` | 46299-46355 | - |
| `0x1003b460` | `PC_AddDefine` | 46356-46367 | - |
| `0x1003b4a0` | `PC_AddGlobalDefine` | 46368-46380 | - |
| `0x1003b4e0` | `PC_RemoveGlobalDefine` | 46381-46392 | - |
| `0x1003b520` | `?` | 46393-46404 | - |
| `0x1003b560` | `PC_CopyDefine` | 46405-46481 | - |
| `0x1003b680` | `PC_AddGlobalDefinesToSource` | 46482-46488 | - |
| `0x1003b6c0` | `PC_Directive_if_def` | 46489-46517 | - |
| `0x1003b7b0` | `PC_Directive_ifdef` | 46518-46523 | - |
| `0x1003b7d0` | `PC_Directive_ifndef` | 46524-46529 | - |
| `0x1003b7f0` | `PC_Directive_else` | 46530-46553 | - |
| `0x1003b880` | `PC_Directive_endif` | 46554-46568 | - |
| `0x1003b8d0` | `PC_OperatorPriority` | 46569-46666 | - |
| `0x1003b9e0` | `PC_EvaluateTokens` | 46667-47542 | - |
| `0x1003c650` | `PC_Evaluate` | 47543-47672 | - |
| `0x1003c900` | `PC_DollarEvaluate` | 47673-47822 | - |
| `0x1003cc10` | `PC_Directive_elif` | 47823-47843 | - |
| `0x1003ccb0` | `PC_Directive_if` | 47844-47858 | - |
| `0x1003cd00` | `PC_Directive_line` | 47859-47866 | - |
| `0x1003cd30` | `PC_Directive_error` | 47867-47877 | - |
| `0x1003cd80` | `PC_Directive_pragma` | 47878-47894 | - |
| `0x1003cdf0` | `UnreadSignToken` | 47895-47912 | - |
| `0x1003ce90` | `PC_Directive_eval` | 47913-47943 | - |
| `0x1003cf80` | `PC_Directive_evalfloat` | 47944-47977 | - |
| `0x1003d090` | `PC_ReadDirective` | 47978-48051 | - |
| `0x1003d1d0` | `PC_DollarDirective_evalint` | 48052-48085 | - |
| `0x1003d2f0` | `PC_DollarDirective_evalfloat` | 48086-48124 | - |
| `0x1003d420` | `PC_ReadDollarDirective` | 48125-48199 | - |
| `0x1003d580` | `PC_ReadTokenHandle` | 48200-48239 | - |
| `0x1003d650` | `PC_ExpectTokenString` | 48240-48293 | - |
| `0x1003d740` | `PC_ExpectTokenType` | 48294-48499 | - |
| `0x1003dae0` | `PC_ExpectAnyToken` | 48500-48509 | - |
| `0x1003db20` | `PC_CheckTokenString` | 48510-48558 | - |
| `0x1003dbe0` | `PC_CheckTokenType` | 48559-48577 | - |
| `0x1003dc80` | `PC_SkipUntilString` | 48578-48628 | - |
| `0x1003dd40` | `PC_UnreadLastToken` | 48629-48636 | - |
| `0x1003dd70` | `PC_UnreadToken` | 48637-48643 | - |
| `0x1003dda0` | `PC_SetIncludePath` | 48644-48704 | - |
| `0x1003de40` | `PC_SetPunctuations` | 48705-48711 | - |
| `0x1003de60` | `LoadSourceFile` | 48712-48735 | - |
| `0x1003df30` | `LoadSourceMemory` | 48736-48758 | - |
| `0x1003e000` | `FreeSource` | 48759-48794 | - |

### `l_script.c`

Primary reconstruction: `src/botlib/precomp/l_script.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x1003e120` | `PS_CreatePunctuationTable` | 48795-48881 | - |
| `0x1003e250` | `PunctuationFromNum` | 48882-48906 | - |
| `0x1003e2c0` | `ScriptError` | 48907-48917 | `src/botlib/aas/aas_map.c:44`, `src/botlib/aas/aas_map.c:6055` (+2) |
| `0x1003e340` | `ScriptWarning` | 48918-48928 | - |
| `0x1003e3c0` | `SetScriptPunctuations` | 48929-48942 | - |
| `0x1003e410` | `PS_ReadWhiteSpace` | 48943-49023 | - |
| `0x1003e520` | `PS_ReadEscapeCharacter` | 49024-49235 | - |
| `0x1003e7f0` | `PS_ReadString` | 49236-49325 | - |
| `0x1003e9f0` | `PS_ReadName` | 49326-49356 | - |
| `0x1003eab0` | `NumberValue` | 49357-49471 | - |
| `0x1003ecd0` | `PS_ReadNumber` | 49472-49646 | - |
| `0x1003f020` | `PS_ReadLiteral` | 49647-49701 | - |
| `0x1003f160` | `PS_ReadPunctuation` | 49702-49732 | - |
| `0x1003f230` | `PS_ReadPrimitive` | 49733-49764 | - |
| `0x1003f2d0` | `?` | 49765-49838 | - |
| `0x1003f4d0` | `PS_ExpectTokenString` | 49839-49892 | - |
| `0x1003f5c0` | `PS_ExpectTokenType` | 49893-50108 | - |
| `0x1003f9b0` | `PS_ExpectAnyToken` | 50109-50118 | - |
| `0x1003f9f0` | `PS_CheckTokenString` | 50119-50168 | - |
| `0x1003fab0` | `PS_CheckTokenType` | 50169-50187 | - |
| `0x1003fb50` | `PS_SkipUntilString` | 50188-50238 | - |
| `0x1003fc10` | `PS_UnreadLastToken` | 50239-50246 | - |
| `0x1003fc30` | `PS_UnreadToken` | 50247-50255 | - |
| `0x1003fc70` | `PS_NextWhiteSpaceChar` | 50256-50271 | - |
| `0x1003fcb0` | `StripDoubleQuotes` | 50272-50335 | - |
| `0x1003fd40` | `StripSingleQuotes` | 50336-50399 | - |
| `0x1003fdd0` | `?` | 50400-50454 | - |
| `0x1003fec0` | `?` | 50455-50509 | - |
| `0x1003ffb0` | `SetScriptFlags` | 50510-50517 | - |
| `0x1003ffd0` | `GetScriptFlags` | 50518-50524 | - |
| `0x1003fff0` | `ResetScript` | 50525-50540 | - |
| `0x10040060` | `EndOfScript` | 50541-50547 | `src/botlib/precomp/l_script.c:2079` |
| `0x10040090` | `NumLinesCrossed` | 50548-50553 | - |
| `0x100400c0` | `?` | 50554-50587 | - |
| `0x10040150` | `FileLength` | 50588-50597 | - |
| `0x100401a0` | `LoadScriptFile` | 50598-50661 | `src/botlib/precomp/l_script.c:2076` |
| `0x10040380` | `LoadScriptMemory` | 50662-50702 | - |
| `0x10040470` | `FreeScript` | 50703-50714 | - |

### `l_struct.c`

Primary reconstruction: `src/botlib/common/l_struct.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x100404b0` | `FindField` | 50715-50770 | - |
| `0x10040540` | `?` | 50771-51011 | - |
| `0x10040990` | `ReadChar` | 51012-51039 | - |
| `0x10040a50` | `ReadString` | 51040-51055 | - |
| `0x10040ad0` | `ReadStructure` | 51056-51269 | - |
| `0x10040e30` | `WriteIndent` | 51270-51288 | - |
| `0x10040e80` | `WriteFloat` | 51289-51333 | - |
| `0x10040f20` | `WriteStructWithIndent` | 51334-51454 | - |
| `0x10041210` | `WriteStructure` | 51455-51460 | - |

### `l_utils.c`

Primary reconstruction: `src/botlib/common/l_utils.c`

| DLL address | Retail name | HLIL lines | Cited at |
| --- | --- | --- | --- |
| `0x10041240` | `sub_10041240` | 51461-51577 | - |
| `0x100415e0` | `sub_100415E0` | 51578-51583 | - |
| `0x10041600` | `sub_10041600` | 51584-51602 | - |
| `0x10041650` | `sub_10041650` | 51603-51615 | - |
| `0x10041680` | `sub_10041680` | 51616-51638 | - |
| `0x10041740` | `sub_10041740` | 51639-51644 | - |
| `0x10041760` | `sub_10041760` | 51645-51652 | - |
| `0x10041790` | `vectoangles` | 51653-51720 | `src/botlib/ai/ai_dm.c:601` |
| `0x100418d0` | `?` | 51721-51736 | - |
| `0x10041900` | `sub_10041900` | 51737-51776 | - |
| `0x10041970` | `sub_10041970` | 51777-51863 | - |
| `0x10041ba0` | `sub_10041BA0` | 51864-52076 | `src/botlib/common/l_assets.c:25`, `src/botlib/common/l_assets.c:211` (+1) |
| `0x10041f60` | `sub_10041F60` | 52077-52091 | `src/botlib/common/l_assets.c:215` |
| `0x10041ff0` | `?` | 52092-52203 | - |
| `0x10042380` | `sub_10042380` | 52204-52216 | - |
| `0x100423b0` | `sub_100423B0` | 52217-52223 | - |
| `0x100423d0` | `sub_100423D0` | 52224-52230 | - |
| `0x100423f0` | `sub_100423F0` | 52231-52238 | - |

