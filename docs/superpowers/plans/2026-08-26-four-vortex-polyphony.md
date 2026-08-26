# Four and Vortex Polyphony Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add deterministic 1-to-16-channel polyphony to Four and Vortex while preserving their existing monophonic DSP and patch compatibility.

**Architecture:** A new Rack-independent `wintoid::polyphony` header owns channel clamping, lane broadcasting, and changed-lane reset iteration. Four and Vortex retain their scalar DSP but wrap it in active-lane loops over fixed state arrays; Brink forwards its current helper API to the common contract.

**Tech Stack:** C++11, VCV Rack 2 SDK, GNU Make, standalone C++ tests with AddressSanitizer and UndefinedBehaviorSanitizer, Python 3 standard-library metadata checks.

**Spec:** `docs/superpowers/specs/2026-08-26-four-vortex-polyphony-design.md`

## Global Constraints

- Four supports 1 to 16 lanes determined only by `V/OCT`; Vortex supports 1 to 16 lanes determined only by `AUDIO IN`.
- An unconnected primary input produces one 0 V lane.
- Secondary inputs never increase voice count. Mono inputs broadcast lane 0; a shorter polyphonic input supplies matching lanes and then falls back to lane 0.
- The eight-voice/three-channel source mapping is exactly `0, 1, 2, 0, 0, 0, 0, 0`.
- Newly active and newly inactive lanes reset. Module reset and sample-rate changes reset all lanes. Vortex mode changes reset all 16 lanes. Four algorithm changes do not reset lanes.
- Preserve scalar DSP formulas, parameter and port IDs, model slugs, patch format, panels, layout, and module registration.
- Use fixed-size state only; allocate nothing and acquire no locks in `process()`.
- Keep all source, test, documentation, filename, commit-message, and asset language within the wintoid modules' own identities; do not record external product inspiration in git.
- Do not add `.superpowers/` or generated test/plugin binaries to commits. Clean build artefacts after final verification.
- Implementation workers use GPT-5.6 Luna with Max reasoning. Both post-task review stages use GPT-5.6 Sol with Medium reasoning.
- Execute in an isolated worktree created with `superpowers:using-git-worktrees`; never implement directly in the user's main workspace.
- After every task commit, complete a Sol Medium specification-compliance review followed by a separate Sol Medium code-quality review. Resolve and re-review findings before starting the next task.

---

### Task 1: Shared Polyphony Contract and Brink Forwarding

**Files:**

- Create: `src/polyphony.h`
- Create: `tests/test_polyphony.cpp`
- Modify: `src/Brink/dsp.h:1-10,193-211`
- Modify: `tests/test_brink_dsp.cpp:174-182`
- Modify: `tests/Makefile:4-31`

**Interfaces:**

- Produces: `wintoid::polyphony::MAX_CHANNELS` with value `16`.
- Produces: `int wintoid::polyphony::effective_channels(int channels)`.
- Produces: `int wintoid::polyphony::broadcast_lane(int lane, int channels)`.
- Produces: `template <typename ResetLane> void wintoid::polyphony::reset_changed_lanes(int previousChannels, int currentChannels, ResetLane resetLane)`.
- Preserves: `brink::MAX_CHANNELS`, `brink::effective_channels(int)`, and `brink::broadcast_lane(int, int)` as forwarding interfaces.

- [ ] **Step 1: Write and register the failing shared-helper test**

Create `tests/test_polyphony.cpp`:

```cpp
#include <stdio.h>
#include <stdlib.h>

#include "../src/polyphony.h"

#define ASSERT(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: assertion failed: %s\n", \
                    __FILE__, __LINE__, #condition); \
            exit(1); \
        } \
    } while (0)

static void test_effective_channels()
{
    ASSERT(wintoid::polyphony::effective_channels(-3) == 1);
    ASSERT(wintoid::polyphony::effective_channels(0) == 1);
    ASSERT(wintoid::polyphony::effective_channels(1) == 1);
    ASSERT(wintoid::polyphony::effective_channels(8) == 8);
    ASSERT(wintoid::polyphony::effective_channels(16) == 16);
    ASSERT(wintoid::polyphony::effective_channels(99) == 16);
}

static void test_broadcast_lane()
{
    ASSERT(wintoid::polyphony::broadcast_lane(7, 0) == 0);
    ASSERT(wintoid::polyphony::broadcast_lane(7, 1) == 0);
    ASSERT(wintoid::polyphony::broadcast_lane(0, 3) == 0);
    ASSERT(wintoid::polyphony::broadcast_lane(1, 3) == 1);
    ASSERT(wintoid::polyphony::broadcast_lane(2, 3) == 2);
    ASSERT(wintoid::polyphony::broadcast_lane(3, 3) == 0);

    const int expected[8] = {0, 1, 2, 0, 0, 0, 0, 0};
    for (int lane = 0; lane < 8; ++lane)
        ASSERT(wintoid::polyphony::broadcast_lane(lane, 3) == expected[lane]);
}

static void assert_transition(int previous, int current,
                              const bool expected[16])
{
    bool observed[16] = {};
    wintoid::polyphony::reset_changed_lanes(
        previous, current, [&](int lane) { observed[lane] = true; });
    for (int lane = 0; lane < 16; ++lane)
        ASSERT(observed[lane] == expected[lane]);
}

static void test_changed_lane_ranges()
{
    bool initial[16] = {};
    initial[0] = true;
    assert_transition(0, 1, initial);

    bool growth[16] = {};
    growth[2] = growth[3] = growth[4] = true;
    assert_transition(2, 5, growth);

    bool shrink[16] = {};
    shrink[2] = shrink[3] = shrink[4] = true;
    assert_transition(5, 2, shrink);

    bool unchanged[16] = {};
    assert_transition(8, 8, unchanged);

    bool upperBoundary[16] = {};
    upperBoundary[15] = true;
    assert_transition(15, 16, upperBoundary);
}

int main()
{
    test_effective_channels();
    test_broadcast_lane();
    test_changed_lane_ranges();
    printf("polyphony helper tests passed\n");
    return 0;
}
```

