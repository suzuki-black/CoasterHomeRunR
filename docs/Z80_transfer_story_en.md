# The Z80 sends faster than the R800: an exchange between the author and two Claudes

COASTER HOME RUN runs on the R800, the fast CPU of the MSX turboR. Yet when it sends the picture to the VDP, it can switch to the Z80, the slower CPU.
This document pulls together the exchange that led to that design.

Japanese version: [Z80転送のやりとり.md](Z80転送のやりとり.md)

## Summary

- On the turboR, the R800 takes 8.67 µs per byte to write to the VDP. The Z80 takes 5.45 µs with unrolled OUTI. A real FS-A1ST gave the same values.
- Using this result meant rebuilding the frame buffer format. The idea was dropped once as "not worth the rewrite".
- The other side brought it back under a different condition. It was then measured and implemented.
- When first implemented, it saved 3.4–3.7 ms per frame (about 4–6% of the frame time).
- Even after differential transfer cut the amount of data sent, it stayed faster on all five stages measured, saving up to 1.23 ms per frame, just under 2% (measured on 2026-10-05). At a stage where every bit of speed counted, it was an effective step.

It is a record of how ideas were rejected, picked up again and carried through to implementation while working with AI.

## The three parties

| Party | Background | Role in this work |
|---|---|---|
| The author | An experienced programmer (C, C++, Perl, Java, PHP and others) with deep MSX knowledge | Solving the technical problems, asking the questions, setting direction, making decisions, measuring on real hardware. Coding onward was assigned to the implementer |
| Claude in chat (the collaborator) | As an AI, has knowledge on par with a veteran MSX engineer. By the nature of a chat, it cannot act directly, such as running code or taking measurements (it can read source shown to it) | Propose ideas, estimate from data sheets and figures, critique the other side's documents |
| Claude Code (the implementer) | As an AI, has knowledge on par with a veteran MSX engineer. Reads the code and measures in openMSX. Cannot touch real hardware | Implementation and measurement |

The two Claudes never spoke to each other directly. They exchanged Markdown documents, and the author decided which ones to act on and what to do next.

## Timeline

Times are JST. They come from file timestamps, commit times and session logs.

| When | Who | What |
|---|---|---|
| Oct 1, 20:13 | Chat | Proposes pre-rendered backgrounds copied with memcpy, written as an instruction |
| Oct 1, 20:19 | Implementer | Writes a rebuttal, showing that the 13.45 ms transfer remains under either method |
| Oct 1, 20:32 | Chat | Drops pre-rendering. Proposes that writes during vertical blank should reach about 2 µs/byte |
| Oct 1, 20:44 | Implementer | Builds a benchmark ROM. 8.67 µs during display, during vblank and with the screen off |
| Oct 1, evening | Author | Asks whether switching to the Z80 could ever be faster |
| Oct 1, 22:38 | Chat | Withdraws its claim. Lists "transfer in Z80 mode" as an experiment still open |
| Oct 1, 22:46 | Implementer | Measures the Z80 at 5.45 µs. Drops the idea, because the packing calculation now costs time |
| Oct 2, 00:32 | Chat | Proposes again: a buffer packed from the start changes the result |
| Oct 2, 00:46 | Implementer | Measures fills. Packed is 9–13% slower, but better overall. Corrects its earlier judgment |
| Oct 3, 13:50 | Author | Agrees to the plan |
| Oct 3, 14:56 | Author, implementer | Real-hardware results recorded. No dropped writes |
| Oct 3, 18:51 | Implementer | Implements it. The picture matches the previous build on every frame |
| Oct 3, 19:36 | Author | Sets the condition for allowing an exception to the implementation rules |

The five round trips from the first proposal to the second round of measurements took about four and a half hours.

## The exchange

### 1. Pre-rendering, and the 13 ms left over

The starting point was a frame time of about 50 ms. Chat proposed pre-rendering the background and copying it with memcpy.

The implementer replied that showing curves, slopes, turning around and branches this way would take about 1.1 MB or more, too much for the RAM or the ROM. It also admitted that pre-rendering wins on speed in some scenes. Chat dropped the idea.

This round made one thing clear: about 13 ms of transfer time remained under any method.

### 2. "When to send" was ruled out

Using a V9938 data sheet value, chat proposed that writes during vertical blank would shrink to about 2 µs per byte. It stated that the value had not been measured, and it set a condition: if the result matched the display period, it would withdraw the proposal.

