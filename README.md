[![CI](https://github.com/TakbirZaman/PolyGrep/actions/workflows/ci.yml/badge.svg)](https://github.com/TakbirZaman/PolyGrep/actions) [![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

PolyGrep is a multithreaded code-search and word-frequency engine written in modern C++17 (STL, std::thread, std::filesystem, CMake, MSVC/MinGW/GCC) with desktop front-ends in Qt 6 Widgets (C++), MFC (C++), and C# WPF plus WinForms (.NET 8, P/Invoke via a C ABI in threadforge.dll), and it works by fanning a recursive directory scan across a bounded C++ thread pool where each worker matches lines (literal Boyer-Moore-Horspool or std::regex) and builds local word-frequency maps that are merged under a lock, while matches stream back thread-safely to whichever UI (queued Qt signals, MFC PostMessage, or a ConcurrentQueue drained by a UI timer in C#) for display with live stats and cancellation.

**Download for Windows (no install needed): [PolyGrep v1.0.0](https://github.com/TakbirZaman/PolyGrep/releases/latest)** — WPF, WinForms and CLI builds.

Free and open source under the [MIT License](LICENSE).

## Use it (no building)

1. Download a zip from **[Releases](https://github.com/TakbirZaman/PolyGrep/releases/latest)** and unzip it.
2. Run `ThreadForge.Wpf.exe` or `ThreadForge.WinForms.exe`: pick a folder, type a pattern, hit **Search** (toggle Regex / case-sensitivity, file extensions, thread count, max results; **Cancel** stops a running scan; double-click a hit to open the file).
3. Or use the CLI: `tf_cli <folder> <pattern> [--regex] [--case] [--ext cpp,h] [--threads N] [--max N] [--words N]` — e.g. `tf_cli src "std::" --ext cpp,hpp --words 5`.

## Build it yourself

**C++ core + CLI + tests (+ Qt GUI if Qt6 is installed):**
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # add -DTF_BUILD_QT=OFF to skip Qt
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```
On Windows with Visual Studio (MFC workload) the same configure also builds the MFC app. If Qt lives elsewhere: `-DCMAKE_PREFIX_PATH=C:/Qt/6.x/msvc2022_64`.

**C# apps (needs .NET 8 SDK; WPF/WinForms need Windows):**
```powershell
cmake --build build --config Release     # produces build\Release\threadforge.dll
cd dotnet
dotnet run --project ThreadForge.Interop.Tests -c Release
dotnet run --project ThreadForge.Wpf -c Release
dotnet run --project ThreadForge.WinForms -c Release
```