Add the target to `tests/Makefile`:

```make
all: test_four_dsp test_four_engine test_vortex_dsp test_brink_dsp test_polyphony test_brink_panel

test_polyphony: test_polyphony.cpp ../src/polyphony.h
	$(CC) $(CFLAGS) -o $@ $< -lm

run: test_four_dsp test_four_engine test_vortex_dsp test_brink_dsp test_polyphony test_brink_panel
	./test_four_dsp
	./test_four_engine
	./test_vortex_dsp
	./test_brink_dsp
	./test_polyphony
	python3 test_brink_panel.py

clean:
	rm -f test_four_dsp test_four_engine test_vortex_dsp test_brink_dsp test_polyphony
```

- [ ] **Step 2: Run the shared-helper test and verify that it fails**

Run:

```bash
make -C tests test_polyphony
```

Expected: Make fails because the required `../src/polyphony.h` target does not exist.

- [ ] **Step 3: Implement the minimal Rack-independent helper**

Create `src/polyphony.h`:

```cpp
#pragma once

namespace wintoid {
namespace polyphony {

static const int MAX_CHANNELS = 16;

inline int effective_channels(int channels)
{
    if (channels < 1) return 1;
    return channels > MAX_CHANNELS ? MAX_CHANNELS : channels;
}

inline int broadcast_lane(int lane, int channels)
{
    const int count = effective_channels(channels);
    if (count == 1 || lane >= count) return 0;
    return lane;
}

template <typename ResetLane>
inline void reset_changed_lanes(int previousChannels,
                                int currentChannels,
                                ResetLane resetLane)
{
    const int begin = previousChannels < currentChannels
        ? previousChannels : currentChannels;
    const int end = previousChannels > currentChannels
        ? previousChannels : currentChannels;
    for (int lane = begin; lane < end; ++lane)
        resetLane(lane);
}

} // namespace polyphony
} // namespace wintoid
```

- [ ] **Step 4: Run the shared-helper test and verify that it passes**

Run:

```bash
make -C tests test_polyphony
./tests/test_polyphony
```

Expected: `polyphony helper tests passed` and exit status 0.

- [ ] **Step 5: Forward Brink's existing API to the common helper**

Add the include near the top of `src/Brink/dsp.h`:

```cpp
#include "../polyphony.h"
```

Replace Brink's channel constant and two channel helpers with:

```cpp
static const int MAX_CHANNELS = wintoid::polyphony::MAX_CHANNELS;

inline int effective_channels(int channels)
{
    return wintoid::polyphony::effective_channels(channels);
}

inline int broadcast_lane(int lane, int channels)
{
    return wintoid::polyphony::broadcast_lane(lane, channels);
}
```

Leave `brink::logic_channels()` in place and make it call the forwarding `brink::effective_channels()` function. Extend `TEST(polyphony_channel_rules)` in `tests/test_brink_dsp.cpp` with:

```cpp
ASSERT(brink::MAX_CHANNELS == wintoid::polyphony::MAX_CHANNELS);
ASSERT(brink::effective_channels(0)
       == wintoid::polyphony::effective_channels(0));
ASSERT(brink::broadcast_lane(9, 8)
       == wintoid::polyphony::broadcast_lane(9, 8));
```

- [ ] **Step 6: Run all standalone tests**

Run:

```bash
make -C tests clean
make -C tests run
```

Expected: every Four, Vortex, Brink, shared-polyphony, and Brink-panel test passes under the configured sanitizer flags.

- [ ] **Step 7: Commit the shared contract**

```bash
git add src/polyphony.h src/Brink/dsp.h tests/test_polyphony.cpp tests/test_brink_dsp.cpp tests/Makefile
git commit -m "refactor: share polyphony channel rules"
```

---

### Task 2: Four Engine-State Reset and Isolation

**Files:**

- Modify: `src/Four/engine.h:15-20`
- Modify: `tests/test_four_engine.cpp:478-500`

**Interfaces:**

- Consumes: the existing `four::EngineState` and `four::engine_process()` interfaces.
- Produces: `void four::reset(EngineState& state)`, which restores operator phase, feedback history, and DC-blocker history to construction state.

- [ ] **Step 1: Add failing Four reset and independence tests**

Insert these tests before `main()` in `tests/test_four_engine.cpp`:

