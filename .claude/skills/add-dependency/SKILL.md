---
name: add-dependency
description: Add, upgrade or remove a third-party C++ library in the Lmao engine as a git submodule. Use when asked to add, pull in, upgrade, pin or remove a library or dependency.
---

# Adding a dependency (git submodules only)

Never use vcpkg, Conan or CMake FetchContent. Never edit files inside a submodule.

## Add
1. Check the license is permissive (MIT, BSD, zlib, Apache 2.0, public domain, FTL). Ask the user before adding anything GPL/LGPL.
2. Add and pin it:
   ```powershell
   git submodule add <url> third_party/<name>
   git -C third_party/<name> tag --sort=-creatordate | Select-Object -First 10   # pick the latest release
   git -C third_party/<name> checkout <tag>
   git config -f .gitmodules submodule.third_party/<name>.shallow true          # if the repo is large
   ```
3. Create its target in `third_party/CMakeLists.txt`:
   - **Has CMake:** set its options *before* `add_subdirectory` with `set(<OPT> OFF CACHE BOOL "" FORCE)`. Turn off tests, examples, docs, install. Then `add_subdirectory(<name> SYSTEM EXCLUDE_FROM_ALL)`.
     Verify every option name exists: `Select-String -Path third_party/<name>/CMakeLists.txt -Pattern 'option\('`.
   - **No CMake:** write a `STATIC` or `INTERFACE` wrapper target with `target_include_directories(... SYSTEM ...)`.
   - **Single-header with an implementation macro:** generate one `_impl.c`/`.cpp` (see the miniaudio wrapper) or document which engine `.cpp` defines it.
   - Make sure it does not download anything at configure time. Disable any such options and add the extra dependency as its own submodule.
   - Add the target to the `tp_folder(...)` call.
4. Add a line to `third_party/LICENSES.md`: name, URL, tag, license.
5. Configure and build (`engine-build` skill) to confirm it works.
6. Commit the `.gitmodules` change, the submodule pointer, the CMake change and the license line together: `Add <name> <tag>`.

## Upgrade
One library per commit. `git -C third_party/<name> fetch --tags`, check out the new tag, re-verify the option names, build, then commit `Bump <name> <old> -> <new>`.

## Remove
```powershell
git submodule deinit -f third_party/<name>
git rm -f third_party/<name>
Remove-Item -Recurse -Force .git/modules/third_party/<name>
```
Then remove its CMake block and `LICENSES.md` line.

## Not submodules
Large tools (Slang/DXC compilers, Vulkan SDK layers, the D3D12 Agility SDK) are downloaded as pinned prebuilt releases by `scripts/setup.ps1` into `tools/bin/` (gitignored).
