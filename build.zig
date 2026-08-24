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
    // Which member of the optimize enum means "as fast as it goes" is spelled
    // differently in different Zig versions — `ReleaseFast` in one,
    // `release_fast` in the next — and the type info the enum answers with has
    // changed shape too. So the name is looked for rather than written down:
    // whichever member says "fast" is the one.
    const fast: std.builtin.OptimizeMode = comptime blk: {
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
            if (std.mem.indexOf(u8, &lower, "fast") != null)
                break :blk std.meta.stringToEnum(std.builtin.OptimizeMode, name).?;
        }
        break :blk @enumFromInt(0); // Debug, if nothing says fast
    };

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
            "src/controls.c",
            "src/file.c",
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
    lib.installHeadersDirectory(b.path("include"), "", .{});
    b.installArtifact(lib);

    mod.linkLibrary(lib);
    addExamples(b, mod, target, optimize);
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
