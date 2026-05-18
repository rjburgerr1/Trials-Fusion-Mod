# Bike customization / scene helper rename sheet

## High-confidence customization-table names

| Current | Proposed name | Confidence | Rationale |
|---|---|---:|---|
| `FUN_007262A0` | `GetBikeCustomizationRootByBikeId` | high | Thin wrapper over bike-id lookup map used before appearance work |
| `FUN_00725C90` | `GetBikeAppearanceGroupListByBikeId` | high | Looks up byte-keyed entry from table at `+0x28/+0x2C`; feeds secondary appearance slots |
| `FUN_00724920` | `FindTaskEntryByByteKey` | high | Linear search over task refs comparing first byte |
| `FUN_007254D0` | `CreateEmptyByteKeyTaskRef` | high | Allocates default task wrapper for missing byte-key lookup |
| `FUN_00723A50` | `GetAppearancePartRecordById` | high | Thin wrapper used with concrete appearance part ids |
| `FUN_00725250` | `GetAppearancePartExpansionById` | high | Looks up short-keyed expansion list at `+0x38/+0x3C` |
| `FUN_007249E0` | `FindTaskEntryByShortKey` | high | Linear search over task refs comparing first `short` |
| `FUN_00724B30` | `CreateEmptyShortKeyTaskRef_TypeB` | medium | Default task wrapper for one short-keyed table |
| `FUN_00725600` | `GetAppearancePartRecordRefById` | high | Thin wrapper over `FUN_00725110` |
| `FUN_00725110` | `GetAppearancePartRecordById_Internal` | high | Short-keyed lookup at `+0x48/+0x4C`; used by `FUN_009226D0` |
| `FUN_00724AA0` | `CreateEmptyShortKeyTaskRef_TypeA` | medium | Default task wrapper for another short-keyed table |
| `FUN_009226D0` | `ApplyAppearancePartToBikeScene` | high | Resolves part record, finds scene node by hashed name, rebuilds mesh instances |

## High-confidence scene / material names

| Current | Proposed name | Confidence | Rationale |
|---|---|---:|---|
| `FUN_00DD02C0` | `ReplaceSceneObjectMaterialByHash` | high | Clears old material state, applies material hash, installs new object |
| `FUN_00DCF980` | `ClearSceneObjectMaterialState` | high | Releases child refs, dynamic arrays, prior material object |
| `FUN_00DA1B40` | `SetSceneObjectRenderFlags` | medium | Stores flags at `+0x20` and propagates collection flag |
| `FUN_00D807A0` | `InstantiateSceneObjectFromPrototype` | medium | Creates scene object from prototype/collection and applies render flags |
| `FUN_00D7D330` | `InstantiateCollectionChildrenIntoScene` | medium | Walks scene objects and clones / transforms children into runtime scene |
| `FUN_00D7D2D0` | `ActivateDeferredSceneNodesRecursive` | medium | Recursively activates eligible nodes |
| `FUN_00DCFEA0` | `AttachMaterialObjectToSceneNode` | high | Installs material object, propagates flags, updates physics/children |
| `FUN_00DCFAF0` | `MarkSceneNodeMaterialDirtyRecursive` | high | ORs `0x100` down entire child tree |
| `FUN_00DA2960` | `SceneObjectDescriptor_CopyCtor` | medium | Copies descriptor fields and zeroes runtime-owned members |
| `FUN_00DC9100` | `PropagateSceneNodeFlagsRecursive` | high | ORs arbitrary flags recursively |
| `FUN_00C40370` | `UnpackRgb24ToFloat4` | high | Expands packed RGB into normalized float4 |
| `FUN_00C424D0` | `BlendMaterialColorOverrides` | high | Blends diffuse/diffuse2/colorN material parameters |

## Generic wrappers worth naming structurally, not semantically

| Current | Proposed name | Confidence | Rationale |
|---|---|---:|---|
| `FUN_0070B670` | `CallGraphicsBackendCreateResource` | medium | Lock-guarded wrapper around a graphics backend virtual call |
| `FUN_007081E0` | `InvokePairMethodOnTaskRef` | low | Tiny virtual-dispatch thunk |

## Functions I would not rename semantically yet

These look like obfuscated / packed runtime stubs or anti-tamper-adjacent glue,
not meaningful bike-domain code:

- `FUN_0190F70A`
- `FUN_01841FA9`
- `FUN_01931CE2`
- `FUN_01864BCD`
- `FUN_0189A451`
- `thunk_FUN_018A31F6`

For now, keep structural names if desired:

- `ObfuscatedRuntimeStub_*`
- `PackedThunk_*`
- `RuntimeGlueStub_*`

but avoid pretending they are understood. Their decompilation is not trustworthy
enough yet for domain names.

## Names already good enough as-is

The following were already reasonably named and do not need churn:

- `PrepareTaskForExecution`
- `AssignTaskReference`
- `AllocateMemory`
- `AllocateMemoryBlock`
- `AllocateGraphicsMemory`
- `AllocateGraphicsMemoryWithThreadCheck`
- `IsGraphicsInitialized`
- `CreateGraphicsBuffers`
- `ReleaseGraphicsBuffer`
- `GetGraphicsAllocator`
- `InitVertexBufferAllocator`
- `InitDynamicBufferAllocator`
- `DestroyGraphicsAllocator`
- `InitializeRenderingBuffers`
- `InitIndexBufferAllocator`
- `FinalizeAllocatorSetup`
- `InitializeSubsystem1`
- `InitializeSubsystem2`
- `InitThreadSynchronization`
- `CleanupThreadResources`
- `MemoryCopy`
- `OptimizedMemcpy`
- `FreeMemory`
- `RenderGraphicsTexture`
- `CheckMaterialLayerExists`
- `GetMaterialLayerValue`
- `CallVTableFunction`
- `find_scene_object_by_hash_recursive`
- `SetMaterialTextureByHash`
- `FindMaterialByHash`
- `AcquireTaskLock`
- `InsertMaterialIntoHashTable`
- `LoadMaterialFromDefinition`
- `ReleaseTaskLock`
- `WaitForMaterialLoad`
- `UpdatePhysicsWorld`
- `AssignHashedString`
- `InitializeStringFromStringBuffer`
- `CopyStringAndReallocateIfNeeded`
- `SetSceneObjectPhysicsEnabledRecursive`
- `CollectSceneObjectsByType`
- `SetSceneObjectPhysicsState`
- `attachment_name_destructor`
- `NoOp_ExceptionUnwindStub`
- `create_mesh_instances`
- `AssignStringWithRealloc`
- `InitializeStringFromConstant`
- `ReallocateStringBuffer`
- `string_append_int`
- `ConvertIntegerToString`

