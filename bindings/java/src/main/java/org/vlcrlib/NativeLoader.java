package org.vlcrlib;

import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;

/**
 * Loads the native JNI bridge ({@code vlcr_jni}) together with the FFI library
 * it depends on.
 *
 * <p>Search order: the {@code VLCRLIB_LIBRARY} environment variable (path to
 * the directory or to the library itself), the working directory, then the
 * CMake build tree ({@code build-bindings/bindings/Release}, ...).
 */
final class NativeLoader {

    private static boolean loaded;

    private NativeLoader() {
    }

    static synchronized void load() {
        if (loaded) {
            return;
        }

        String fileName = libraryFileName();
        for (Path candidate : candidates(fileName)) {
            if (candidate == null || !Files.isRegularFile(candidate)) {
                continue;
            }
            loadWithDependencies(candidate.toAbsolutePath());
            loaded = true;
            return;
        }

        // Last resort: the JVM's library path (java.library.path).
        System.loadLibrary("vlcr_jni");
        loaded = true;
    }

    private static String libraryFileName() {
        String os = System.getProperty("os.name", "").toLowerCase();
        if (os.contains("win")) {
            return "vlcr_jni.dll";
        }
        if (os.contains("mac")) {
            return "libvlcr_jni.dylib";
        }
        return "libvlcr_jni.so";
    }

    private static List<Path> candidates(String fileName) {
        List<Path> paths = new ArrayList<>();

        String fromEnv = System.getenv("VLCRLIB_LIBRARY");
        if (fromEnv != null && !fromEnv.isBlank()) {
            Path env = Paths.get(fromEnv);
            paths.add(Files.isDirectory(env) ? env.resolve(fileName) : env);
        }

        Path cwd = Paths.get("").toAbsolutePath();
        paths.add(cwd.resolve(fileName));
        paths.add(cwd.resolve("bindings").resolve("Release").resolve(fileName));
        paths.add(cwd.resolve("bindings").resolve("Debug").resolve(fileName));

        for (Path dir = cwd; dir != null; dir = dir.getParent()) {
            for (String build : new String[] { "build-bindings", "build" }) {
                for (String config : new String[] { "Release", "Debug" }) {
                    paths.add(dir.resolve(build).resolve("bindings").resolve(config).resolve(fileName));
                }
            }
        }
        return paths;
    }

    private static void loadWithDependencies(Path library) {
        Path directory = library.getParent();

        // On Windows a native DLL resolves its imports from its own directory
        // when loaded by full path, but loading the runtime DLLs explicitly
        // makes the failure mode obvious instead of "module not found".
        if (directory != null) {
            for (String dependency : new String[] { "SDL3.dll", "vulkan-1.dll", "libvlc.dll" }) {
                Path path = directory.resolve(dependency);
                if (Files.isRegularFile(path)) {
                    try {
                        System.load(path.toAbsolutePath().toString());
                    } catch (UnsatisfiedLinkError ignored) {
                        // optional dependency
                    }
                }
            }
            // vlcr_jni.dll imports vlcr_ffi.dll; load it first for clarity.
            Path ffi = directory.resolve("vlcr_ffi.dll");
            if (Files.isRegularFile(ffi)) {
                try {
                    System.load(ffi.toAbsolutePath().toString());
                } catch (UnsatisfiedLinkError ignored) {
                    // on non-Windows the FFI library has a different name
                }
            }
        }

        System.load(library.toString());
    }
}
