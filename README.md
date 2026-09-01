# Artifact Fabric

Artifact Fabric is a production-system runtime for the lifecycle and authority
of expensive, machine-produced artificial-intelligence artifacts. It is not a
cache, a package manager, a blob store, or a filesystem wrapper. It is the
generalized cross-artifact substrate that answers one question:

> What machine-produced artifact exists, where did it come from, what is it
> compatible with, which dependencies and generations make it valid, where
> should it live, who may publish or replace it, and when is it safe to reuse,
> invalidate, migrate, or retire?

Artifact Fabric is written in C++20, CMake-based, developed on MSVC for
Windows, and validated against NVIDIA CUDA 13.1 on a real NVIDIA GeForce
RTX 5090 (compute capability 12.0 / sm_120).

---

## Core systems question

An artifact is not reusable merely because bytes exist. Reuse requires proof
that identity, compatibility, dependency state, generation authority,
integrity, and lifecycle state still agree. Artifact Fabric owns the
cross-artifact substrate for artifact identity, kind, producer identity,
provenance linkage, compatibility, immutable publication, generation,
dependency relationships, persistence, placement, residency metadata,
validation state, lifecycle, promotion, supersession, invalidation,
migration, replication metadata, reuse eligibility, authority, and recovery.

## Architectural boundary

Artifact Fabric does not itself produce kernels, graphs, or engines. Systems
such as a model cache, kernel cache, graph cache, compilation fabric, or state
provenance system may *produce*, *consume*, *specialize*, or *reason about*
artifacts. Artifact Fabric provides the generalized, authority-aware substrate
those systems use to publish, validate, reuse, and retire artifacts safely.

## Artifact model

The canonical `ArtifactDescriptor` carries the artifact identity, kind,
generation, immutable content digest, producer identity and generation,
provenance reference, timestamps, model/adapter identity where applicable,
runtime and backend, compiler and toolchain, target architecture, compute
capability, ABI, dtype, layout, shape/specialization, launch metadata, direct
and transitive dependencies, policy fingerprint, validation and lifecycle
state, placement metadata, size, reuse metadata, and authority metadata. Once a
generation is finalized and published, its semantic identity and content are
immutable. A changed field produces a new generation or new identity.

## Identity and generation model

Strong typed identities (`ArtifactId`, `ArtifactContentId`,
`ProducerId`, `ProvenanceId`, `ModelId`, `KernelId`, `WorkerId`,
`WorkerBootId`, `PlacementId`, ...) are distinct types, not strings or
integers. Generations (`ArtifactGeneration`, `ProducerGeneration`,
`DependencyGeneration`, `ModelGeneration`, `ToolchainGeneration`,
`PlacementGeneration`, ...) are separately typed so authority can roll
independently and generations are never cross-contaminated by a single generic
counter.

## Compatibility

`evaluate_compatibility` performs deterministic typed checks across artifact
kind, model/adapter identity and revision, runtime, backend, compiler,
toolchain, architecture, compute capability, ABI, dtype, layout, shape,
specialization, launch geometry, tokenizer/vocabulary, dependency generations,
policy generation, artifact generation, and protocol version. It returns a
typed outcome (`COMPATIBLE`, `INCOMPATIBLE`, `STALE`,
`INVALIDATED`, `DEPENDENCY_STALE`, `GENERATION_MISMATCH`,
`TOOLCHAIN_MISMATCH`, `ARCH_MISMATCH`, `ABI_MISMATCH`,
`INTEGRITY_FAILURE`, `UNKNOWN`) and exposes the exact failed dimensions.

## Dependency graph

A directed acyclic dependency graph supports direct/transitive/reverse
dependencies, dependency generations, digests, shared dependencies,
fan-in/fan-out, promotion dependencies, invalidation propagation, and
supersession chains. It rejects illegal cycles, self-dependency, duplicate
edges, stale dependency generations, malformed references, and impossible
states. Traversal is deterministic.

## Publication and promotion

Publication is transactional: plan, reserve, produce/acquire, validate,
verify dependencies, persist, publish, commit. A failure before the commit
never becomes authoritative; prior valid state remains authoritative,
temporary backing is cleaned up, and accounting rolls back exactly.
`promote` moves a candidate to validated/published authority and requires
integrity, compatibility, dependency, generation-authority, and validation
success.

## Lifecycle