```cpp
TEST(engine_states_remain_independent_when_interleaved)
{
    four::EngineParams paramsA;
    paramsA.algorithm = 7;
    paramsA.modMaster = 0.f;
    paramsA.baseFreq = 220.f;
    paramsA.opLevel[1] = 0.f;
    paramsA.opLevel[2] = 0.f;
    paramsA.opLevel[3] = 0.f;

    four::EngineParams paramsB = paramsA;
    paramsB.baseFreq = 659.25f;
    paramsB.opWarp[0] = 0.35f;

    four::EngineState laneA, laneB, referenceA, referenceB;
    const float sampleTime = 1.f / 48000.f;
    for (int sample = 0; sample < 512; ++sample) {
        const float expectedA = four::engine_process(
            referenceA, paramsA, sampleTime, 0.f);
        const float expectedB = four::engine_process(
            referenceB, paramsB, sampleTime, 0.f);
        const float actualA = four::engine_process(
            laneA, paramsA, sampleTime, 0.f);
        const float actualB = four::engine_process(
            laneB, paramsB, sampleTime, 0.f);
        ASSERT_NEAR(actualA, expectedA, 1e-6f);
        ASSERT_NEAR(actualB, expectedB, 1e-6f);
    }

    four::reset(laneA);
    const float expectedB = four::engine_process(
        referenceB, paramsB, sampleTime, 0.f);
    const float actualB = four::engine_process(
        laneB, paramsB, sampleTime, 0.f);
    ASSERT_NEAR(actualB, expectedB, 1e-6f);
}

TEST(engine_state_reset_restores_clean_sequence)
{
    four::EngineParams params;
    params.algorithm = 0;
    params.modMaster = 0.8f;
    params.opFeedback[0] = 0.5f;
    four::EngineState used, fresh;
    const float sampleTime = 1.f / 48000.f;

    for (int sample = 0; sample < 256; ++sample)
        four::engine_process(used, params, sampleTime, 1.f);

    four::reset(used);
    for (int sample = 0; sample < 256; ++sample) {
        const float actual = four::engine_process(
            used, params, sampleTime, 1.f);
        const float expected = four::engine_process(
            fresh, params, sampleTime, 1.f);
        ASSERT_NEAR(actual, expected, 1e-6f);
    }
}
```

Add these calls in `main()` after `run_output_bounded()`:

```cpp
run_engine_states_remain_independent_when_interleaved();
run_engine_state_reset_restores_clean_sequence();
```

- [ ] **Step 2: Run the Four engine test and verify that it fails**

Run:

```bash
make -C tests test_four_engine
```

Expected: compilation fails because `four::reset(EngineState&)` is not defined.

- [ ] **Step 3: Add the explicit reset operation**

Immediately after `EngineState` in `src/Four/engine.h`, add:

```cpp
inline void reset(EngineState& state)
{
    state = EngineState();
}
```

- [ ] **Step 4: Run the Four engine test and verify that it passes**

Run:

```bash
make -C tests test_four_engine
./tests/test_four_engine
```

Expected: all engine tests pass, including the new independence and clean-reset cases.

- [ ] **Step 5: Run the complete standalone suite**

Run:

```bash
make -C tests run
```

Expected: every standalone test target passes.

- [ ] **Step 6: Commit the Four state API**

```bash
git add src/Four/engine.h tests/test_four_engine.cpp
git commit -m "test: define independent Four voice state"
```

---

### Task 3: Four Rack Polyphonic Adapter

**Files:**

- Modify: `src/Four/Four.cpp:1-3,80-235`

**Interfaces:**

- Consumes: `wintoid::polyphony::{MAX_CHANNELS,effective_channels,broadcast_lane,reset_changed_lanes}` from Task 1.
- Consumes: `four::reset(EngineState&)` from Task 2.
- Produces: `MAIN_OUTPUT` with the effective `V/OCT` channel count and one independent `four::EngineState` per active lane.

- [ ] **Step 1: Establish the pre-adapter regression baseline**

Run:

```bash
make -C tests clean
make -C tests run
make
```

Expected: all standalone tests pass and the current monophonic plugin builds before the Rack adapter changes.

- [ ] **Step 2: Add Four's state-array lifecycle and broadcast reader**

Add the shared include:

```cpp
#include "../polyphony.h"
```

Replace the single `engineState` member with:

```cpp
four::EngineState engineStates[wintoid::polyphony::MAX_CHANNELS];
int previousChannels = 0;
float previousSampleRate = 0.f;

void resetLane(int lane)
{
    four::reset(engineStates[lane]);
}

void clearRuntimeState()
{
    for (int lane = 0; lane < wintoid::polyphony::MAX_CHANNELS; ++lane)
        resetLane(lane);
    previousChannels = 0;
    previousSampleRate = 0.f;
}

void onReset() override
{
    clearRuntimeState();
}

static float readBroadcast(Input& input, int lane)
{
    const int channels = input.getChannels();
    if (channels <= 0) return 0.f;
    return input.getVoltage(
        wintoid::polyphony::broadcast_lane(lane, channels));
}

void prepareLanes(int channels)
{
    wintoid::polyphony::reset_changed_lanes(
        previousChannels, channels,
        [&](int lane) { resetLane(lane); });
    previousChannels = channels;
}
```

Call `clearRuntimeState();` at the end of the constructor, immediately after output configuration. Change the primary tooltip configuration to:

```cpp
configInput(VOCT_INPUT,
            "V/OCT (polyphonic voice count, 1 to 16 channels)");
```

- [ ] **Step 3: Replace Four's scalar `process()` body with the active-lane adapter**

Keep the existing parameter-ID arrays with their current values. Replace `process()` with this structure and formulas:

