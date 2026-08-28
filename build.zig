//! Zig package for ween32: exposes
//!   - module "ween32": the Zig bindings (zig/ween32.zig) — the win32 API
//!     surface, identical on every platform;
//!   - on non-Windows targets, the bindings link the bundled C library
//!     (software renderer + X11 backend);
//!   - on Windows they link the real user32/gdi32 instead: ween32 *is*
//!     win32 there, so the same application code gets the native system.
//!
//! Consume from a build.zig:
//!   const ween32 = b.dependency("ween32", .{ .target = t, .optimize = o });
//!   exe.root_module.addImport("ween32", ween32.module("ween32"));

const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    // A program someone runs rather than steps through, so the default is
    // the fast one: Debug leaves every blit and every bounds check
    // unoptimised, which is the difference between a pencil that follows the
    // pointer and one that lags a tenth of a second behind it.
    // `-Doptimize=Debug` still asks for the other thing.
    // Which member means "as fast as it goes" is asked for by name rather
    // than written down; optimizeNamed below says why it cannot be.
    const fast = optimizeNamed("fast");

    const optimize = b.option(std.builtin.OptimizeMode, "optimize",
        "Prioritize performance, safety, or binary size") orelse fast;

    const mod = b.addModule("ween32", .{
        .root_source_file = b.path("zig/ween32.zig"),
        .target = target,
        .optimize = optimize,
    });

    if (target.result.os.tag == .windows) {
        mod.linkSystemLibrary("user32", .{});
        mod.linkSystemLibrary("gdi32", .{});
        mod.linkSystemLibrary("comctl32", .{});
        mod.linkSystemLibrary("comdlg32", .{});
        // and the one a program without a C runtime would otherwise be
        // missing: the file calls, and what Zig's own start-up needs
        mod.linkSystemLibrary("kernel32", .{});
        addExamples(b, mod, target, optimize);
        return;
    }

    const lib_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    lib_mod.addCSourceFiles(.{
        .files = &.{
            "src/surface.c",
            "src/classic.c",
            "src/font.c",
            "src/marlett.c",
            "src/fonts.c",
            "src/propsheet.c",
            "src/gdi.c",
            "src/draw.c",
            "src/comdlg.c",
            "src/menu.c",
            "src/imagelist.c",
            "src/user.c",
            "src/dialog.c",
            "src/registry.c",
            "src/resource.c",
            "src/resource_none.c",
            "src/controls.c",
            "src/printing.c",
            "src/file.c",
            "src/kernel.c",
            "src/shell.c",
            "src/winmain.c",
            "src/shellart.c",
            "src/cursorart.c",
            "src/headless.c",
            "src/x11.c",
        },
        .flags = &.{ "-std=c99", "-DWEEN_BACKEND_X11" },
    });
    lib_mod.addIncludePath(b.path("include"));
    lib_mod.linkSystemLibrary("X11", .{});

    const lib = b.addLibrary(.{
        .name = "ween32",
        .linkage = .static,
        .root_module = lib_mod,
    });
    // Installed as well, for `zig build install` and anything that consumes
    // the artifact's headers rather than linking the package: ween32.h at the
    // root, and the win32 one-liners beside it. On Windows the package is not
    // built at all and the real headers are the ones found.
    lib.installHeadersDirectory(b.path("include"), "", .{});
    lib.installHeadersDirectory(b.path("include/win32"), "", .{});
    b.installArtifact(lib);

    mod.linkLibrary(lib);
    addExamples(b, mod, target, optimize);
}

/// The member of the optimize enum that means what you asked for, found by
/// name rather than written down: `ReleaseFast` in one Zig, `release_fast` in
/// the next, and the type info the enum answers with has changed shape as
/// well. Ask for "fast", "safe" or "small" and a rename you never see cannot
/// break the build.
///
/// A program building against this package wants it too -- notepad's default
/// is ReleaseSafe -- so it is public rather than a local in build().
pub fn optimizeNamed(comptime want: []const u8) std.builtin.OptimizeMode {
    return comptime blk: {
        const info = @typeInfo(std.builtin.OptimizeMode).@"enum";
        const names: []const [:0]const u8 =
            if (@hasField(@TypeOf(info), "field_names")) info.field_names else nb: {
                var out: [info.fields.len][:0]const u8 = undefined;
                for (info.fields, 0..) |f, i| out[i] = f.name;
                const frozen = out;
                break :nb &frozen;
            };
        for (names) |name| {
            var lower: [name.len]u8 = undefined;
            for (name, 0..) |c, i| lower[i] = std.ascii.toLower(c);
            if (std.mem.indexOf(u8, &lower, want) != null)
                break :blk std.meta.stringToEnum(std.builtin.OptimizeMode, name).?;
        }
        break :blk @enumFromInt(0); // Debug, if nothing carries that name
    };
}

