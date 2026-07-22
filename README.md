# macOS build for Coinyecoin 2.3.0-fishsticks

Your tree already has everything needed to build for macOS (depends/hosts/darwin.mk,
contrib/macdeploy, and `make deploy` targets). Put `build-macos.sh` in your source
root. It mirrors your Windows script: same job-count/ccache/nice logic, same
depends system, run in the same Ubuntu container.

## The one real prerequisite: Apple's macOS SDK
Apple's license forbids redistributing their SDK, so it can't be in the repo or
downloaded automatically. You extract it once from Xcode 7.3.1:

1. Download "Xcode 7.3.1.dmg" from https://developer.apple.com/download/all/ (free
   Apple ID). No Mac required to extract, but you do need the file.
2. Extract the SDK using the tool already in your tree:
       ./contrib/macdeploy/extract-osx-sdk.sh        # produces MacOSX10.11.sdk.tar.gz
   (On Linux, install `p7zip-full` first so it can open the .dmg/.pkg.)
3. Drop it where the build expects it:
       mkdir -p depends/SDKs
       tar -C depends/SDKs -xf MacOSX10.11.sdk.tar.gz
   You should now have  depends/SDKs/MacOSX10.11.sdk/

The script checks for that folder and stops with these instructions if it's missing.

## Build
    docker run --rm -v "$PWD":/src -w /src ubuntu:20.04 bash /src/build-macos.sh

First run builds depends (Qt/boost/openssl/... for darwin) — slow, like Windows was.
Re-runs reuse depends + ccache and are fast. Output:
- `Coinyecoin-Qt.app` (+ a zipped copy)
- `Coinyecoin-Core.dmg` if `make deploy` succeeds (the .dmg step is the most
  fragile part of a Linux cross-build; if it fails you still get the .app zip)
- `src/coinyecoind`, `src/coinyecoin-cli`

## CI
`gitlab-ci-macos-job.yml` is a job you can paste into your existing `.gitlab-ci.yml`
next to the windows-* jobs. It caches depends + the SDK. Because the SDK can't be
committed, provision it to the runner once (cache seed or a masked `$MACOS_SDK_URL`);
the file explains both options.

## Honest expectations
This is a 2016 codebase, so like the Windows port expect a short fix-chain on the
first build (a few errors from the newer toolchain meeting old code). Cross-compiling
with the pinned 10.11 SDK is the most reliable route precisely because it recreates
the era the code was written for. Paste any errors and we'll work through them.

### Alternative: build on a real Mac / GitHub Actions macOS runner
If getting the SDK is a pain, you can instead build natively on a Mac (see
`doc/build-osx.md`) or on a free GitHub Actions `macos-*` runner — those are real
Macs with Xcode, so no SDK extraction. The trade-off: a modern Mac + modern Xcode
against 2016 code tends to need *more* fixes than the pinned-SDK cross-build, and
the depends Qt 5.7.1 may not build on Apple-silicon runners (you'd lean on Homebrew
Qt5 instead, with its own API-compat wrinkles). Happy to set that path up instead
if you'd prefer it — just say which way you want to go.
