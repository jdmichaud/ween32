/* Where a Windows program starts.
 *
 * A win32 GUI program has no main: it has WinMain, and what calls it is the C
 * runtime's startup code, which the linker puts in front of it. That is the
 * one piece of a Windows program that is not the program's own, and off
 * Windows it has to come from somewhere -- so it comes from here.
 *
 * This lives in a file of its own, and is weak, for the same reasons the
 * empty resource data is: a program with a main of its own -- every example
 * here, and every Zig program, whose start-up code defines one -- keeps it,
 * whichever order the linker happens to read the archive in.
 */

#include "ween_internal.h"

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR cmd_line,
                   int show);

/* Weak, so that a program with a main of its own -- every example here, and
 * any Zig program, whose start-up code defines one -- keeps it. The archive
 * hands this over only when nothing else has. */
__attribute__((weak)) int main(void)
{
    /* The command line WinMain is handed is what follows the program's own
     * name -- Windows passes the arguments only, where GetCommandLine gives
     * the whole thing including the name. */
    char *line = GetCommandLineA();
    char *args = line;
    if (*args == '"') {
        args++;
        while (*args && *args != '"')
            args++;
        if (*args == '"')
            args++;
    } else {
        while (*args && *args != ' ')
            args++;
    }
    while (*args == ' ')
        args++;
    /* SW_SHOWDEFAULT is what a program started from a shortcut is given, and
     * what every program passes straight to ShowWindow. */
    return WinMain(GetModuleHandleA(NULL), NULL, args, SW_SHOWDEFAULT);
}