/// Where a C program finds ween32's headers: `#include <windows.h>` has to
/// resolve off Windows, and `<ween32.h>` under it.
///
/// Both are added as include paths straight into the package, rather than
/// left to the copy `installHeadersDirectory` makes, because that copy does
/// not always happen: on the machine this is developed on, ween32's own
/// checkout is an sshfs mount, and installing headers out of one produces an
/// **empty** tree with no error at all. The program then fails on
/// `'windows.h' file not found` while the Windows build of the same source
/// is green, because that one uses the real headers -- so the half that
/// proves the port works is the half that breaks, silently, on a filesystem.
/// An include path is read where it stands and has no such failure.
///
///     const w = b.dependency("ween32", .{ .target = t, .optimize = o });
///     exe.root_module.linkLibrary(w.artifact("ween32"));
///     ween32.addHeaders(w, exe);
pub fn addHeaders(ween32: *std.Build.Dependency,
                  exe: *std.Build.Step.Compile) void {
    exe.root_module.addIncludePath(ween32.path("include"));
    exe.root_module.addIncludePath(ween32.path("include/win32"));
}

/// What to compile, and where its includes come from.
pub const ResourceOptions = struct {
    /// The .rc itself.
    file: std.Build.LazyPath,
    /// Directories its #includes are looked for in -- the one it sits in,
    /// usually, since a resource script includes the program's resource.h.
    include_dirs: []const std.Build.LazyPath = &.{},
};

/// Give a program its resources: its menus, its accelerators, its dialogs and
/// its strings, out of the .rc it already has.
///
/// On Windows this is `addWin32ResourceFile` and the linker puts them in the
/// .exe, which is where USER32 looks. Everywhere else there is no .exe to
/// look in, so the script is compiled with the same `zig rc` and the bytes it
/// produces are carried into the program as an object; ween32 reads the .res
/// itself. Either way the program calls LoadMenu and gets its menu, which is
/// the whole point: nothing in the program knows which of the two happened.
///
///     const w = b.dependency("ween32", .{ .target = t, .optimize = o });
///     ween32.addResources(b, w, exe, .{ .file = b.path("app.rc"),
///                                       .include_dirs = &.{b.path(".")} });
pub fn addResources(b: *std.Build, ween32: *std.Build.Dependency,
                    exe: *std.Build.Step.Compile, opts: ResourceOptions) void {
    if (exe.rootModuleTarget().os.tag == .windows) {
        exe.root_module.addWin32ResourceFile(.{ .file = opts.file });
        return;
    }
    const rc = b.addSystemCommand(&.{ b.graph.zig_exe, "rc", "/fo" });
    const res = rc.addOutputFileArg("app.res");
    for (opts.include_dirs) |dir| {
        rc.addArg("/i");
        rc.addDirectoryArg(dir);
    }
    // Everything after `--` is the input, however it begins: an absolute path
    // starts with a slash and `zig rc` takes options with one too, so without
    // this the script's own path is read as an option and the compile stops
    // on it.
    rc.addArg("--");
    rc.addFileArg(opts.file);

    const blob = b.createModule(.{
        .root_source_file = ween32.path("zig/resource_blob.zig"),
        .target = exe.root_module.resolved_target,
        .optimize = exe.root_module.optimize orelse .Debug,
    });
    blob.addAnonymousImport("app_res", .{ .root_source_file = res });
    exe.root_module.addObject(b.addObject(.{
        .name = "ween32_resources",
        .root_module = blob,
    }));
}

/// The Zig examples. `zig build paint` builds Paint; `zig build` builds it
/// along with the library, on either kind of host.
fn addExamples(b: *std.Build, mod: *std.Build.Module, target: std.Build.ResolvedTarget, optimize: std.builtin.OptimizeMode) void {
    const paint = b.addExecutable(.{
        .name = "paint",
        .root_module = b.createModule(.{
            .root_source_file = b.path("examples/paint/main.zig"),
            .target = target,
            .optimize = optimize,
            // On Windows it carries no C runtime: it reads and writes a
            // .bmp with CreateFile and ReadFile, as a win32 program does,
            // and has nothing else to ask one for. Everywhere else the
            // library it links against is C and wants one.
            .link_libc = target.result.os.tag != .windows,
        }),
    });
    paint.root_module.addImport("ween32", mod);
    // A program with windows, not a console one: without this the loader
    // opens a console beside it.
    if (target.result.os.tag == .windows)
        paint.subsystem = .windows;
    b.installArtifact(paint);
    const step = b.step("paint", "Build Paint");
    step.dependOn(&b.addInstallArtifact(paint, .{}).step);
}
