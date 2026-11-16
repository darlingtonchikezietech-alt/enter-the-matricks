# Enter The Matricks (codename ETM)

Seated dual-cannon VR survival shooter for **Meta Quest 2**, built in **Unreal Engine 5**.

> ⚠️ Working title. Don't use it publicly until naming clearance is done (tracker `docs/PLAN_REVIEW.md`, P-16).

**Plan, spec, task board and handoff notes live in the tracker repo:**
👉 [enter-the-matricks-tracker](https://github.com/darlingtonchikezietech-alt/enter-the-matricks-tracker) (start with `RESUME_HERE.md`)

## Status

Scaffold only. The C++ below was written to the bible's spec but **has not been compiled yet**. That is task T-0005.

| Path | What | Task |
|---|---|---|
| `Source/ETM/Weapons/CannonHeatModel.h` | Pure-C++ heat model, matches `tools/balance_sim.py` (tested outside Unreal) | T-0009/T-0010 |
| `Source/ETM/Weapons/CannonComponent.*` | Servo aim within the envelope, cadence, muzzle hitscan, obstruction check, weak-point multipliers | T-0009 |
| `Source/ETM/Combat/DefenseComponent.*` | Brace: 2 s, 75%, 12 s cooldown from end | T-0011 |
| `Source/ETM/Combat/IntegrityComponent.*` | Rig/objective health with brace hook | T-0011 |
| `Source/ETM/Player/XRSeatedRigPawn.*` | XR origin, tracked camera, controllers → cannons, recenter, Forward re-anchor | T-0007 |
| `Tools/HeatModelTest/` | g++ test of the heat model, runs in CI | T-0010 |
| `Config/DefaultEngine.ini` | Quest baseline: mobile forward, HDR off, multiview, 4× MSAA | T-0005 |

## Setup

```bash
git clone https://github.com/darlingtonchikezietech-alt/enter-the-matricks
git clone https://github.com/darlingtonchikezietech-alt/enter-the-matricks-tracker   # sibling folder
cd enter-the-matricks
git lfs install
git config core.hooksPath .githooks      # enforces claimed paths + [T-xxxx] commit tags
export ETM_AGENT=your-name
```

Then right-click `ETM.uproject` → *Generate project files*, or open it with the engine version recorded in `docs/TOOLCHAIN.md` (to be created in T-0005).
