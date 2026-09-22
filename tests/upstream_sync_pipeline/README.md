# Input pipeline regression

Deploy this directory separately as `games/TEST_INPUT_PIPELINE` and copy
`host/assets/menu.ttf` to `font.ttf`. No game resources are used; this fixture
is not bundled in VPKs. Screenshots use only its own save directory.

1. Circle on page 1 must log `masked circle` without leaving the page.
   Left releases the mask; Circle then enters page 2.
2. Circle on page 2 must execute the handler, emit the deferred dummy decide,
   then enter page 3. Falling through the handler in the same frame fails.
3. Circle on page 3 must remain there with filter verdict 1. Left selects
   verdict 2; Circle then enters page 4 by default-role fallback.
4. Touch (200, 445) on page 4. The fully transparent interactive layer must
   receive exactly one click and advance through the dummy decide to PASS.
5. Triangle captures `save/capture.png`; the log must contain `INPUT-TEST PASS`
   and no `INPUT-FAIL`. Repeat the prior save/load fixture after this test.

The pre-sync core fails step 1, providing a negative control. Unit tests also
cover repeat keys, source-alpha thresholds, hover invalidation, and filtered
handler outcomes.

For captured-pointer regression, deploy a second copy as `TEST_INPUT_DRAG`,
changing both BOOT entries in its system.ini to `drag.iet`. Drag the block
from (150, 445) toward (550, 445), then drag its new center back toward
(300, 445). Select the visible block interior rather than its right edge;
the final movement sample can coincide with the touch release.
The script masks key 1 while captured; each movement and release must still
complete, producing `INPUT-DRAG PASS 1` then `INPUT-DRAG PASS 2`.
