# What the differential test is expected to disagree about

alice, setting the harness up: *"A difference in the dump is a finding, not
a bug — ours has two faces and the machine has seven. The comparison needs a
known-differences list from its first run, or it drowns and somebody weakens
it to get it green."*

This is that list. **Every entry has to say what was measured and where**, or
it is a place to hide a bug rather than a place to record one.

---

## 1. ~~The trailing paragraph mark~~ — MEASURED, AND IT IS OURS

**Not a known difference. A bug in ween32.** Windows 2000:

```
riched20   type a              len 2      ours   type a         len 1
riched20   type a, b           len 4      ours   type a, b, c   len 3
riched20   type, enter, type   text 61 0d 0a 62, line 1 at 2, len 4
```

The third row settles the shape: the break is **one** character in index
space while being two bytes of text, and `len 4` is a, break, b, **plus a
trailing paragraph mark**. A rich edit document always ends in one.

**This entry was written as "not measured on the machine", and the rule in
this file is why that mattered.** Written as an expected difference — the
convenient reading, and the one that would have made the first comparison
quiet — the model bug underneath it would have been permanently invisible to
the only instrument built to find it.

**It is a model difference, not a rendering one**: every index in every
message means something else, and any program selecting to the end of a
document is off by one against ours. **It stays here as a pointer to the
tracker item, not as a difference we accept** — and until it is fixed every
dump differs on `len` and on the last `char` range, matched as KNOWN and
printed rather than hidden. A quiet run would mean the differ had been taught
to expect a bug.


### What fixing it involves, since "a tracker item" is not a plan

Scoped rather than started, at the end of a long session, because it is a
model change and half of one is worse than none.

**The two quantities are different and ween32 has only one of them.** The text
is `abc`; the document is `abc` plus a paragraph mark. `GetWindowTextA` hands
back three bytes on both sides and always has -- **the disagreement is not in
the text.** It is in what the control answers when asked for its own extent:

```
richedit.c:3155   EM_EXSETSEL with cpMax -1 resolves to the end
ours              e->len          the machine   e->len + 1
```

So the change is a document extent alongside the text length, not a longer
buffer: positions run 0..len inclusive of a mark that is not in `e->text`.
`dump.h` already asks the right question -- it takes `len` from the control's
own resolution rather than from `strlen`, for exactly this reason.

**The work is the audit, not the edit.** `e->len` appears 72 times in
richedit.c and each one is either a text bound (clamp to `e->len`) or a
document bound (clamp to `e->len + 1`), and they are not distinguishable by
reading the name. Caret and anchor become document bounds; every `e->text[i]`
stays a text bound. **A single one of the 72 read the wrong way is a read one
past the buffer**, which the sanitizer will catch, or a selection that silently
drops its last character, which nothing will.

**What it buys**: every index in every message means the same thing on both
sides. What it costs today is three `KNOWN` lines per dump -- visible,
labelled, and not hiding anything, which is why this is a scheduled change
rather than an urgent one.

### Attempted, measured, and reverted -- with the blocker named

Written and taken back out. **The audit half works and is not the problem.**
Splitting the reported selection from the text selection -- `rich_range_said`
for the two messages that answer a program, `rich_range` clamped to `e->len`
for the fourteen that touch bytes -- and resolving `cpMax` of -1 to
`e->len + 1` took seven of nine scenarios from `0 new, 3 known` to
**`0 new, 0 known`**. `len`, the `char` ranges and the `para` ranges all
agreed with the machine exactly.

**What stopped it is one field further in, and the differ said so precisely:**

```
02   machine   char 0..8 B--   char 9..9 --- Arial 200
     ours      char 0..9 B--
```

**The mark is a run of its own and carries the document's default while the
text beside it is bold.** Ours has no format for it at all, so a mark-only
selection collapses under the clamp and answers from the run *before* the
caret. tests/monkey_test.c found the consequence in twenty-two steps --
`bold, type, bold, selall, bold`, and *"a formatting change could not be
undone"*, because nothing had recorded a format for a character with no run.

**Giving the mark the document default fixes the dump and needs three
assumptions, and that is where it was stopped:**

```
measured    the mark carries the default while the text is bold  (02)
NOT         whether a select-all bold can change it
NOT         what it carries when no SCF_DEFAULT was ever sent -- which is
            the monkey's own control, so the fix did not even fire there
```

