# Screen Vision Automation

Desktop automation driven by real-time screen capture and graphics pattern matching.

## Overview

A console application for Windows. It watches a selected application window through screen capture, searches the captured frames for predefined graphics patterns, and triggers actions through keyboard and mouse input emulation. Which action is triggered depends on the state recognised on screen.

The engine is not tied to one application: patterns, detection rules and the actions bound to them are described in JSON configuration files, so supporting another DirectX application means writing a new configuration rather than new code. The current configuration set was written against the EVE Online client, which served as the test target during development.

## Implementation notes

Written in C++17 for predictable performance of the capture and matching loop. Built with CMake and CMakePresets; third-party dependencies are managed with vcpkg. Multiple patterns can be detected in a single frame. Developed in VSCodium with clangd.

Configuration files: eve_config.json (behaviour), eve_images.json (pattern library), prg_params.json (runtime parameters).

## Related project

An earlier version of the same idea, built for Linux and targeting Android applications, captures video from a device through scrcpy and V4L2 and detects patterns with OpenCV. It now lives in its own repository: android-screen-automation.
