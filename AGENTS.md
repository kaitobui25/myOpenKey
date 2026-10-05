# OpenKey agent rules

## Mandatory custom-build version rule

Current custom Windows build baseline: **26.1**.

Whenever a new user-facing Windows build/release is planned, the agent must update the custom build version **before starting or triggering the build**.

- Increment sequentially by `0.1`: `26.1 -> 26.2 -> 26.3 -> 26.4 -> ...`.
- Do not reuse an already released/built version for a new change set.
- A retry of the **same** failed build does not increment the version again. Increment only when starting the next distinct build/change set.
- Do not trigger the GitHub Actions release/build workflow for a new change set until the version and update text below have been updated.

For every new custom Windows build, update all of these together:

1. `Sources/OpenKey/win32/OpenKey/OpenKey/MainControlDialog.cpp`
   - `CUSTOM_BUILD_VERSION` -> new short version, e.g. `L"26.2"`.
   - `CUSTOM_BUILD_SUMMARY` -> a short, user-facing line beginning with `Cap nhat:` that summarizes the important changes in that build.
2. `Sources/OpenKey/win32/OpenKey/OpenKey/OpenKey.rc`
   - `FILEVERSION` -> matching numeric version, e.g. `26,2,0,0`.
   - `PRODUCTVERSION` -> matching numeric version.
   - String `FileVersion` -> matching string, e.g. `26.2.0.0`.
   - String `ProductVersion` -> matching string.
3. `CHANGELOG.md`
   - Add the new custom-build version at the top with the build date and a concise description of the changes.

Before build/push, verify the displayed custom version, Windows file/product metadata, `Cap nhat:` summary, and changelog all refer to the same version.

This rule is mandatory even if the user forgets to ask for a version bump explicitly.
