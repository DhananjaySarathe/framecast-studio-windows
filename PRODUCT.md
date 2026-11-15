# Framecast Studio

<!-- impeccable:product-schema 1 -->

## Platform

adaptive

Native desktop application for Windows 11 Intel/AMD laptops and desktops. The Mac app is an external visual reference in its own repository; this checkout contains only Windows code.

## Product Purpose

Create, preview, record and broadcast a show in landscape and portrait at the same time. The studio supports a solo presenter managing cameras, screen sharing, audio and a show rundown.

## Capabilities and Constraints

The Swift Mac application is the existing feature reference. The Windows C++ engine implements capture, composition, audio mixing, recording and streaming; the Windows interface must call that engine. Windows hardware capture still needs verification on Windows. Future parity includes guests, captions, chat, camera angles and AI direction; unsupported features must not appear connected or active.

## Brand Commitments

The user explicitly requires the Windows studio to keep the Mac application's UI and features. Preserve its composition, palette, terminology and workflow. Use ordinary Windows window chrome and keyboard shortcuts where platform conventions differ. Do not redesign the studio or introduce invented content.

## Evidence on Hand

`Sources/Framecast/Theme.swift`, `StudioView.swift`, `ScenesSidebar.swift`, `ProgramView.swift`, and the committed screenshots under `docs/screenshots/` define the incumbent visual system. Current source takes precedence when an older screenshot differs.

## Product Principles

- Preview and on-air state must be distinguishable.
- Recording and broadcast controls operate independently.
- Capture starts after the user explicitly selects sources.
- Show failures with a clear recovery action; do not fabricate connection status.
- Keep source capture and output work off the interface thread.
