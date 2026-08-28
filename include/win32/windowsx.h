/* <windowsx.h> off Windows: the names a Windows program includes, answered by
 * ween32. A program written for win32 compiles here with an include path and
 * nothing else — which is what the library is for.
 *
 * This directory is on the include path only for builds that are not
 * Windows. A build targeting Windows must find the real one: include/ itself
 * is on the path there, so these live one directory down where they cannot
 * shadow it.
 */
#include <ween32.h>
