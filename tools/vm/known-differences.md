# What the differential test is expected to disagree about

alice, setting the harness up: *"A difference in the dump is a finding, not
a bug — ours has two faces and the machine has seven. The comparison needs a
known-differences list from its first run, or it drowns and somebody weakens
it to get it green."*

This is that list. **Every entry has to say what was measured and where**, or
it is a place to hide a bug rather than a place to record one.

---

## 1. The trailing paragraph mark, and therefore `len`

```
ours     type a, b, c   ->  len 3   text 61 62 63   char 0..2
riched20 (wine)          ->  len 4                  char 0..3
```

riched20 counts a paragraph mark past the last visible character; `cpMax =
-1` resolves to that. **ween32's length is the visible characters and no
mark.**

**Status: not measured on the machine.** Sam's numbers are from wine, whose
riched20 is not Windows 2000's. **This one is first on the list because if it
holds, every dump differs on `len` and on the last `char` line, and the first
comparison is a hundred per cent noise.**

**And it may be a real divergence rather than an expected one.** A document
that always ends in a paragraph mark is Rich Edit's model, not a rendering
detail — it changes what every index means. If the machine agrees with wine,
this belongs in the tracker and not in this file.

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

## 4. Shift in the repeat count — a fault, not a difference

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
