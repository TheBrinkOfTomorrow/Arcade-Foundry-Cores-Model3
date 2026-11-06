# Design documents

High-level designs for the Model 3 cores: what each part is for, how it is
built and why, how it is tested and what is still open. They describe the
design as it is and are updated in the same pull request as a change that
moves it. The dated history is in [HANDOFF.md](../../HANDOFF.md), the
current state in [STATE.md](../../STATE.md). They are public: they name no
people, machines or private paths and hold nothing taken from ROMs.

| Document | Covers |
|---|---|
| [MODEL3.md](MODEL3.md) | The Model 3 cores as a whole: Supermodel as base, one core per game, graphics on each platform, checking against Supermodel |
| [ARM64-PERFORMANCE.md](ARM64-PERFORMANCE.md) | Speed on the Raspberry Pi 5 and Apple devices: wait-loop skip, PowerPC recompiler, Metal and OpenGL ES renderers, threads |