**Two of those are the shape this file exists to refuse**, and each one was
only visible because the previous one had been tried. That is the whole value
of the attempt: the scope above said "the work is the audit", and the audit
was the easy half.

**One machine sequence unblocks it**: select all, bold, and read the format at
the last index. If the mark stays default, the change is a morning's work with
every number already in `seq/machine`.

## 2. The default face

```
ours     MS Shell Dlg           riched20  System at 9.75pt
```

Measured on the machine, `tools/vm/deffmt.txt`, and reproduced under wine.

**And the System font is bold**, so an unbolded document reads `B--` on the
machine and `---` here. It cost Sam a whole style measurement earlier: he ran
a bold test against a control that was already bold and every row came back
`bold`.

**It stops mattering for WordPad the moment `SCF_DEFAULT` is honoured**,
which it now is — WordPad sets Arial 10 before a character exists, so the
document under test is Arial on both sides. It matters for a bare control.

## 3. Faces available

ween32 has two embedded strikes; the machine has seven faces. Any sequence
that asks for a face we do not have will differ in what the control resolves
to.

**The dump records the face that was *asked for*, not the one resolved**,
precisely so this does not appear on every line. It will still appear
wherever the two disagree about whether a request was honoured at all.

## 4. Which message inserts a paragraph break

```
ween32            WM_CHAR CR inserts;  WM_KEYDOWN VK_RETURN does not
riched20 (wine)   WM_KEYDOWN VK_RETURN inserts;  WM_CHAR CR does not
```

**Exact opposites, and neither single form works on both.** Sam measured the
second under wine, I measured the first here.

The executor therefore sends **both**, which is what a real message loop
delivers — `WM_KEYDOWN`, then `WM_CHAR` via `TranslateMessage` — so each side
acts on the one it recognises and ignores the other. One break on both, for
the right reason rather than a compensating one.

**Status: MEASURED ON THE MACHINE, and it is a ween32 bug.** Windows 2000
agrees with wine exactly — the two single forms are opposite:

```
                        riched20 (machine)   ween32
WM_CHAR CR               nothing              inserts
WM_KEYDOWN VK_RETURN     inserts              nothing
EM_REPLACESEL CR         inserts              inserts
```

A program sending `WM_CHAR` CR to a rich edit gets a paragraph from us and
nothing from Windows; one sending the keydown gets the reverse. **A tracker
item, not a difference we accept** — it is here because the executor
compensates for it, and a compensation has to be visible.


**Had the executor picked one form, every sequence containing `enter` would
have lost its break on one side**, and the diff would have read as *ours
inserts paragraphs that WordPad does not* — a spectacular finding entirely
manufactured by the harness.

## 5. ~~Shift in the repeat count~~ — FIXED

ween32 read Shift out of `lParam` bit 0, where win32 keeps the repeat count,
and Ctrl out of bit 28, which win32 reserves. **So an ordinary synthesised
keydown — repeat count 1, which is what every real keystroke carries — came
out of us as a shift-keystroke.** Not only a harness problem: any win32
program driving a control with `SendMessage` hit it.

```
                                    before          after
WM_KEYDOWN VK_LEFT, lParam 1    selection extended  caret moved
same, Shift actually held       selection extended  selection extended
```

The fix is the one this entry named. `GetKeyState` in win32 answers as of the
message *retrieved*, and ween32 set its modifier state at event ingest while
handlers run at dispatch, so a batched gesture reported the last event's
modifiers to every message in the batch — which is why the state was smuggled
into `lParam` in the first place. The modifiers now travel **with** the queued
message and are restored when it is taken off the queue. `MSG` is win32's own
struct and `make win32` compiles it, so nothing could be added to it: a
parallel ring beside `g_queue`. Nine read sites across five files moved to
`GetKeyState`; Alt stays in bit 29, which is win32's own context code.

**Every one of the tests covering this encoded the bug.** The `key()` helpers
in `edit_test.c`, `richedit_test.c` and `keys_test.c` built their lParam the
way the library read it, and `clip_test.c`, `views_test.c`, `resource_test.c`
and `propsheet_test.c` wrote the bits out by hand. The suite and the library
agreed with each other and neither agreed with win32, so **1223 assertions
passed and would have passed under any convention at all**. That is the same
shape as the paging test computing its expected page from `ween_gui_font()`,
found the same evening, and it is worth naming as a class: *a test that
re-derives the implementation's assumption cannot fail when the assumption is
wrong.* Injecting a keystroke now goes through `ween_set_modifiers`, which is
internal — the tests and the script driver are this library's input system, so
they say what is held rather than encoding how it is stored.