```cpp
void process(const ProcessArgs& args) override
{
    if (args.sampleRate != previousSampleRate) {
        clearRuntimeState();
        previousSampleRate = args.sampleRate;
    }

    const int channels = wintoid::polyphony::effective_channels(
        inputs[VOCT_INPUT].getChannels());
    prepareLanes(channels);
    outputs[MAIN_OUTPUT].setChannels(channels);

    const int coarseIds[] = {
        OP1_COARSE_PARAM, OP2_COARSE_PARAM,
        OP3_COARSE_PARAM, OP4_COARSE_PARAM
    };
    const int fineIds[] = {
        OP1_FINE_PARAM, OP2_FINE_PARAM,
        OP3_FINE_PARAM, OP4_FINE_PARAM
    };
    const int levelIds[] = {
        OP1_LEVEL_PARAM, OP2_LEVEL_PARAM,
        OP3_LEVEL_PARAM, OP4_LEVEL_PARAM
    };
    const int warpIds[] = {
        OP1_WARP_PARAM, OP2_WARP_PARAM,
        OP3_WARP_PARAM, OP4_WARP_PARAM
    };
    const int foldIds[] = {
        OP1_FOLD_PARAM, OP2_FOLD_PARAM,
        OP3_FOLD_PARAM, OP4_FOLD_PARAM
    };
    const int fbIds[] = {
        OP1_FB_PARAM, OP2_FB_PARAM,
        OP3_FB_PARAM, OP4_FB_PARAM
    };
    const int freqModeIds[] = {
        OP1_FREQ_MODE_PARAM, OP2_FREQ_MODE_PARAM,
        OP3_FREQ_MODE_PARAM, OP4_FREQ_MODE_PARAM
    };
    const int foldTypeIds[] = {
        OP1_FOLD_TYPE_PARAM, OP2_FOLD_TYPE_PARAM,
        OP3_FOLD_TYPE_PARAM, OP4_FOLD_TYPE_PARAM
    };
    const int levelCvIds[] = {
        OP1_LEVEL_CV_INPUT, OP2_LEVEL_CV_INPUT,
        OP3_LEVEL_CV_INPUT, OP4_LEVEL_CV_INPUT
    };
    const int warpCvIds[] = {
        OP1_WARP_CV_INPUT, OP2_WARP_CV_INPUT,
        OP3_WARP_CV_INPUT, OP4_WARP_CV_INPUT
    };
    const int foldCvIds[] = {
        OP1_FOLD_CV_INPUT, OP2_FOLD_CV_INPUT,
        OP3_FOLD_CV_INPUT, OP4_FOLD_CV_INPUT
    };
    const int fbCvIds[] = {
        OP1_FB_CV_INPUT, OP2_FB_CV_INPUT,
        OP3_FB_CV_INPUT, OP4_FB_CV_INPUT
    };
    const int levelCvAIds[] = {
        OP1_LEVEL_CV_ATTEN_PARAM, OP2_LEVEL_CV_ATTEN_PARAM,
        OP3_LEVEL_CV_ATTEN_PARAM, OP4_LEVEL_CV_ATTEN_PARAM
    };
    const int warpCvAIds[] = {
        OP1_WARP_CV_ATTEN_PARAM, OP2_WARP_CV_ATTEN_PARAM,
        OP3_WARP_CV_ATTEN_PARAM, OP4_WARP_CV_ATTEN_PARAM
    };
    const int foldCvAIds[] = {
        OP1_FOLD_CV_ATTEN_PARAM, OP2_FOLD_CV_ATTEN_PARAM,
        OP3_FOLD_CV_ATTEN_PARAM, OP4_FOLD_CV_ATTEN_PARAM
    };
    const int fbCvAIds[] = {
        OP1_FB_CV_ATTEN_PARAM, OP2_FB_CV_ATTEN_PARAM,
        OP3_FB_CV_ATTEN_PARAM, OP4_FB_CV_ATTEN_PARAM
    };

    four::EngineParams common;
    common.algorithm = (int)params[ALGO_PARAM].getValue();
    common.globalVCA = params[VCA_PARAM].getValue();
    const float globalFineMult = exp2f(
        params[FINE_TUNE_PARAM].getValue() / 1200.f);

    for (int op = 0; op < 4; ++op) {
        const int freqMode = (int)params[freqModeIds[op]].getValue();
        common.opFreqMode[op] = freqMode;
        common.opFoldType[op] =
            (int)params[foldTypeIds[op]].getValue();
        const float coarseParam = params[coarseIds[op]].getValue();
        common.opCoarse[op] = freqMode == 0
            ? four::coarse_ratio_from_index((int)roundf(coarseParam))
            : four::coarse_fixed_from_param(coarseParam);
        common.opFine[op] = exp2f(
            params[fineIds[op]].getValue() / 1200.f);
    }

    for (int lane = 0; lane < channels; ++lane) {
        four::EngineParams ep = common;
        ep.baseFreq = four::voct_to_freq(
            readBroadcast(inputs[VOCT_INPUT], lane)) * globalFineMult;

        const float modCv = readBroadcast(inputs[XM_CV_INPUT], lane)
            * params[XM_CV_ATTEN_PARAM].getValue() / 10.f;
        ep.modMaster = clamp(
            params[XM_PARAM].getValue() + modCv, 0.f, 1.f);

        const float extPm = readBroadcast(inputs[EXT_PM_CV_INPUT], lane);
        ep.extPmDepth = clamp(
            extPm * params[EXT_PM_CV_ATTEN_PARAM].getValue(),
            0.f, 1.f);

        for (int op = 0; op < 4; ++op) {
            const float levelCv = readBroadcast(inputs[levelCvIds[op]], lane)
                * params[levelCvAIds[op]].getValue() / 10.f;
            ep.opLevel[op] = clamp(
                params[levelIds[op]].getValue() + levelCv, 0.f, 1.f);

            const float warpCv = readBroadcast(inputs[warpCvIds[op]], lane)
                * params[warpCvAIds[op]].getValue() / 10.f;
            ep.opWarp[op] = clamp(
                params[warpIds[op]].getValue() + warpCv, 0.f, 1.f);

            const float foldCv = readBroadcast(inputs[foldCvIds[op]], lane)
                * params[foldCvAIds[op]].getValue() / 10.f;
            ep.opFold[op] = clamp(
                params[foldIds[op]].getValue() + foldCv, 0.f, 1.f);

            const float feedbackCv = readBroadcast(inputs[fbCvIds[op]], lane)
                * params[fbCvAIds[op]].getValue() / 10.f;
            ep.opFeedback[op] = clamp(
                params[fbIds[op]].getValue() + feedbackCv, 0.f, 1.f);
        }

        const float out = four::engine_process(
            engineStates[lane], ep, args.sampleTime, extPm);
        outputs[MAIN_OUTPUT].setVoltage(out * 5.f, lane);
    }
}
```

