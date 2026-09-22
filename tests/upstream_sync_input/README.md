# Upstream input regression

Standalone synthetic fixture; no game assets or fonts. Copy this directory to a new `games/TEST_UPSTREAM_INPUT` directory in the emulator/device data root and select it from the launcher.

At 960×544, tap the blue child button at (200,240): the white marker at (200,90) must remain white because its parent group is disabled. Tap green at (600,240), then blue: the marker must turn red. Tap yellow at (600,410), then blue: the marker must stay white because the parent reset removed the child click handler. Green can still respond after reset because it is outside that subtree.

The baseline before upstream `451f842` incorrectly lets the first blue tap turn the marker red. `SYNC-INPUT` log records accompany each action. Keyboard wheel roles are covered by the core unit test; Vita controller keys do not emit a mouse wheel.