The executor still passes lParam 0 for its key operations. It no longer has to
— both sides now agree with a proper repeat count — and it is left alone only
so the machine dumps do not need taking a fourth time.

## 6. ~~Where a line wraps~~ — DELETED, BY A MEASUREMENT

**This entry is gone from the differ, and it is the only one so far removed
rather than added.** It excused a differing wrap column whenever both sides
wrapped into the same number of lines.

I was wrong about it twice, in opposite directions, and the second time is
the one worth keeping:

```
first    fonts differ, so these three findings are expected     -> excused 03, 06, 07
second   fonts differ, so this "does not go away when a bug is
         fixed, only when the two have the same fonts, which
         is not a goal of this project"                         -> permanently incomparable
```

**Both were the same mistake**: attributing a difference to fonts without
measuring fonts. Sam caught the first by measuring the control (276 against
408) and the second by re-taking the dumps at equal widths:

```
03   ours  0 at 0 len 35 / 1 at 35 len 43 / 2 at 78 len 5    machine identical
07   ours  0 at 0 len 43 / 1 at 43 len 33                    machine identical
```

**The columns agree exactly.** They were incomparable because the two
controls were different widths, not because ween32 has two bitmap strikes.
The font residue I hedged about is, in these scenarios, zero — and I had
declared it permanent.

The rule this file exists to enforce reads backwards as well: **an entry is
added by a measurement, so it is removed by one.** A third rewrite would have
been a third guess.

*What it did not do*: it would not have hidden 06 below. I claimed that when
deleting it and then checked — 06 differs in `len` as well as `at`, and the
tightened form requires every later field to match. What it would have
excused is a line that starts elsewhere and is the same length, which is an
ordinary shifted line table. An excuse that is no longer needed still costs
that.

---

## 06 — CLOSED TWICE, and the second time by shipping Arial

**The wrap column and the length now agree exactly**, which they never have:

```
before Arial existed   ours 0 at 0 len 44        machine 0 at 0 len 40
after                  ours 1 at 40 len 28       machine 1 at 40 len 28
```

**The cause was that our "Arial" was Tahoma.** The measurement below said the
difference was glyph widths and that was right; what it could not say was
*why* our glyphs were narrower, because at the time there was no Arial to
compare against. WordPad asked for Arial, the mapper handed back Tahoma, and
Tahoma's glyphs are narrower than Arial's over that string. Shipping the face
jd asked for closed a wrapping difference nobody had connected to it.

**The only difference left in 06 is entry 7's one pixel** -- `y 16` against
`y 17` -- and that is now true of every remaining difference in the whole set.

### The original reading, which was right about the what and not the why

## 06 — glyph widths, measured

**Sam's re-take moved exactly one thing and it was `y`** — every `at` and
every `len` byte-identical across nine dumps, and `y` entered the contract in
agreement, 1/17/33 on both sides in every scenario **including 06**. So 06's
disagreement was horizontal only: same line tops, different break column.
The old dump could not have said that.

`glyphs.c` then answered the rest. Over 06's own string:

```
idx       ours   machine   diff
  0          9         9      0      -- the selection bar, both
 40        233       241     +8
 44        257       266     +9
 68        397       408    +11
```

**Two false starts, both mine, both worth keeping.**

*First*, I read `x 44` — 257 ours, 266 the machine — against a 256px client
and concluded that under the machine's own rule **we** should break at 40 too,
so ours was over-running its column. Wrong: index 43 is a space, and a
trailing space hangs past the margin rather than counting against it. What is
measured is the last non-space:

```
ours    x 42 'g' 247, x 43 '_' 254   content 254-9 = 245  fits w=246
machine x 44 266, so 'g' near 262    content near 253     does not
```

Both sides follow the same rule, and the column is 246 on our side by direct
reading. **Different glyphs, identical wrap width** — 03 and 07 agreeing
exactly at client 276 says the same thing from the other end.