The implementer built a benchmark ROM. Every condition gave 8.67 µs, so the proposal was withdrawn as agreed. The implementer read this as a fixed wait that the S1990 inserts on R800 accesses, not as a limit of the VDP.

### 3. The question of "who sends"

Stuck for speed-up ideas, the author asked whether switching to the Z80 could ever be faster.

The R800 is faster than the Z80, so the usual move is to give work to the R800. Going back to the Z80 looks like a detour. But if the wait applies to the R800, the Z80 might not have it.

Chat took this up and put it plainly: what had been ruled out was when to send, and who sends had not been measured yet. It split the idea into three premises.

1. The S1990 wait does not apply in Z80 mode
2. The round-trip cost of CHGCPU is negligible
3. The idea still pays off after allowing for the R800 being stopped during transfer

It also added a condition: if any one of them failed, the idea would be dropped.

### 4. It was faster, and it was dropped

The implementer added items to the benchmark ROM and recorded the results eight minutes after receiving chat's document.

| Condition | Time per byte |
|---|---|
| R800 | 8.67 µs |
| Z80 OTIR | 6.86 µs |
| Z80 unrolled OUTI | **5.45 µs** |
| CHGCPU round trip | about 900 µs |

Premises 1 and 2 held. Premise 3 failed. At that time the R800 packed two blocks into one byte during its wait. If the Z80 did the sending, that calculation would need its own time, and the total would barely change.

| Method | Packing | Switching | Sending | Total |
|---|---|---|---|---|
| R800 packs while sending | Hidden in the wait | — | 13.3 ms | 13.3 ms |
| Z80 sends | about 4.3 ms | 0.9 ms | 8.4 ms | about 13.6 ms |

The implementer dropped the idea as agreed. It closed with one more remark: a packed buffer would make filling heavier, so the rewrite was not worth it.

### 5. An objection to the last paragraph

Chat agreed with dropping the idea and objected only to that closing paragraph. With a buffer packed from the start, the packing calculation would disappear. Writes would also be cut in half, so filling should get lighter.

Again it said that it could not verify its own estimate. It proposed benchmarking the fill alone first, with a clear test: if the result exceeded the current fill time, the idea would be dropped.

### 6. It failed the test but won overall

The implementer recorded the real fill calls from the game and replayed them through both the current fill and a packed fill.

| Scene | Current fill | Packed fill | Difference | Pixel mismatch |
|---|---|---|---|---|
| 45 s | 3.96 ms | 4.30 ms | +9% | 0 bytes |
| 75 s | 4.97 ms | 5.62 ms | +13% | 0 bytes |

Filling got heavier, so chat's estimate was wrong. Writing the blocks took only about 40% of the fill time. The rest was per-line setup. By the proposed test, the idea failed.

However, transfer was expected to fall from 13.3 ms to 9.3 ms, while filling got worse by only 0.3–0.65 ms. Overall it was a gain. The implementer corrected its own "not worth the rewrite" judgment.

Two conditions to check before implementing also came up.

- Whether the VDP drops writes at Z80 speed
- CHGCPU re-enables interrupts on return, so VDP interrupts must be stopped during the switch

### 7. Checking on real hardware

The author agreed to the plan and ran the benchmark ROM on a real FS-A1ST.

| Item | Real hardware | openMSX |
|---|---|---|
| R800 | 8.66 µs | 8.67 µs |
| Z80 unrolled OUTI | 5.45 µs | 5.45 µs |
| CHGCPU round trip | 922 µs | about 900 µs |
| Read-back mismatches (256 bytes) | 0 | 0 |

The values that mattered for the decision matched openMSX.

### 8. Implementation, and an exception to the rules

The work meant rewriting everything that writes to the frame buffer. VRAM contents were checked against the previous build on every frame for stages 1–5, turning around and the title screen.

The planned step of not sending the 13 HUD lines did not hold up, because scenery shows beside the coaster's body. Instead, only the middle of the bottom band is skipped when unchanged.

Switching to the Z80 calls the BIOS every frame, which breaks the implementation rules in the specification. The author set the condition: it is allowed if the frame rate goes up, and not otherwise. These are the results, on the same ROM and the same machine.

| Stage | R800 sends | Z80 sends | Per frame |
|---|---|---|---|
| 1 | 15.3 fps | 16.2 fps | −3.6 ms |
| 2 | 17.8 fps | 18.9 fps | −3.4 ms |
| 3 | 11.3 fps | 11.8 fps | −3.6 ms |
| 4 | 14.3 fps | 15.1 fps | −3.7 ms |
| 5 | 15.5 fps | 16.3 fps | −3.5 ms |