- [ ] **Step 4: Build Four through both test and Rack compilation paths**

Run:

```bash
make -C tests test_four_dsp test_four_engine test_polyphony
./tests/test_four_dsp
./tests/test_four_engine
./tests/test_polyphony
make
```

Expected: all three standalone binaries pass and the Rack plugin builds without warnings introduced by `Four.cpp`.

- [ ] **Step 5: Check the Four adapter invariants in the source diff**

Run:

```bash
git diff --check
git diff -- src/Four/Four.cpp
```

Confirm in the displayed diff that `V/OCT` is the only source passed to `effective_channels()`, `MAIN_OUTPUT.setChannels(channels)` occurs before lane writes, every input read in `process()` goes through `readBroadcast()`, and only `engineStates[lane]` reaches `engine_process()`.

- [ ] **Step 6: Commit Four polyphony**

```bash
git add src/Four/Four.cpp
git commit -m "feat: add polyphony to Four"
```

---

### Task 4: Vortex Per-Voice State and Isolation

**Files:**

- Modify: `src/Vortex/dsp.h:144-229`
- Modify: `tests/test_vortex_dsp.cpp:323-377`

**Interfaces:**

- Consumes: existing `vortex::Filter1` and `vortex::Filter2` types.
- Produces: `vortex::VoiceState` with public members `f1`, `f2a`, and `f2b`, plus `void VoiceState::reset()`.

- [ ] **Step 1: Add failing Vortex voice-state tests**

Insert these tests before `main()` in `tests/test_vortex_dsp.cpp`:

```cpp
TEST(voice_state_reset_clears_all_filter_history)
{
    vortex::VoiceState state;
    vortex::filter1_configure_lp(state.f1, 48000.f, 1000.f);
    vortex::filter2_configure(
        state.f2a, 48000.f, 1000.f, 0.2f, vortex::F2_LP);
    vortex::filter2_configure(
        state.f2b, 48000.f, 2000.f, 0.3f, vortex::F2_HP);

    for (int sample = 0; sample < 128; ++sample) {
        state.f1.process_lp(1.f);
        vortex::filter2_process(state.f2a, 1.f, vortex::F2_LP);
        vortex::filter2_process(state.f2b, 1.f, vortex::F2_HP);
    }

    state.reset();
    ASSERT_NEAR(state.f1.z, 0.f, 1e-6f);
    ASSERT_NEAR(state.f2a.z0, 0.f, 1e-6f);
    ASSERT_NEAR(state.f2a.z1, 0.f, 1e-6f);
    ASSERT_NEAR(state.f2b.z0, 0.f, 1e-6f);
    ASSERT_NEAR(state.f2b.z1, 0.f, 1e-6f);
}

TEST(voice_states_remain_independent_when_interleaved)
{
    vortex::VoiceState laneA, laneB, referenceA, referenceB;
    vortex::filter2_configure(
        laneA.f2a, 48000.f, 400.f, 0.2f, vortex::F2_LP);
    vortex::filter2_configure(
        referenceA.f2a, 48000.f, 400.f, 0.2f, vortex::F2_LP);
    vortex::filter2_configure(
        laneB.f2a, 48000.f, 4000.f, 0.6f, vortex::F2_HP);
    vortex::filter2_configure(
        referenceB.f2a, 48000.f, 4000.f, 0.6f, vortex::F2_HP);

    for (int sample = 0; sample < 512; ++sample) {
        const float inputA = sinf(
            2.f * vortex::PI * 110.f * sample / 48000.f);
        const float inputB = sinf(
            2.f * vortex::PI * 3300.f * sample / 48000.f);
        const float expectedA = vortex::filter2_process(
            referenceA.f2a, inputA, vortex::F2_LP);
        const float expectedB = vortex::filter2_process(
            referenceB.f2a, inputB, vortex::F2_HP);
        const float actualA = vortex::filter2_process(
            laneA.f2a, inputA, vortex::F2_LP);
        const float actualB = vortex::filter2_process(
            laneB.f2a, inputB, vortex::F2_HP);
        ASSERT_NEAR(actualA, expectedA, 1e-6f);
        ASSERT_NEAR(actualB, expectedB, 1e-6f);
    }

    laneA.reset();
    const float expectedB = vortex::filter2_process(
        referenceB.f2a, 0.25f, vortex::F2_HP);
    const float actualB = vortex::filter2_process(
        laneB.f2a, 0.25f, vortex::F2_HP);
    ASSERT_NEAR(actualB, expectedB, 1e-6f);
}
```

Add these calls after `run_filter2_reset()` in `main()`:

```cpp
run_voice_state_reset_clears_all_filter_history();
run_voice_states_remain_independent_when_interleaved();
```

- [ ] **Step 2: Run the Vortex test and verify that it fails**

Run:

```bash
make -C tests test_vortex_dsp
```

Expected: compilation fails because `vortex::VoiceState` is not defined.

- [ ] **Step 3: Add the Rack-independent Vortex voice-state bundle**

Add this after `Filter2` and before its configuration functions in `src/Vortex/dsp.h`:

```cpp
struct VoiceState
{
    Filter1 f1;
    Filter2 f2a;
    Filter2 f2b;

    void reset()
    {
        f1.reset();
        f2a.reset();
        f2b.reset();
    }
};
```

- [ ] **Step 4: Run the Vortex test and verify that it passes**

Run:

```bash
make -C tests test_vortex_dsp
./tests/test_vortex_dsp
```

Expected: all Vortex DSP tests pass, including both voice-state cases.

