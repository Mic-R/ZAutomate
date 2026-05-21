ZAutomate THIS CODE IS NOT GOOD BUT IT AT LEAST WORKS AND DOESNT CRASH (I THINK)
=========

WSBF's radio automation system, vintage 2011.
This is dirty vibecode. I plan on making it better.

## C++ Rewrite (2026)

This repository now includes a modern C++ implementation under `cpp/` with:

- A thread-safe, multi-threaded cart queue engine
- Parallel playlist prefetching via a thread pool
- Fully integrated modules: Automation, Studio, and Cart Machine in one executable
- Real WSBF API integration (HTTP calls to production endpoints)
- Minimalist Qt desktop interface with project branding
- Unit/integration-style tests runnable with CTest
- GitHub Actions CI for Linux and Windows
- Automated release workflow with package assets per version

The integrated application header includes:

- Copyright Michael Reimchen

### Build (C++)

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build -j
```

### Run Demo (C++)

```bash
./cpp/build/zautomate
```

The CLI fallback is still available as:

```bash
./cpp/build/zautomate_cli
```

Inside the GUI, use the dashboard tabs for:

- Automation queue control
- Studio search
- Cart machine cache refresh

Hidden easter egg: use `Ctrl+Shift+B` or the App menu to reveal the hidden card.

### Run Tests (C++)

```bash
ctest --test-dir cpp/build --output-on-failure
```

### Build Debian Package

```bash
cpack --config cpp/build/CPackConfig.cmake
```

This produces `.deb` packages on Linux.

### Build Windows Package

On Windows runners, the same CPack step produces `.zip` distributables.

### Versioned Releases

Push a tag to create a release with packaged artifacts:

```bash
git tag v1.0.0
git push origin v1.0.0
```

The workflow in `.github/workflows/release.yml` builds, tests, packages, and publishes release assets automatically.
