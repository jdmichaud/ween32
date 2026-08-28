//! A program's compiled resources, under the two names the library looks for.
//!
//! On Windows the resources are in the .exe and USER32 reads them from there.
//! Off Windows there is no .exe to hold them, so the build compiles the .rc
//! with `zig rc` -- the same compiler the Windows build uses -- and this
//! object carries the .res it produced into the program, where src/resource.c
//! reads it. The two symbols are declared weak and empty in resource_none.c,
//! so linking this object is what gives a program its menus and its strings.
//!
//! Which .res is the build's business: it arrives as the import "app_res",
//! which build.zig's addResources points at the file `zig rc` just wrote.

const res = @embedFile("app_res");

export const ween_app_resource_data = res.*;
export const ween_app_resource_len: c_uint = res.len;