- [ ] **Step 5: Run the complete standalone suite**

Run:

```bash
make -C tests run
```

Expected: every standalone test target passes.

- [ ] **Step 6: Commit the Vortex state API**

```bash
git add src/Vortex/dsp.h tests/test_vortex_dsp.cpp
git commit -m "test: define independent Vortex voice state"
```

---

### Task 5: Vortex Rack Polyphonic Adapter

**Files:**

- Modify: `src/Vortex/Vortex.cpp:1-202`

**Interfaces:**

- Consumes: `wintoid::polyphony::{MAX_CHANNELS,effective_channels,broadcast_lane,reset_changed_lanes}` from Task 1.
- Consumes: `vortex::VoiceState` from Task 4.
- Produces: `AUDIO_OUTPUT` with the effective `AUDIO_INPUT` channel count and one independent filter-state bundle per active lane.

- [ ] **Step 1: Add Vortex's state-array lifecycle and broadcast reader**

Add the shared include:

```cpp
#include "../polyphony.h"
```

Replace the three scalar filter members and tracking member with:

```cpp
vortex::VoiceState voiceStates[wintoid::polyphony::MAX_CHANNELS];
int lastMode = -1;
int previousChannels = 0;
float previousSampleRate = 0.f;

void resetAllVoiceStates()
{
    for (int lane = 0; lane < wintoid::polyphony::MAX_CHANNELS; ++lane)
        voiceStates[lane].reset();
}

void clearRuntimeState()
{
    resetAllVoiceStates();
    lastMode = -1;
    previousChannels = 0;
    previousSampleRate = 0.f;
}

void onReset() override
{
    clearRuntimeState();
}

static float readBroadcast(Input& input, int lane)
{
    const int channels = input.getChannels();
    if (channels <= 0) return 0.f;
    return input.getVoltage(
        wintoid::polyphony::broadcast_lane(lane, channels));
}

void prepareLanes(int channels)
{
    wintoid::polyphony::reset_changed_lanes(
        previousChannels, channels,
        [&](int lane) { voiceStates[lane].reset(); });
    previousChannels = channels;
}
```

Call `clearRuntimeState();` at the end of the constructor. Change the primary tooltip configuration to:

```cpp
configInput(AUDIO_INPUT,
            "Audio (polyphonic voice count, 1 to 16 channels)");
```

- [ ] **Step 2: Replace Vortex's scalar `process()` body with the active-lane adapter**

Use this complete control flow while retaining the existing mode equations:

