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
    const optimize = b.standardOptimizeOption(.{});

    const mod = b.addModule("ween32", .{
        .root_source_file = b.path("zig/ween32.zig"),
        .target = target,
        .optimize = optimize,
    });

    if (target.result.os.tag == .windows) {
        mod.linkSystemLibrary("user32", .{});
        mod.linkSystemLibrary("gdi32", .{});
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
            "src/gdi.c",
            "src/user.c",
            "src/dialog.c",
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
}
