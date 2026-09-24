# Changelog
All notable changes to this project will be documented in this file. See [conventional commits](https://www.conventionalcommits.org/) for commit guidelines.

- - -
## [v3.13.0](https://github.com/barryw/vice-mcp/compare/962d86c0e5fe9ca0baaf928602d20080537fbb88..v3.13.0) - 2026-09-24
#### Features
- (**mcp**) add vice.frame.advance, run N frames from a stopped machine - ([962d86c](https://github.com/barryw/vice-mcp/commit/962d86c0e5fe9ca0baaf928602d20080537fbb88)) - Aaron Bell
#### Bug Fixes
- (**mcp**) bound the step wait by the clock, not a count of polls - ([cdc1aec](https://github.com/barryw/vice-mcp/commit/cdc1aec1a9adbba9081b014fc03f2d44c9c057ca)) - Aaron Bell, Claude Opus 5.5
- (**mcp**) stop a step that runs long, drop one a checkpoint cuts short - ([79c7204](https://github.com/barryw/vice-mcp/commit/79c7204a326746b168d2d6b3fbff12d3fcce49e4)) - Aaron Bell, Claude Opus 5.5
- (**mcp**) end vice.frame.advance at a checkpoint and disarm stale frames - ([849f661](https://github.com/barryw/vice-mcp/commit/849f6611a1a7590b4b3cf35d56daeeab7ba59c34)) - Aaron Bell, Claude Opus 5.5
- (**mcp**) make vice.execution.step run on a held machine and reply after it - ([ab6f88d](https://github.com/barryw/vice-mcp/commit/ab6f88d19194006b802b3890025edd6d0d4a9790)) - Aaron Bell, Claude Fable 5.1
#### CI/CD
- build and run MCP unit tests on pull requests - ([bd11bd0](https://github.com/barryw/vice-mcp/commit/bd11bd0a28401e8b635fc577d322072633ac592f)) - Barry Walker, Claude Opus 5.5 (1M context)

- - -

## [v3.12.1](https://github.com/barryw/vice-mcp/compare/6b433ec0a5b34f50d9a519ca2ba54ff640e73df6..v3.12.1) - 2026-09-24
#### Bug Fixes
- (**charset**) stop PETSCII->UTF-8 conversion writing past its buffer - ([df646f2](https://github.com/barryw/vice-mcp/commit/df646f2a0216e58d76f4a86c0f9d5f4cfa402e8e)) - Barry Walker, Claude Opus 5.5 (1M context)
#### Documentation
- (**readme**) fix remaining tool count in reference summary - ([8ac000c](https://github.com/barryw/vice-mcp/commit/8ac000c5e6097713ae1a9f1d5c9fd586b7a8205d)) - Barry Walker, Claude Opus 5.5 (1M context)
- (**readme**) drop unregistered tools, complete Debian deps - ([ed02d7f](https://github.com/barryw/vice-mcp/commit/ed02d7f453d76bfc1860fc94e3e203d5b78615e9)) - Barry Walker, Claude Opus 5.5 (1M context)

- - -

## [v3.12.0](https://github.com/barryw/vice-mcp/compare/dd8ce547e266ad6afb060f63ea0de56c4d9996f4..v3.12.0) - 2026-09-23
#### Features
- (**mcp**) let vice.watch.add take stop, load and store like checkpoint.add - ([90e5ee4](https://github.com/barryw/vice-mcp/commit/90e5ee45c27fe91ae965370f24d29afbf067175c)) - Aaron Bell
#### Bug Fixes
- (**ci**) use gh api for draft-release cleanup (WAL-44) - ([dd8ce54](https://github.com/barryw/vice-mcp/commit/dd8ce547e266ad6afb060f63ea0de56c4d9996f4)) - Forge, [@barryw](https://github.com/barryw)
- (**mcp**) dispatch directly when the machine stops before a queued trap runs - ([755a7f6](https://github.com/barryw/vice-mcp/commit/755a7f696d6d7e56f5804368466caa39106bff40)) - Aaron Bell, Claude Fable 5.1
- (**mcp**) latch joystick input from the MCP tools immediately - ([bca55c4](https://github.com/barryw/vice-mcp/commit/bca55c461abe7fb3c1fc0c45b3ec89d872569a68)) - Aaron Bell, Claude Fable 5.1
- (**mcp**) make vice.run_until resume a machine that is in UI pause - ([6483786](https://github.com/barryw/vice-mcp/commit/6483786f4096e8a3dd49975ad8baaca290dc9564)) - Aaron Bell
- (**mcp**) report "running" from vice.ping until something stops the CPU - ([082e596](https://github.com/barryw/vice-mcp/commit/082e596e9c055950a9941748160fe9304f7af5b8)) - Aaron Bell
- (**mcp**) pause on a stopping watchpoint instead of opening the monitor - ([24f303f](https://github.com/barryw/vice-mcp/commit/24f303f9c1ddc4f8abb71d50190e295e75601f02)) - Aaron Bell
- (**mcp**) stop the CPU where a checkpoint, step or pause says it stops - ([fff8e94](https://github.com/barryw/vice-mcp/commit/fff8e94e4580581bde1aa9a1249ebb916a733d56)) - Aaron Bell
- (**mcp**) guard the CPU trap queue against the HTTP thread - ([9753bc5](https://github.com/barryw/vice-mcp/commit/9753bc58abf62f0ab672efbbaa44b56352cbb707)) - Marcel Rockenschuh, Claude Fable 5
- (**mcp**) correct joystick port off-by-one in input tools - ([7fb1352](https://github.com/barryw/vice-mcp/commit/7fb1352c72959b172617f9bf38051c27e80f08b6)) - Timothy Tacker
- (**monitor**) keep checkpoints alive across a snapshot load - ([827c9cc](https://github.com/barryw/vice-mcp/commit/827c9cc8c917f39f513673be4a8b2bce1bb8b59e)) - Aaron Bell, Claude Fable 5.1
- (**tests**) include stdint.h and stddef.h so the unit suite builds - ([1a7d8fc](https://github.com/barryw/vice-mcp/commit/1a7d8fc5dbe2373b01434ddab078994d7a306d7c)) - Aaron Bell
#### Documentation
- add vice.joystick.tap to README.md - ([05ead64](https://github.com/barryw/vice-mcp/commit/05ead641f87246cffed8fe56489b9d1b4edc851e)) - TWT

- - -

## [v3.11.0](https://github.com/barryw/vice-mcp/compare/91b379aaed8353b3c5310f625ca7c69e60fa299c..v3.11.0) - 2026-07-01
#### Branding
- (**WAL-40**) WHI endorsement + suite cross-link block (#3) - ([367c6ea](https://github.com/barryw/vice-mcp/commit/367c6ea11afb3bbbebc208e5a770304571a4d2a3)) - Barry Walker, Forge, [@barryw](https://github.com/barryw)
#### Features
- (**ci**) adopt cog-based versioning and published Releases (WAL-44) (#1) - ([74e3661](https://github.com/barryw/vice-mcp/commit/74e3661ee7af19c570ab68e0170514838efda0f2)) - Barry Walker, Forge, [@barryw](https://github.com/barryw)
#### Bug Fixes
- (**ci**) push bump commit+tag via HEAD:main --tags (WAL-44) - ([96de04e](https://github.com/barryw/vice-mcp/commit/96de04e037906e0cd062e1373197079392776adb)) - Forge, Claude Opus 4.8
- (**ci**) gitignore minted .ci-token so cog bump sees a clean tree (WAL-44) - ([c37a158](https://github.com/barryw/vice-mcp/commit/c37a158c6407e02f06b0f77f865829e1151eb34d)) - Forge, Claude Opus 4.8
#### CI/CD
- (**macos**) mint GitHub App token for release upload, drop shared PAT (WAL-74) - ([ee00066](https://github.com/barryw/vice-mcp/commit/ee00066c959f277518ea9f46f88029b4508426c0)) - Keel CI, Claude Opus 4.8
- mint GitHub App token for release steps, drop shared PAT (WAL-72) - ([91b379a](https://github.com/barryw/vice-mcp/commit/91b379aaed8353b3c5310f625ca7c69e60fa299c)) - Keel CI, Claude Opus 4.8

- - -

Changelog generated by [cocogitto](https://github.com/cocogitto/cocogitto).