*Second*, before that I had `11/10` beside `44/40 = 1.10` and called it "a
coincidence of ratios, not a finding". **The ratio was not even a
coincidence** — Sam corrected himself: his probe's "Arial 10" was *bold*,
because a mask of `CFM_FACE|CFM_SIZE` leaves riched20's own effects in place
and its default face is System, which is bold. Named effects take 11px to 9
and meet WordPad exactly. That is entry 2's trap, one field over, and it is
recorded in `seq/machine/README` as having already cost seven findings.

**Do not generalise the direction.** Over this string the machine is wider, so
we fit more characters before a break — the right direction for 44 against 40.
For `w` alone ours is the wider one, 10px against 9. It is per-character and
it goes both ways, so *"the machine's glyphs are wider"* is true of one string
and is not a fact about the fonts.

**No entry, because there is nothing left to excuse.** Entry 6 said this twice
without measuring it and was deleted for it; the difference now is two pixel
tables and a per-character reading, not a plausible sentence.

## What the contract does not cover

alice, on the seven-of-seven result: *"A contract's coverage is a fact about
the contract and belongs next to its results. Otherwise the next person reads
seven of seven and stops."*

**Seven of seven is true of the questions we thought to ask.** It landed in
the same minute as jd demonstrating two defects by hand, and neither was
reachable by any of the seven — not because the sequences were unlucky but
because the contract could not express them. Closed since:

```
right indent       no operation in the language   ->  rindent:TWIPS:0
where a line ENDS  not in the dump                ->  line N at X len Y
the scrollbar      not in the dump at all         ->  vscroll 0|1
```

**The right-indent one is worth reading twice.** `dxRightIndent` was written,
read back, printed and diffed all evening by three instruments and no
character ever moved because of it — **every one checked that a setting was
stored; not one checked that it did anything.** With `len` on the line the
dump says it plainly:

```
no right indent      para ind 0 0 0      line 0 at 0 len 40
right indent 2880    para ind 0 0 2880   line 0 at 0 len 40
```

**Still not covered, and this list is meant to be extended rather than read
past:**

```
not in the dump      anything drawn: the caret's visibility, the bar's
                     position, the ruler, the selection highlight
                     the scrollbar's *threshold* — `vscroll` records the
                     state; what content height raises it is a machine
                     question nobody has asked
not in the language  tabs; anything driven by a mouse — deliberately, since
                     an injected gesture is not replayable on the guest,
                     which leaves §5's drag-and-drop and the selection bar
                     to tests/monkey_test.c alone
not comparable       wrap columns, while the fonts differ (entry 6)
never asked          whether a bullet narrows the wrap, or only moves the
                     line's start
```

**The `y` came out of Sam noticing what the dump could not see.** alice found
the control measures one font and lays out in another; Sam's answer was that
**the harness could not have found it** -- `at` and `len` are character
indices, so a control laying out at 80px a line and one at 16px produce the
same dump. Every scenario here has two lines and not one could see it.

It is the right-indent lesson a second time, one field over: *every
instrument checked that a setting was stored; not one checked that it did
anything.*

## The bullet's wrap term -- written, unverifiable, removed

By the rule the indents established -- every term `rich_line_left` adds,
`rich_wrap_width` takes off -- a bulleted first line should wrap eleven
pixels earlier. I wrote it, and then no text could show it:

```
text "ab ab ab ..." -- a break every three characters
plain                              first line breaks at 45
bulleted                                                45
start indent 165tw, the same 11px                       45
```

Either the subtraction never reached the width or eleven pixels never
crosses a break, and **I could not tell which from the outside** -- that is
the only thing I established.

So it came out. What is measured is where a bulleted line *begins* (Sam's
four rows, the eleven-pixel floor); **whether the line also ends earlier is
a different fact and neither side has been asked.** Sam's reconciliation of
the bullet grid as "one rule with a floor" is an inference over two
measurements, not a reading of either, and shipping on it would have swapped
an unmeasured rule for a *tidier* unmeasured rule. The tidiness is what makes
that hard to see later.

One sequence settles it once there is a machine, and `line N at X len Y` can
already say it.

**A sequence can only find what the language can say and the dump can see.**
That is not a caveat about one run; it is the shape of the instrument, and it
is why jd driving the program keeps finding things four instruments do not.

---

## 7. Arial's line height, one row short

```
Arial 10 at 96 dpi, the caret's height on an empty document
    the machine   16 rows
    ours          15
```

