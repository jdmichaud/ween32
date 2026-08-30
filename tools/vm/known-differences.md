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

## 5. Shift in the repeat count — a fault, not a difference

ween32 reads Shift from `lParam` bit 0 (`src/richedit.c:3766`,
`src/dialog.c:462`) where win32 keeps the repeat count. The executor passes
0 so the two sides agree, which means **this harness cannot detect the
divergence it is compensating for**.

It is here so that the compensation is visible. The fix is in ween32's
message queue: `GetKeyState` alone is not enough, because `g_shift_down` is
set at event ingest (`src/user.c:3033`) and a handler runs at dispatch, so a
batched gesture reports the last event's modifiers. win32 answers as of the
message *retrieved*, which needs the modifier state carried on the queued
message.

---

## 6. Where a line wraps — the column, not the count

```
        ours          the machine
03      line 1 at 44  at 69
06      line 1 at 44  at 40
07      line 1 at 43  at 72
```

**I attributed this to glyph widths and that was wrong, or at least not the
half that mattered.** Sam then measured the control itself:

```
asked for 280x160    ours    client 276x156   (the client edge)
                     machine client 408x289   (the host's client area)
```

**Two controls of different widths wrap at different columns**, and no font
difference is needed to explain most of that. `rp_create` sets the size again
after creating it, so both sides are the size they were asked for whatever
`CreateWindowExA` did with it.

**What I did with the wrong attribution is the part worth recording**: I used
it to turn three findings into expected ones. The differ said `1 new` on 03,
06 and 07; I explained the difference, added the entry, and the run went
green. **That is exactly the failure this file warns about, committed by the
person who wrote the warning** — and the only reason it did not stand is that
Sam measured the control instead of accepting the explanation.

A residual font difference may well remain once the sizes agree. **It has to
be measured before it is claimed again.**

**The line *count* is comparable and is not excused.** All seven scenarios
agree on it — 1, 1, 2, 2, 2, 2, 2 — so a difference in how many lines the
text becomes is still a finding, and the differ matches nothing here when
the counts disagree. **Only the column is unreadable while the fonts
differ.**

This is the one entry that is a *limit of the comparison* rather than a
divergence: it does not go away when a bug is fixed, only when the two have
the same fonts, which is not a goal of this project.

---

## Proving the list has not blunted the instrument

Every time an entry is added, a difference that is **not** on the list must
still be reported. Checked after entry 6 went in, by editing one field of a
machine dump:

```
NEW   sel   8 8   3 7        1 new, 3 known
```

**That check is the point of this section.** The failure alice named is
somebody weakening the comparison to get it green, and a list of excuses
weakens it one entry at a time with each entry looking reasonable. **A run
that can no longer fail is not evidence, and the only way to know is to make
it fail on purpose.**

## The seven-of-seven is withdrawn until the dumps are re-taken

**It was `0 new` on all seven and it should not have been.** Sam questioned
it from the outside, without reading the differ:

> *My three wrapping dumps were taken in a control 408 wide; ours wraps in
> one 280 wide. Their break indices should differ by a lot. You report
> `0 new`.* **Two numbers that should disagree and do not is the same
> warning as two that agree for different reasons.**

He was right, and the cause was entry 6 — mine. The rule excused **any**
`line` difference once the line counts agreed, which is far looser than it
reads: his dumps predate the `len` field, so `1 at 44 len 39` against
`1 at 69` is a different line *shape* and it was waved through as a wrap
column.

**Tightened to the `at` column alone, with every other field identical**,
the same seven now report three and four differences each — the sizes, the
missing `vscroll`, and the line shape. That is the honest state: **the
machine dumps are older than the contract, and the two controls are not the
same width.**

`WS_MAXIMIZE` is why: `0x01000000` is in WordPad's style word, a child
created with it fills its parent's client, and **ween32 does not honour it**
— a divergence in its own right, and the reason the two sides sized their
controls by different rules.

**What this cost was about an hour of a result everybody believed**,
including me, and it was not caught by the instrument. It was caught by
somebody asking why two numbers that had every reason to differ did not.

---

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

## The rule this file exists to enforce

**An entry is added by a measurement, never to make a run green.** The
failure mode alice named is somebody weakening the comparison to get it
passing; the way that happens is a difference being written down as expected
because it was inconvenient, and it is indistinguishable afterwards from one
that was investigated.
