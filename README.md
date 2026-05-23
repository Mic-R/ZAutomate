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

## Installation Wizard / Installer Artifacts

Release builds now produce installer-friendly artifacts for all target platforms:

- Ubuntu: `.deb` package (plus `.tgz` archive)
- Windows: `.zip` bundle, and optional NSIS installer `.exe` (wizard)
- macOS: `.dmg` installer image (plus `.tgz` archive)

To enable the Windows installer wizard, set repository variable `ENABLE_NSIS=ON`.

## FTP Deployment for Release Artifacts

After a tag-based release is built and the GitHub Release is created, artifacts are uploaded to your FTP server.

Required repository secrets:

- `FTP_SERVER`
- `FTP_USERNAME`
- `FTP_PASSWORD`
- `FTP_TARGET_DIR` (remote directory path)

Release files are uploaded into a versioned subfolder automatically:

- `FTP_TARGET_DIR/<tag>/`
- Example: `/releases/v1.1.0/`

Example release trigger:

```bash
git tag v1.1.0
git push origin v1.1.0
```

## Windows: App icon and SmartScreen (code signing)

If the app icon does not appear in the Windows taskbar, ensure an ICO file is available at `cpp/src/gui/resources/app.ico` — the build embeds that into the EXE on Windows using `cpp/src/gui/windows/app.rc`.

SmartScreen warnings are triggered by unsigned binaries or low-reputation publishers. To avoid the SmartScreen warning you must sign your executable and installer with a code signing certificate. Recommended approaches:

- Purchase an EV code signing certificate and sign both the EXE and the NSIS installer using `signtool.exe` (Windows) or `osslsigncode` (cross-platform). Timestamp signatures so they remain valid after certificate expiry.
- On Windows, use:

```powershell
& "C:\Program Files (x86)\Windows Kits\10\bin\x64\signtool.exe" sign /fd SHA256 /tr "http://timestamp.digicert.com" /td SHA256 /a "path\\to\\certificate.pfx" /p "$env:PFX_PASSWORD" "path\\to\\zautomate.exe"
```

- You can automate signing in GitHub Actions by providing a base64-encoded `.pfx` in a secret and using a step that decodes and calls `signtool` on a Windows runner. EV certs build reputation faster and reduce SmartScreen prompts.

I can add an example GitHub Actions signing step if you want; you'll need to provide the certificate and any required secrets.  
