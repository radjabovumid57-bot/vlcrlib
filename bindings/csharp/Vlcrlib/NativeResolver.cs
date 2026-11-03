using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;

namespace Vlcrlib
{
    /// <summary>
    /// Finds vlcr_ffi.dll and makes sure its runtime dependencies (SDL3.dll,
    /// vulkan-1.dll, ...) are loadable.
    ///
    /// Search order: VLCRLIB_LIBRARY, the app directory, runtimes/&lt;rid&gt;/native,
    /// then the CMake build tree (useful when running examples from a checkout).
    /// </summary>
    internal static partial class Native
    {
        static Native()
        {
            NativeLibrary.SetDllImportResolver(typeof(Native).Assembly, Resolve);
        }

        private static IntPtr Resolve(string libraryName, Assembly assembly,
                                      DllImportSearchPath? searchPath)
        {
            if (!string.Equals(libraryName, Lib, StringComparison.Ordinal))
                return IntPtr.Zero;

            foreach (string candidate in Candidates())
            {
                if (!File.Exists(candidate))
                    continue;

                PreloadDependencies(Path.GetDirectoryName(candidate)!);

                if (NativeLibrary.TryLoad(candidate, out IntPtr handle))
                    return handle;
            }

            // Fall back to the default probing (PATH, app directory, ...).
            return IntPtr.Zero;
        }

        private static string[] Candidates()
        {
            string? fromEnv = Environment.GetEnvironmentVariable("VLCRLIB_LIBRARY");
            string baseDir = AppContext.BaseDirectory;
            string fileName = OperatingSystem.IsWindows() ? "vlcr_ffi.dll"
                : OperatingSystem.IsMacOS() ? "libvlcr_ffi.dylib"
                : "libvlcr_ffi.so";

            var candidates = new System.Collections.Generic.List<string>();
            if (!string.IsNullOrWhiteSpace(fromEnv))
                candidates.Add(fromEnv);

            candidates.Add(Path.Combine(baseDir, fileName));
            candidates.Add(Path.Combine(baseDir, "runtimes", "win-x64", "native", fileName));

            // Walk up looking for a CMake build tree next to the sources.
            DirectoryInfo? dir = new DirectoryInfo(baseDir);
            for (int depth = 0; dir != null && depth < 8; depth++, dir = dir.Parent)
            {
                candidates.Add(Path.Combine(dir.FullName, "build-bindings", "bindings", "Release", fileName));
                candidates.Add(Path.Combine(dir.FullName, "build-bindings", "bindings", "Debug", fileName));
                candidates.Add(Path.Combine(dir.FullName, "build", "bindings", "Release", fileName));
            }

            return candidates.ToArray();
        }

        private static void PreloadDependencies(string directory)
        {
            if (!OperatingSystem.IsWindows())
                return;

            foreach (string dependency in new[] { "SDL3.dll", "vulkan-1.dll", "libvlc.dll" })
            {
                string path = Path.Combine(directory, dependency);
                if (File.Exists(path))
                {
                    try { NativeLibrary.Load(path); }
                    catch (Exception) { /* optional dependency */ }
                }
            }
        }
    }
}