A guarded lifecycle (`DECLARED`, `BUILDING`, `VALIDATING`, `VALID`,
`PUBLISHED`, `ACTIVE`, `STALE`, `INVALIDATED`, `SUPERSEDED`,
`QUARANTINED`, `EVICTED`, `RETIRED`, `FAILED`) enforces an explicit
transition table. Illegal transitions fail deterministically.

## Supersession and invalidation

`supersede` records a predecessor/successor/reason/timestamp/compatibility
relationship without rewriting history. `invalidate` propagates to relevant
dependents only and exposes exact invalidation cause chains. Old artifacts
never silently become new artifacts.

## Persistence and recovery

Durable versioned binary persistence uses deterministic little-endian
encoding, an explicit schema version, bounded lengths and counts, and a CRC-32
trailer, with rejection of corruption, truncation, trailing garbage,
duplicate IDs, invalid enums, invalid generations, malformed dependencies,
impossible lifecycle states, and NaN/Inf. Recovery restores identities,
metadata, content references, dependencies, lifecycle state, generations,
compatibility metadata, placement metadata, authority state, and supersession
state, and reproduces stable digests.

## Placement and storage

Explicit placement metadata spans local filesystem, local NVMe, host memory,
device memory, shared storage, object/external and opaque backing. Selection
uses deterministic inputs (locality, integrity, readiness, cost, generation,
compatibility, transfer requirement). Artifact authority is never blurred with
physical location.

## Distributed authority

Distributed mutation authority is fenced by `CoordinatorEpoch`,
`WorkerBootId`, `AttemptId`, `AttemptGeneration`, and the relevant
generation counters. Stale authority is rejected deterministically over the
framed TCP protocol; old work can never publish, promote, invalidate, supersede,
or retire current artifacts after a worker restart, epoch rollover, retry,
generation rollover, dependency change, model revision change, toolchain
change, or producer restart.

## CUDA-backed artifact proof

On a real NVIDIA GeForce RTX 5090 (sm_120, CUDA 13.1), `run_cuda_proof`
compiles a kernel with NVRTC, publishes it through Artifact Fabric as a
content-addressed `COMPILED_KERNEL` artifact, loads it through the CUDA
driver, launches it on the device, and compares the result against a CPU
reference. It then proves that a mutated compatibility requirement (compute
capability change) rejects reuse, publishes a fresh generation that executes
successfully, and verifies device memory returns to baseline. Synthetic
multi-device scenarios are not fabricated; only the single real GPU is used.

## Examples

The `examples/` directory contains runnable programs covering basic
publication, content addressing, dependency graphs, compatibility,
reuse eligibility, supersession, invalidation propagation, quarantine,
deduplicated backing, single-flight production, persistence/recovery,
authority fencing, the CUDA artifact, generation rollover, and explainability.

## Build, install, use

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

CUDA-backed proof (requires a supported NVIDIA GPU and CUDA toolkit):

```
cmake -S . -B build-cuda -G Ninja -DCMAKE_BUILD_TYPE=Release -DARTIFACT_FABRIC_WITH_CUDA=ON
cmake --build build-cuda
ctest --test-dir build-cuda
```

Install and consume:

```
cmake --install build --prefix <prefix>
```

```
find_package(ArtifactFabric CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE af::artifact_fabric)
```

The `af` CLI provides `register`, `inspect`, `publish`, `validate`,
`promote`, `supersede`, `invalidate`, `retire`, `check-reuse`,
`show-dependencies`, `show-dependents`, `explain`, `snapshot`,
`save`, `recover`, `replay`, and `benchmark`, with text or JSON output.

## Benchmark summary

The benchmark (`af_bench`) reports workload sizes and units. On the developer
workstation the measured orders of magnitude are: descriptor creation +
hashing ~0.2 us/op; SHA-256 of 64 bytes ~0.36 us/op; indexed lookup by id
sub-microsecond; compatibility evaluation ~0.5 us/op; publication ~8 us/op;
descriptor serialization ~1 us/op; persistence save ~2.8 ms/image for a
2000-artifact catalog; recovery ~10 ms/image. Exact numbers vary with
hardware and build.

## Limitations

Artifact Fabric is validated on Windows with MSVC and CUDA 13.1 / RTX 5090.
The distributed proof runs a coordinator and worker OS processes over framed
TCP on loopback. Placement targets other than the local filesystem are
metadata abstractions backed by externally-managed storage, not a
implemented cloud service. Multi-GPU scenarios are not validated; the CUDA
proof uses a single real GPU.

---

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