**This is the first honest reading that assertion has ever given.** It has
been green for months while measuring a different font: it says *Arial 10*,
and until `fonts/arial.ttf` existed the request fell back to Tahoma, whose
ppem-13 strike happens to give sixteen. **A check that names one thing and
measures another** -- the same fault Sam found in wordpad's `fontsize.py` the
same evening, and this one had been passing since it was written.

So the claim is not *we regressed by a row*. It is: **we never rendered Arial,
we do now, and its line is one short of the machine's.**

**The arithmetic, because "the fonts differ" is not a cause:**

```
src/font.c   f->ascent = (hhea.ascender * strike_ppem + upem/2) / upem
             1854 * 13 + 1024 = 25126,  / 2048 = 12    -> cell 15
the strike's own sbitLineMetrics say     ascender 13   -> cell 16
Windows, given the same lfHeight -13,    reports       16
```

Ten point at 96 dpi is 13.333 pixels and the strike is stored at ppem 13
because EBLC keys on an integer. Scaling `hhea` by the rounded ppem loses the
third of a pixel that carries the ascender over; the true rasterised ascent at
13 ppem is 13, which is what `mkstrikes.py` writes into the strike and what
`font.c` then ignores.

**Why font.c ignores it, which is the part that makes this a debt and not a
one-line fix.** The comment there says the cell comes from `hhea` rather than
the strike *"whose descender some fonts leave at 0"*. Honouring the strike's
own metrics fixes Arial and **moves every one of Tahoma's eight existing
strikes**:

```
ppem      8    9   10   11   12   13   15   16
strike    6    7    8    9   10   10   12   13
hhea      8    9   10   11   12   13   15   16
```

Nine committed captures agree with the hhea column. So this cannot be changed
for Arial alone.

**It is now the only thing the differential disagrees about.** Twelve
scenarios, and every `NEW` line in every one of them is this pixel:

```
03   line 1  ours y 16  machine y 17      off by one
     line 2  ours y 31  machine y 33      off by two -- it accumulates
06   line 1  ours y 16  machine y 17      all that is left of 06
```

**A one-pixel cell error is one pixel per line and it adds up down the page**,
which is a stronger reason to fix it than the single caret measurement that
found it.

**What retires it.** Rewrite the kept strikes' `sbitLineMetrics` to the values
`font.c` computes today -- which makes honouring them a no-op for Tahoma --
and then read the cell from the strike. Both halves in one change, with the
capture comparison as the proof, and a machine reading of Arial's line height
at two or three more sizes to confirm the ascent rule before it is baked in.
**One data point is what this file exists to refuse building on**, and there is
exactly one here.


## The rule this file exists to enforce

**An entry is added by a measurement, never to make a run green.** The
failure mode alice named is somebody weakening the comparison to get it
passing; the way that happens is a difference being written down as expected
because it was inconvenient, and it is indistinguishable afterwards from one
that was investigated.

## The second rule, which cost more to learn

**A control has to be able to tell the explanations apart, not merely have a
known answer.**

"Run the new probe against a known-good case first" is the rule this project
has been quoting all evening, and it is right, and it is not enough. Twice
tonight a probe was validated against a control that shared the very fault
being investigated, so it came back green and licensed a wrong conclusion:

```
the rect page   the offset was called a bias in the method, on the strength
                of a no-rect control -- the one regime where "a bias" and
                "my control is four pixels narrower than I think" predict
                exactly the same number
the wrap rule   `2 * x0` fitted eight rows, every one of them a symmetric
                rectangle, where it and `f(left) + f(right)` are the same
                arithmetic
```

**Both had a control. Both controls were incapable of failing** for the
specific reason under test, and in both cases the check that settled it was
not a better control but a *case chosen because the two answers disagree
there* -- an asymmetric rectangle, and an origin that moves.

So the question to ask of a control is not *"do I know what this should
say?"* but **"if my explanation were wrong, would this row look different?"**
If the answer is no, it is a demonstration and not a test, however green.

This is the same shape as the six instruments that agreed with the thing they
were measuring -- a test dividing by the same font the code did, helpers
encoding the convention they covered, a probe that never pressed Ctrl, a
runner whose exit status was `tail`'s, a check that passed with the change it
was written to catch. **The fix has been identical every time: a second
sample, chosen because it could disagree.**
