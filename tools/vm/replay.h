/* One sequence language and one document dump, shared by both sides of the
 * differential test.
 *
 * jd: *"Take the original wordpad as reference if needed. Make sure the
 * monkey testing is testing different scenarios, with interaction between
 * all those features."*
 *
 * **The oracle up to now has been our model of a rich edit.** The monkey
 * checks ween32 against what bob believes riched20 does, and the machine is
 * consulted one question at a time, after somebody already suspects
 * something. Nothing compared a *sequence* against riched20. This is the
 * half that makes "ours agrees with our model" into "ours agrees with
 * WordPad", which is the project's premise.
 *
 * **The same header compiles on both sides**: here against ween32, and on
 * the guest against the real riched20 through `zig cc -target
 * x86-windows-gnu`. One serialiser rather than two readings of a
 * description -- the mistake that a written contract invites is each side
 * implementing it slightly differently and the diff blaming the program.
 *
 * ## The language
 *
 * Comma-separated `op:a:b`, ASCII, one line. It is tests/monkey_test.c's
 * own format extended, because that shrinker already emits it and a shrunk
 * differential failure is the report worth having.
 *
 * **Absolute, never relative.** `select:3:7`, not "select three more". A
 * step has to mean the same thing on two documents that may already
 * disagree -- otherwise one divergence makes every later line differ and
 * the shrinker blames the wrong step.
 *
 * **Set, not toggle**, for bold/italic/underline. A toggle is a
 * read-then-write, so the two sides would diverge wherever they merely
 * *report* differently, which is not the bug being hunted.
 *
 * **No mouse.** It cannot be replayed identically across a headless
 * backend and a live guest, so any difference would be a fact about the
 * harness. Dan has the gesture half, one level up.
 */
#ifndef REPLAY_H
#define REPLAY_H

enum {
    OP_TYPE, OP_ENTER, OP_PASTE, OP_BACK, OP_DELETE,
    OP_SELECT, OP_SELALL, OP_CLEAR, OP_HOME, OP_END,
    OP_UP, OP_DOWN, OP_LEFT, OP_RIGHT, OP_REPLACE,
    OP_BOLD, OP_ITALIC, OP_UNDER, OP_SIZE,
    OP_ALIGN, OP_BULLET, OP_INDENT, OP_RESIZE, OP_UNDO,
    OP_N
};

static const char *const op_names[OP_N] = {
    "type", "enter", "paste", "back", "delete",
    "select", "selall", "clear", "home", "end",
    "up", "down", "left", "right", "replace",
    "bold", "italic", "under", "size",
    "align", "bullet", "indent", "resize", "undo"
};

struct rp_step { int op, a, b; };

/* The filler a `paste` or `replace` inserts. Generated rather than stored so
 * both sides produce identical bytes from the same two numbers, and chosen
 * to contain a space and no CR: a paste that carried a line break would mix
 * two questions in one step. */
static void rp_filler(char *out, int n, int seed)
{
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz ";
    int i;
    for (i = 0; i < n; i++)
        out[i] = alphabet[(seed + i * 7) % 27];
    out[n] = 0;
}

#endif