### 9. What came after

- **Switching threshold:** On 2026-10-05, differential transfer began sending only the 8-byte groups that had changed. Switching was no longer always worthwhile, so a threshold was added. The estimate said 36 groups, but measuring showed 20 was fastest.
- **Staying on the R800:** On 2026-10-06, three prototypes tried hiding the compare work inside the OUT wait. All were slower than the current Z80 switching design, which confirmed its strength. In openMSX, only 20–30 cycles could be hidden.
- **Conflict with voices:** After PCM voices went in, line interrupts did not arrive during the switch. Transfers now stay on the R800 while a voice plays. A 0.16-second stall on a just-requested voice appeared too, and was fixed on 2026-10-15.

## The gain

Even after differential transfer, Z80 switching helps on all five stages measured. This is the frame time measured on 2026-10-05, compared with sending on the R800 alone.

| Stage | R800 only | With Z80 switching | Saved | Share |
|---|---|---|---|---|
| 1 | 44.76 ms | 44.22 ms | 0.54 ms | 1.2% |
| 2 | 46.65 ms | 45.81 ms | 0.84 ms | 1.8% |
| 3 | 66.14 ms | 64.91 ms | 1.23 ms | 1.9% |
| 4 | 56.96 ms | 55.96 ms | 1.00 ms | 1.8% |
| 5 | 49.64 ms | 49.22 ms | 0.42 ms | 0.8% |

That is up to just under 2% from a single step. At a stage where every bit of frame time counted, it was effective. It also meets the condition set for the exception to the implementation rules: the frame rate must go up.

## Which guesses held up

| Who | Guess | Result |
|---|---|---|
| Chat | Vblank writes reach about 2 µs/byte | Wrong. Always 8.67 µs |
| Chat | Z80 OUTI runs at 4.47 µs | Wrong. 5.45 µs |
| Chat | The wait does not apply to the Z80 | Right |
| Chat | A packed buffer makes filling lighter | Wrong. 9–13% heavier |
| Chat | Packed buffer + Z80 cuts transfer by about 4 ms | As calculated (13.3→9.3 ms expected) |
| Implementer | The 62-cycle wait comes from the S1990 | Fits the measurements (an interpretation) |
| Implementer | The rewrite is not worth it | Wrong. Corrected by the implementer itself |
| Implementer | The threshold is 36 groups | Wrong. 20 when measured |
| Implementer | Skipping the 13 HUD lines saves 3.6 ms | Wrong. Scenery shows beside the coaster |
| Author | The Z80 might be faster in some cases | Right |

Measurements exposed the wrong guesses quickly, and each one fed the next idea. That cycle is what carried the idea through to implementation.

## Why it worked

1. **Withdrawal conditions came first.** Each proposal said which result would end it. That kept results from turning into arguments.
2. **Estimates and measurements were kept apart.** Values were marked as unmeasured, which decided the next measurement.
3. **Rejections were read narrowly.** Only "when to send" had been ruled out. "Who sends" and "what format to keep" stayed open as separate questions.
4. **Measuring was cheap.** New items went into the same benchmark ROM, so results came back within tens of minutes.
5. **The two sides stood in different places.** Chat questioned premises from outside, and the implementer answered with code and measurements. In a single conversation, that closing paragraph might have gone through unchallenged.
6. **The author made the decisions.** The question, the real-hardware measurements and the exception rule all came from the author. So did the policy against shortcut optimizations, which guided the choices.

## Scope of this record

- The author's question comes from the author's memory. The chat conversation is not in this repository's records. In the documents, Z80 transfer first appears in chat's reply of 2026-10-01, 22:38.
- The original chat documents are not in this repository.
- The gain and the threshold were measured in openMSX. Real-hardware measurements cover the time per byte, the switching round trip and dropped writes.
- The S1990 wait is an interpretation that fits the measurements. Nobody checked the circuit.

## Detailed records (in Japanese)

- [Background method comparison](背景描画方式の比較.md)
- [Reply to the counterargument](背景描画方式_再反論への回答.md)
- [Z80 transfer and line differences](背景描画方式_Z80転送と行差分の検討.md)
- [Packed buffer measurements](背景描画方式_詰めたバッファの計測.md)
- [Verification log](検証記録.md)
