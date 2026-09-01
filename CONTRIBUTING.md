# Contributing to Artifact Fabric

We welcome contributions to Artifact Fabric. By contributing you agree that
your contributions are licensed under the Apache License, Version 2.0. There
is no Contributor License Agreement (CLA) and no copyright assignment
required.

## Code of Conduct

Be respectful, constructive, and evidence-driven. Treat every claim you make
as something you must be able to demonstrate with a reproducible test or
workload.

## Development workflow

1. Fork the repository and create a topic branch.
2. Make focused, reviewable changes. Keep each commit coherent.
3. Build and run the full test suite before submitting. Artifact Fabric is
   a systems runtime: correctness, deterministic behavior, and the absence
   of races and resource leaks are part of the definition of done.
4. Verify there are no compiler warnings. The project builds with
   MSVC `/W4 /WX` (warnings as errors) on Windows.
5. Where a feature can change observable behavior, add deterministic tests.
   Use fixed seeds for property or randomized tests and print them.
6. Update the README and inline documentation for any user-visible change.
7. Push the branch and open a pull request.

## Licensing

The project is published under the Apache License, Version 2.0. See
LICENSE and NOTICE. Contributions are accepted under the same terms.

## Hardware and toolchain notes

CUDA-backed proof paths are optional and are guarded by an opt-in CMake
option. They exercise the NVIDIA CUDA runtime on supported NVIDIA GPUs.
Without a suitable GPU and CUDA toolkit, the relevant tests are skipped or
reported as unavailable rather than faked. Always prefer narrowing a claim
over claiming a result you cannot reproduce.

## Style

- C++20, MSVC on Windows, CMake, Ninja.
- Use the project's strongly typed ID and generation types instead of raw
  strings or integers for identity.
- No telemetry, no networking except the explicit Artifact Fabric framed
  protocol, and no surprises: make authority, lifecycle, and mutation
  transitions explicit and deterministic.
