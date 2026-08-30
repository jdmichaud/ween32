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

## The rule this file exists to enforce

**An entry is added by a measurement, never to make a run green.** The
failure mode alice named is somebody weakening the comparison to get it
passing; the way that happens is a difference being written down as expected
because it was inconvenient, and it is indistinguishable afterwards from one
that was investigated.
