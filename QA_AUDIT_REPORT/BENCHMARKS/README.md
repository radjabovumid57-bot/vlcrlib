# Benchmarks

Windows MSVC x64 Release, RTX 5050, Vulkan 1.4.351, C17.

`windows_stress_1k.csv`, `windows_stress_10k.csv`, `windows_stress_100k.csv`
contain per-frame `qa_stress` data (CPU ms, draws, triangles).

Headline (final C17 runs): 1k = 9.81 ms CPU / 0.355 ms GPU; 10k = 42.81 ms /
3.147 ms; 100k = 390.25 ms CPU (timestamp sample unavailable). 100k is
CPU-submission bound. No Android/Linux/macOS runtime benchmark was possible.
