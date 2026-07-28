# OBS Plugin Template

## Controlling Multiviews from obs-websocket

Looking Glass registers an obs-websocket vendor named `looking-glass`, so multiview
windows can be opened, closed, and sent to the main display from outside OBS Studio
(Stream Deck, Companion, custom tooling) — the same actions offered by the
`Tools ▸ Looking Glass ▸ <multiview name>` submenus.

Call them with obs-websocket's `CallVendorRequest` request:

| Request type                 | Description                                                                   |
|------------------------------|-------------------------------------------------------------------------------|
| `OpenMultiview`              | Opens the multiview window, or focuses it if it is already open.               |
| `CloseMultiview`             | Closes the multiview window. Succeeds as a no-op when it is not open.          |
| `SendMultiviewToMainDisplay` | Opens the multiview if needed, then centers it as a 1280x720 window on the primary display. |

Every request takes a single field, `multiviewName`, naming a multiview in the
current scene collection:

```json
{
  "requestType": "CallVendorRequest",
  "requestData": {
    "vendorName": "looking-glass",
    "requestType": "OpenMultiview",
    "requestData": {
      "multiviewName": "Studio Wall"
    }
  }
}
```

The vendor response data carries a `success` boolean, plus an `error` string
describing the problem when `success` is `false`:

```json
{
  "success": false,
  "error": "No multiview named 'Studio Wall' exists in the current scene collection."
}
```

## Introduction

The plugin template is meant to be used as a starting point for OBS Studio plugin development. It includes:

* Boilerplate plugin source code
* A CMake project file
* GitHub Actions workflows and repository actions

## Supported Build Environments

| Platform  | Tool   |
|-----------|--------|
| Windows   | Visual Studio 17 2022 |
| macOS     | XCode 16.0 |
| Windows, macOS  | CMake 3.30.5 |
| Ubuntu 24.04 | CMake 3.28.3 |
| Ubuntu 24.04 | `ninja-build` |
| Ubuntu 24.04 | `pkg-config`
| Ubuntu 24.04 | `build-essential` |

## Quick Start

An absolute bare-bones [Quick Start Guide](https://github.com/obsproject/obs-plugintemplate/wiki/Quick-Start-Guide) is available in the wiki.

## Documentation

All documentation can be found in the [Plugin Template Wiki](https://github.com/obsproject/obs-plugintemplate/wiki).

Suggested reading to get up and running:

* [Getting started](https://github.com/obsproject/obs-plugintemplate/wiki/Getting-Started)
* [Build system requirements](https://github.com/obsproject/obs-plugintemplate/wiki/Build-System-Requirements)
* [Build system options](https://github.com/obsproject/obs-plugintemplate/wiki/CMake-Build-System-Options)

## GitHub Actions & CI

Default GitHub Actions workflows are available for the following repository actions:

* `push`: Run for commits or tags pushed to `master` or `main` branches.
* `pr-pull`: Run when a Pull Request has been pushed or synchronized.
* `dispatch`: Run when triggered by the workflow dispatch in GitHub's user interface.
* `build-project`: Builds the actual project and is triggered by other workflows.
* `check-format`: Checks CMake and plugin source code formatting and is triggered by other workflows.

The workflows make use of GitHub repository actions (contained in `.github/actions`) and build scripts (contained in `.github/scripts`) which are not needed for local development, but might need to be adjusted if additional/different steps are required to build the plugin.

### Retrieving build artifacts

Successful builds on GitHub Actions will produce build artifacts that can be downloaded for testing. These artifacts are commonly simple archives and will not contain package installers or installation programs.

### Building a Release

To create a release, an appropriately named tag needs to be pushed to the `main`/`master` branch using semantic versioning (e.g., `12.3.4`, `23.4.5-beta2`). A draft release will be created on the associated repository with generated installer packages or installation programs attached as release artifacts.

## Signing and Notarizing on macOS

Basic concepts of codesigning and notarization on macOS are explained in the correspodning [Wiki article](https://github.com/obsproject/obs-plugintemplate/wiki/Codesigning-On-macOS) which has a specific section for the [GitHub Actions setup](https://github.com/obsproject/obs-plugintemplate/wiki/Codesigning-On-macOS#setting-up-code-signing-for-github-actions).