```cpp
void process(const ProcessArgs& args) override
{
    if (args.sampleRate != previousSampleRate) {
        clearRuntimeState();
        previousSampleRate = args.sampleRate;
    }

    const int mode = (int)params[MODE_PARAM].getValue();
    if (mode != lastMode) {
        resetAllVoiceStates();
        lastMode = mode;
    }

    const int channels = wintoid::polyphony::effective_channels(
        inputs[AUDIO_INPUT].getChannels());
    prepareLanes(channels);
    outputs[AUDIO_OUTPUT].setChannels(channels);

    const float cutoffKnob = params[CUTOFF_PARAM].getValue();
    const float resonance = params[RESONANCE_PARAM].getValue();
    const float baseDamping =
        0.707f * (1.f - resonance) + 0.01f * resonance;
    const float driveKnob = params[DRIVE_PARAM].getValue();
    const bool cutoffCvConnected = inputs[CUTOFF_CV_INPUT].isConnected();
    const bool resonanceCvConnected =
        inputs[RESONANCE_CV_INPUT].isConnected();
    const bool driveCvConnected = inputs[DRIVE_CV_INPUT].isConnected();

    for (int lane = 0; lane < channels; ++lane) {
        vortex::VoiceState& voice = voiceStates[lane];
        float signal = readBroadcast(inputs[AUDIO_INPUT], lane) / 5.f;

        float cutoff = cutoffKnob;
        if (cutoffCvConnected) {
            const float cutoffCv =
                readBroadcast(inputs[CUTOFF_CV_INPUT], lane)
                * params[CUTOFF_CV_ATTEN_PARAM].getValue();
            cutoff *= vortex::voct_to_mult(cutoffCv);
        }
        cutoff = clamp(cutoff, 20.f, 20000.f);

        float damping = baseDamping;
        if (resonanceCvConnected) {
            const float resonanceCv =
                readBroadcast(inputs[RESONANCE_CV_INPUT], lane)
                * params[RESONANCE_CV_ATTEN_PARAM].getValue() * 0.2f;
            damping = clamp(damping - resonanceCv, 0.01f, 0.707f);
        }

        float drive = driveKnob;
        if (driveCvConnected) {
            const float driveCv = readBroadcast(inputs[DRIVE_CV_INPUT], lane)
                * params[DRIVE_CV_ATTEN_PARAM].getValue() / 10.f;
            drive = clamp(drive + driveCv, 0.f, 1.f);
        }
        if (drive > 0.f)
            signal = vortex::soft_clip(signal * (1.f + drive * 9.f));

        float wet = 0.f;
        switch (mode) {
        case 0:
            vortex::filter1_configure_lp(voice.f1, args.sampleRate, cutoff);
            wet = voice.f1.process_lp(signal);
            break;
        case 1:
            vortex::filter2_configure(
                voice.f2a, args.sampleRate, cutoff, damping, vortex::F2_LP);
            wet = vortex::filter2_process(voice.f2a, signal, vortex::F2_LP);
            break;
        case 2:
            vortex::filter2_configure(
                voice.f2a, args.sampleRate, cutoff, damping, vortex::F2_LP);
            vortex::filter2_configure(
                voice.f2b, args.sampleRate, cutoff, damping, vortex::F2_LP);
            wet = vortex::filter2_process(voice.f2a, signal, vortex::F2_LP);
            wet = vortex::filter2_process(voice.f2b, wet, vortex::F2_LP);
            break;
        case 3:
            vortex::filter1_configure_hp(voice.f1, args.sampleRate, cutoff);
            wet = voice.f1.process_hp(signal);
            break;
        case 4:
            vortex::filter2_configure(
                voice.f2a, args.sampleRate, cutoff, damping, vortex::F2_HP);
            wet = vortex::filter2_process(voice.f2a, signal, vortex::F2_HP);
            break;
        case 5:
            vortex::filter2_configure(
                voice.f2a, args.sampleRate, cutoff, damping, vortex::F2_HP);
            vortex::filter2_configure(
                voice.f2b, args.sampleRate, cutoff, damping, vortex::F2_HP);
            wet = vortex::filter2_process(voice.f2a, signal, vortex::F2_HP);
            wet = vortex::filter2_process(voice.f2b, wet, vortex::F2_HP);
            break;
        case 6:
            vortex::filter2_configure(
                voice.f2a, args.sampleRate, cutoff, damping, vortex::F2_BP);
            wet = vortex::filter2_process(voice.f2a, signal, vortex::F2_BP);
            break;
        case 7:
            vortex::filter2_configure(
                voice.f2a, args.sampleRate, cutoff, damping, vortex::F2_BP);
            vortex::filter2_configure(
                voice.f2b, args.sampleRate, cutoff, damping, vortex::F2_BP);
            wet = vortex::filter2_process(voice.f2a, signal, vortex::F2_BP);
            wet = vortex::filter2_process(voice.f2b, wet, vortex::F2_BP);
            break;
        case 8:
            vortex::filter2_configure(
                voice.f2a, args.sampleRate, cutoff, damping, vortex::F2_NOTCH);
            wet = vortex::filter2_process(
                voice.f2a, signal, vortex::F2_NOTCH);
            break;
        case 9:
            vortex::filter2_configure(
                voice.f2a, args.sampleRate, cutoff, damping, vortex::F2_NOTCH);
            vortex::filter2_configure(
                voice.f2b, args.sampleRate, cutoff, damping, vortex::F2_NOTCH);
            wet = vortex::filter2_process(
                voice.f2a, signal, vortex::F2_NOTCH);
            wet = vortex::filter2_process(voice.f2b, wet, vortex::F2_NOTCH);
            break;
        case 10:
            vortex::filter2_configure(
                voice.f2a, args.sampleRate, cutoff, damping, vortex::F2_AP);
            wet = vortex::filter2_process(voice.f2a, signal, vortex::F2_AP);
            break;
        case 11:
            vortex::filter2_configure(
                voice.f2a, args.sampleRate, cutoff, damping, vortex::F2_AP);
            vortex::filter2_configure(
                voice.f2b, args.sampleRate, cutoff, damping, vortex::F2_AP);
            wet = vortex::filter2_process(voice.f2a, signal, vortex::F2_AP);
            wet = vortex::filter2_process(voice.f2b, wet, vortex::F2_AP);
            break;
        }

        voice.f1.z = vortex::flush_denormal(voice.f1.z);
        voice.f2a.z0 = vortex::flush_denormal(voice.f2a.z0);
        voice.f2a.z1 = vortex::flush_denormal(voice.f2a.z1);
        voice.f2b.z0 = vortex::flush_denormal(voice.f2b.z0);
        voice.f2b.z1 = vortex::flush_denormal(voice.f2b.z1);
        outputs[AUDIO_OUTPUT].setVoltage(wet * 5.f, lane);
    }
}
```

- [ ] **Step 3: Build Vortex through both test and Rack compilation paths**

Run:

```bash
make -C tests test_vortex_dsp test_polyphony
./tests/test_vortex_dsp
./tests/test_polyphony
make
```

Expected: both standalone binaries pass and the Rack plugin builds without warnings introduced by `Vortex.cpp`.

- [ ] **Step 4: Check the Vortex adapter invariants in the source diff**

Run:

```bash
git diff --check
git diff -- src/Vortex/Vortex.cpp
```

Confirm in the displayed diff that `AUDIO_INPUT` is the only source passed to `effective_channels()`, all CV and audio reads go through `readBroadcast()`, a mode change calls `resetAllVoiceStates()`, and filter processing refers only to `voiceStates[lane]`.

- [ ] **Step 5: Commit Vortex polyphony**

```bash
git add src/Vortex/Vortex.cpp
git commit -m "feat: add polyphony to Vortex"
```

---

### Task 6: Metadata, Documentation, and Release Verification

**Files:**

- Create: `tests/test_polyphony_metadata.py`
- Modify: `tests/Makefile:4-31`
- Modify: `README.md:7-27`
- Modify: `plugin.json:4-33`

**Interfaces:**

- Consumes: Four and Vortex's exact primary-input tooltip strings from Tasks 3 and 5.
- Produces: plugin version `2.2.0`, `Polyphonic` tags for Four and Vortex, manifest descriptions naming 16-channel support, and README voice-count/broadcast guidance.

- [ ] **Step 1: Write and register the failing metadata test**

Create `tests/test_polyphony_metadata.py`:

```python
#!/usr/bin/env python3

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
manifest = json.loads((ROOT / "plugin.json").read_text(encoding="utf-8"))
modules = {module["name"]: module for module in manifest["modules"]}

assert manifest["version"] == "2.2.0"
for name in ("Four", "Vortex"):
    assert "Polyphonic" in modules[name]["tags"]
    assert "16-channel polyphonic" in modules[name]["description"].lower()

readme = (ROOT / "README.md").read_text(encoding="utf-8")
assert "voice count follows the **V/OCT** input" in readme
assert "voice count follows **AUDIO IN**" in readme
assert "shorter polyphonic modulation inputs broadcast lane 0" in readme
assert "shorter polyphonic CV inputs broadcast lane 0" in readme

four_source = (ROOT / "src/Four/Four.cpp").read_text(encoding="utf-8")
vortex_source = (ROOT / "src/Vortex/Vortex.cpp").read_text(encoding="utf-8")
assert "V/OCT (polyphonic voice count, 1 to 16 channels)" in four_source
assert "Audio (polyphonic voice count, 1 to 16 channels)" in vortex_source

print("polyphony metadata tests passed")
```

Add `test_polyphony_metadata` to the final `tests/Makefile` target lists and commands:

```make
all: test_four_dsp test_four_engine test_vortex_dsp test_brink_dsp test_polyphony test_brink_panel test_polyphony_metadata

test_polyphony_metadata:
	python3 test_polyphony_metadata.py

run: test_four_dsp test_four_engine test_vortex_dsp test_brink_dsp test_polyphony test_brink_panel test_polyphony_metadata
	./test_four_dsp
	./test_four_engine
	./test_vortex_dsp
	./test_brink_dsp
	./test_polyphony
	python3 test_brink_panel.py
	python3 test_polyphony_metadata.py

.PHONY: all run clean test_brink_panel test_polyphony_metadata
```

- [ ] **Step 2: Run the metadata test and verify that it fails**

Run:

```bash
make -C tests test_polyphony_metadata
```

Expected: failure at `manifest["version"] == "2.2.0"` because the manifest still reports `2.1.0`.

- [ ] **Step 3: Update the manifest and README**

Set `plugin.json` version to:

```json
"version": "2.2.0"
```

Use these exact Four fields:

```json
"description": "16-channel polyphonic 4-operator FM/PM synthesizer with waveshaping, wavefolding, and feedback",
"tags": [
  "Oscillator",
  "Digital",
  "Waveshaper",
  "Polyphonic"
]
```

Use these exact Vortex fields:

```json
"description": "16-channel polyphonic 12-mode multi-mode filter with drive and CV control. Filter DSP by Yuriy Ivantsov (ivantsov-filters)",
"tags": [
  "Filter",
  "Effect",
  "Polyphonic"
]
```

Replace Four's current `V/OCT` README bullet with:

```markdown
- **16-channel polyphony** — voice count follows the **V/OCT** input; mono and shorter polyphonic modulation inputs broadcast lane 0
```

Add this bullet to the Vortex section after the Controls bullet:

```markdown
- **16-channel polyphony** — voice count follows **AUDIO IN**; mono and shorter polyphonic CV inputs broadcast lane 0
```

- [ ] **Step 4: Run the metadata test and verify that it passes**

Run:

```bash
make -C tests test_polyphony_metadata
```

Expected: `polyphony metadata tests passed` and exit status 0.

- [ ] **Step 5: Run automated release verification**

Run:

```bash
make -C tests clean
make -C tests run
make
git diff --check
```

Expected: all sanitizer-enabled standalone tests and Python checks pass, the Rack plugin builds, and `git diff --check` emits no errors.

- [ ] **Step 6: Perform the Rack channel and broadcast smoke checks**

When Rack can be launched in the execution environment, verify this exact matrix:

```text
Four primary V/OCT channels:     1, 4, 8, 16 -> MAIN OUT: 1, 4, 8, 16
Four with fixed-frequency ops:   8 V/OCT channels -> MAIN OUT: 8
Vortex primary AUDIO IN channels: 1, 4, 8, 16 -> AUDIO OUT: 1, 4, 8, 16
Unpatched primary input:                       output channel count 1
Eight primary lanes + mono CV:                 CV lane 0 reaches all voices
Eight primary lanes + three-channel CV:        source lanes 0,1,2,0,0,0,0,0
Mono primary + 16-channel secondary CV:         output channel count remains 1
```

Expected: every row matches. If Rack cannot be launched, copy this matrix into the implementation handoff as an explicit unverified release checklist.

- [ ] **Step 7: Perform the Rack state-lifecycle and CPU smoke checks**

When Rack can be launched, verify:

```text
Four:   shrink 8 voices to 2, then restore 8; lanes 2-7 restart cleanly.
Four:   change algorithm while holding 8 voices; active phases are not reset.
Vortex: shrink 8 voices to 2, then restore 8; lanes 2-7 restart cleanly.
Vortex: change mode, then restore inactive lanes; no old-mode filter tail returns.
CPU:    observe one voice and 16 voices for 10 seconds each; record both values.
```

Expected: state behaviour matches each line, one-voice overhead is not visibly abnormal, and the 16-voice reading grows roughly linearly rather than superlinearly. If Rack cannot be launched, report all five checks as unverified.

- [ ] **Step 8: Remove generated binaries and inspect final scope**

Run:

```bash
make -C tests clean
make clean
git status --short
git diff --stat
```

Expected: no generated test or plugin binaries appear in git status. Only the Task 6 source/documentation changes are uncommitted; pre-existing `.superpowers/` state remains outside the feature commit.

- [ ] **Step 9: Commit documentation and verification**

```bash
git add README.md plugin.json tests/test_polyphony_metadata.py tests/Makefile
git commit -m "docs: publish Four and Vortex polyphony"
```

- [ ] **Step 10: Run clean-tree post-commit verification**

Run:

```bash
git show --check --stat --oneline HEAD
git status --short --branch
```

Expected: the commit check is clean, the feature worktree has no uncommitted feature files, and no generated binary is present. Report any pre-existing ignored or `.superpowers/` state separately rather than adding it.
