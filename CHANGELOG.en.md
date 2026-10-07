# Changelog

[简体中文](CHANGELOG.md) | [English](CHANGELOG.en.md) · [README](README.en.md)

Features, compatibility fixes, and performance changes in Art3m1sPSV, including PSV core changes pinned by each application version. Versions are listed newest first. Historical entries are based on Git tag descriptions and their commits; dates use the tagged commit's date.

New changes go under **Unreleased** and are moved into a dated `vMAJOR.MINOR.PATCH` section when released. Beta tags on the same commit are listed as aliases, not separate releases. Experiments, temporary diagnostics, and unaccepted working-tree changes are not treated as released features. Performance improvements in individual scenes do not imply the same frame rate in every game or effect.

## Unreleased

## v1.3.13 — 2026-10-08

- Check for updates on the game selection screen and show the notice only there. Hide it in games and other menus, and restore it when returning to the game list.
- Restyle the About button with a dark outlined surface and information icon, shifted up by 1 pixel. Move other host UI text up by 1 pixel while preserving original game-list text baselines.
- Add tests for bilingual update-notice drawing, real Release response parsing, and persistent visibility.

## v1.3.12 — 2026-10-08

- Show a one-time resource preparation notice after language selection when no external game resources are found, and save acknowledgement. Highlight the companion conversion tool and resource location at the top of both READMEs.
- Directly upload pre-swizzled DXT1/BC1 and DXT5/BC3 textures from image DDS files and E-mote PSB models.
- Bundle only the complete STARWIND_DEMO.

- Check stable GitHub Releases asynchronously once per app session when opening the first game. A newer release with an uploaded VPK produces a persistent silent notice in the top-right corner, which disappears after installing that version or newer. Failed checks do not interrupt gameplay; the HTTPS client and CA bundle are included.

- Added an EXE icon fallback when no usable PNG or PFS-matching EXE exists: cache an icon only if exactly one EXE in the folder yields one. Revalidate changed source sets to avoid stale ambiguous icons.

- Added a per-game Audio fade in/out switch, off by default, for extra short ramps on forced stops and immediate replacements. Script-requested fades remain unchanged.

## v1.3.11 — 2026-10-05

- Open and prime paired BGM loop segments in the background for direct handoff after the intro; release paired resources on stop, replacement, or cancellation and share the existing compressed-audio budget.
- Prepare BGM and looping sound decoders in the background while preserving gain, pan, and loop-file settings; prioritize playback reads over shared PFS preload traffic to reduce audio gaps during scene/UI loading.

## v1.3.10 — 2026-10-05

- Game folder names now support Chinese, Japanese, spaces, and common symbols, with path safety checks and a 100-byte UTF-8 limit.
- Standardized font-setting role labels as `Dialogue` / `Subtitle`, with matching documentation and illustrations.
- Removed incorrect clock-control plugin requirements and the development-testing status from the README.

## v1.3.9 — 2026-10-05

- Font settings now label sizes, offsets, and subtitle visibility by the script roles `dialogue` / `subtitle`, rather than implying Chinese/Japanese language detection. Stored settings and behavior are unchanged.
- Moved English setting-row labels and values up by 2 pixels for better vertical alignment, and refreshed the English illustrations. Titles, help text, the Chinese UI, and game text are unchanged.
- Added seven English menu previews to the English README, rendered from the current menu code, font, and atlas. Chinese illustrations are unchanged.

## v1.3.8 — 2026-10-05

- Added English README and CHANGELOG documents, including historical release notes and links between Chinese and English versions.
- Added an English UI. On first launch, choose Simplified Chinese or English; the saved choice prevents repeated prompts. Language can also be changed immediately in the launcher's **Language / 语言** setting. Covers game selection, settings, loading messages, About, and the host game menu without changing game dialogue language.

## v1.3.7 — 2026-10-04

- Fixed layout width being lost for spaces without glyph outlines. Font-size changes now retain spaces and reflow the text.
- Registered name and dialogue text layers in the original demo and removed the temporary per-word English layer splitting, allowing font-size overrides to work in all three languages. Retained cleanup of text layers from older demo saves.
- Bundled the complete Starwind Observatory demo and standalone E-mote demo in the VPK, including PFS resources, icons, and titles. Both offer Chinese/Japanese/English selection at the beginning and Japanese voices from VOICEVOX:ナースロボ＿タイプＴ.
- Added Chinese, Japanese, and English end credits identifying Codex-generated character, background, and dialogue assets, with voice attribution and source links.
- Built-in resources are read from app0; settings, caches, and saves still use ux0. External demos with the same name take precedence. Local builds and GitHub Actions verify bundled files and checksums against their manifest.

## v1.3.6 — 2026-10-04

- Fixed full startup self-tests still running behind a black screen when Debug was off. Normal startup only initializes validated rendering paths; Debug enables and displays self-tests. Normal rendering optimizations and the independent cache overlay remain available.
- Added per-game OGV preloading and caching, enabled by default with limits of four groups / 16 MiB. A color video and its mask form one group within the shared cache budget. Files are read in background chunks and reused for playback and looping; over-limit groups keep streaming. The overlay reports usage, group counts, and hits.
- Updated README menu screenshots and feature descriptions, including CPU image compression, OGV caching, background alpha, and cache-overlay fields.

## v1.3.5 — 2026-10-04

- Added an approximately 12 ms fade-out when audio is forcibly stopped, immediately switched, or playback exits. It reuses decoded PCM without additional file reads, preserves script-requested fades, and adds output-interval diagnostics. Waveform and resource-release tests passed; listening validation on hardware remained pending.
- Standardized the application title, launcher, About page, and VPK filename as Art3m1sPSV. Added a PSV icon based on the upstream logo, with attribution in Credits.
- Fixed save/load actions registered only with high-bit PC key codes being unavailable from the in-game menu. Added declared Backlog, dialogue-box, and choice navigation for scripts without directional bindings, while preserving existing bindings and input filters.
- Fixed pan-cache preparation stopping after a single eviction. If actual free video memory remains insufficient, it now makes bounded additional attempts, preserves a safety margin, and records the remaining shortfall. Allocation failures also trigger bounded fragmentation handling with the safety margin retained.
- Fixed some delayed dialogue lines refusing to advance early on confirmation. Timed waits that permit input now accept confirmation forwarded by game Lua. Unskippable waits, auto-mode exit, and menu input blocking remain intact. Runtime regressions and hardware acceptance passed.
- Added per-game **CPU image compression**, off by default. Users can select folders and independently configure decoded size, all-zero RGBA pixel ratio, and average zero-run length thresholds. Eligible images use ZeroSpan32 caching with direct restoration into GPU texture memory.
- Documented its use for sprites and portraits with large transparent areas. Default thresholds are 512 KiB and a 60% all-zero pixel ratio.
- Fixed missing portrait and expression-layer preloads, using actual filename resolution rules and scheduling preloads and eviction in story order.
- Unified pan-cache reuse for eligible effects. Gaussian blur, existing Kawase blur, and eligible multilayer results are reused while content, relative layer positions, and effect parameters remain unchanged. Content changes or movement beyond cached bounds trigger rebuilding.
- Preserved screen-edge handling for full-resolution Gaussian blur and separated coordinate-dependent outer effects from cacheable inner effects.
- Transparent sprites and expression layers can restore only their nontransparent area from ZeroSpan32 into a smaller GPU texture while retaining original image dimensions and coordinates. Unsupported shaders, clipping, and interactive layers retain the full-texture path.
- On texture allocation failure, rebuildable effect caches are evicted between frames first. Resources used by the current scene, transitions, and screenshots remain protected, avoiding persistent allocation failures.
- Separated platform resource status from setting rows to fix overlapping menu text.
- Added a versioned changelog and README link, and corrected the separate core repository's license link.
- Validation: 682 core tests passed, along with audio fade/resource-lifetime, effect-cache pressure, and version packaging/release tests. Dialogue skipping passed hardware acceptance. Other validation scope is noted above; high-load coverage remains ongoing.

## v1.3.4 — 2026-09-30

- Added GXM-native compressed textures in DDS/PVR containers: BC1–BC5, including supported signed variants, PVRTC/PVRTC2, and ETC1. Preload caches and native uploads retain compressed data.
- Added per-game **Ignore BG alpha**, ultimately off by default. It may reduce background compositing cost but can break scenes requiring transparency.
- Adjusted story image preparation, pre-upload, and failure retries to reduce repeated work during sprite switches.
- Extended loading progress through staged initialization until the first game frame is complete.
- Raised playback and voice-preparation thread priorities to reduce audio stalls under load.

## v1.3.3 — 2026-09-30

- Check matching image resources before generating platform fallback tables, preventing fallback based only on a platform name or table with no usable resources.
- Show platform resource status in game settings, distinguishing matched resources, fallback tables, and missing resources.
- Added independent Japanese secondary-text font size and standardized menu text sizes and layout.
- Show the build version on the About page; removed the duplicate top-toolbar visibility option from global launcher settings.

## v1.3.2 — 2026-09-29

- Added independent horizontal/vertical offsets for Chinese primary text and Japanese secondary text, plus **Hide Japanese**. Options are off by default and apply only to the current game.
- Added top-toolbar and dialogue-volume-bar visibility options to the in-game host menu, saved independently and applied immediately.
- Switched PSV archive reads to native 64-bit seeking, fixing large-file offset problems.

## v1.3.1 — 2026-09-29

- Added game-list icons and an About page with developer, project URL, and credits.
- Support common PNG icon locations. When no icon exists, extract and cache one during game loading from an EXE with the same base name as the PFS.
- Added a logging switch enabled by default; changed the default E-mote Mesh value from `0.4` to `1.0`.
- Extended boot entry points and platform-table fallback for Vita, Windows, Switch, Android, iOS, and PS4.
- Fixed script block comments, startup font state, and font-setting application failures, with improved error logging.

## v1.3.0 — 2026-09-29

- Improved E-mote model display, motion, expressions, lip sync, and nested mask compatibility.
- Support native BC3/DXT5 model texture uploads to reduce expansion and copying.
- Optimized pose, mesh, vertex, and mask reuse, and reduced mask compositing/clearing regions where safe.
- Added PSB preloading, parsed-model caching, and coordination with the shared memory budget. The debug cache overlay shows model usage and hits.
- Added per-game E-mote Mesh detail and independent E-mote CPU/ES4 settings, requesting 444/222 MHz by default.
- Added per-game top-toolbar hiding.

## v1.2.20 — 2026-09-27

Beta alias: `beta1.3.0`.

- Cache blur results with edge margins and reuse them during eligible background pans, reducing repeated blur passes.
- Preserve the opaque property of large RGB24 images using opacity evidence obtained during decoding.
- Added independent CPU and ES4 settings for effect pans, off by default; restore the applicable clocks when the scene ends.
- Adjusted the clock-settings layout to fix overlapping help text after adding new options.

## v1.2.19 — 2026-09-25

- Cache the source image for static Screen blending while preserving its blend with the live background on reuse.
- Optimized transparent single-image compositing while retaining alpha and rounding behavior.
- Fixed callback state, dialogue text, and wait-state restoration after loading saves.
- Save/load menu caches may remain after closing and are evicted first when other resources need space.

## v1.2.18 — 2026-09-25

- Reduced unnecessary menu-resource maintenance during playback.
- Reused stable overlay rendering results and narrowed effect-input dependencies to masks actually referenced.
- Retained cache budgets and image quality. Stalls in complex blur transitions remained a known limitation.

## v1.2.17 — 2026-09-25

- Optimized story-timeline preloading, UI cache eviction, staged GPU pre-upload, and first-frame effect-input reuse to reduce scene-entry stalls.
- Reduced repeated file parsing and adjusted archive reads to accommodate audio.
- Fixed optional mask parameters and animation-mask handling so boolean values are not treated as resource paths.
- Fixed Screen-blend alpha and added `L + START` as a mouse right-click shortcut.
- Added GitHub Actions support for publishing verified tag-build VPKs to their corresponding GitHub Releases.

## v1.2.16 — 2026-09-22

Beta alias: `beta1.2.0`.

- Use GXM for OGV YUV420P/YUV422P color and alpha conversion.
- Moved the production core to a pinned submodule of the separate PSV fork and adapted selected upstream input and save-restoration fixes.
- Added automatic GitHub Actions builds and verification, with versioned artifact and VPK filenames.
- Removed demos from the VPK at this version; generated test resources remained in local temporary directories.
- Added a Debug switch, off by default, controlling startup shader self-test display; background capability validation was retained at this version.
- Improved resource adaptation instructions, controls, and open-source acknowledgments.

## v1.2.15 — 2026-09-22

Beta alias: `beta1.1.0`.

- Integrated the production core and native GXM host, organizing source, dependency patches, and build entry points.
- Removed unused hosts, UI libraries, and tools; separated build outputs, temporary files, and backups.
- Derive VPK versions from official tags and retain separate packages and verification records for each build.
- Added the public README with installation, usage, and resource adaptation instructions.

## v1.2.14 — 2026-09-21

- Prioritize preloads requested by game Lua cache scripts and protect decoded images nearing first use.
- Show completed / planned Lua preloads in the debug cache overlay.
- Fixed restoration of layered character resources under GPU cache pressure.
- Consolidated PSV shader sources under `shaders/psv/`.

## v1.2.13 — 2026-09-20

Beta alias: `beta1.0.0`.

- Use single-channel alpha glyph atlases for dialogue, names, and Backlog. Pixel storage per 512 × 512 atlas decreased from 1 MiB to 256 KiB.
- Preserve Gray8 masks as single-channel GXM textures to reduce system and video memory usage.
- Retained font sizes and outline algorithms. This tag did not claim to fix the intermittent missing character layers that were still undiagnosed at the time.

## v1.2.12 — 2026-09-20

- Improved mosaic-input reuse and shared caching of nested shader inputs.
- Fixed scene caches being replaced by input caches, repeated texture uploads, and layered parameter combinations, reducing grayscale scene-entry stalls.
- Consolidated supplemental shader outputs and shared native GXM host code.
- First resource uploads and first effect compositions could still stall.

## v1.2.11 — 2026-09-20

- Smoothed cached text outlines, fixing gaps and protrusions.
- Adjusted transparent-edge sampling in glyph atlases to reduce rough text edges while preserving font size and layout.

## v1.2.10 — 2026-09-20

- Added bounded eviction of remaining idle GPU caches and retry when H.264 hardware decoder initialization fails, reducing premature software fallback under memory pressure.
- Improved actual CDRAM reclamation and video-fallback diagnostics. Some recovery scenarios still required validation at this version.

## v1.2.9 — 2026-09-20

- CPU decoded backups dynamically borrow spare shared-cache space and yield it to preloads. The shared pool remains 192 MiB and the idle GPU cache limit remains 32 MiB.
- Added the right-side cache overlay and launcher switch, off by default, showing READY, IDLE, CPU/GPU usage, and hit statistics.
- Protect preloaded backgrounds to reduce repeated grayscale-effect construction and decoding.
- Expanded built-in shaders and platform-table compatibility, added shader conversion/compilation controls, and fixed ancestor alpha in intermediate composites and text-completion behavior.

## v1.2.8 — 2026-09-15

- Fixed compatibility issues in character pans, partial grayscale, and mask compositing.
- Fixed `mask=false` hiding save thumbnails, reuse of stale transition captures after returning from saves, and wait state after closing the opening instructions.
- Improved native script commands and font-role recognition. Avoid repeated decoding after texture upload failures and limit archive lock duration.
- OGV-specific clocks request CPU 444 MHz / ES4 222 MHz by default.

## v1.2.7 — 2026-09-13

- Added hierarchical launcher settings and separate global/OGV CPU and ES4 clock controls.
- CPU choices are off / 444 MHz; removed the unsupported 500 MHz option.

## v1.2.6 — 2026-09-13

- Improved OGV software decoding, looping, and paired color/alpha playback.
- Added GXM YUV444 color and alpha conversion, reduced video-queue copies, and coordinated decoding, uploads, and GPU completion.
- Added native ATRAC9 audio support and legacy archive filename-encoding detection.
- Fixed scaled UI tables, language aliases, and dialogue display compatibility.

## v1.2.5 — 2026-09-11

- Added an in-game host-menu action to exit the current game and return to game selection.
- Scoped PS-button protection to loading to prevent accidental initialization interruption.

## v1.2.4 — 2026-09-11

- Recorded hardware acceptance of per-game font-size overrides. This tag serves as an acceptance version for existing font settings rather than introducing another rendering feature.

## v1.2.3 — 2026-09-11

- Added a CapUnlocker switch for CPU core 4, allowing background image loading and software video tasks to use that core.
- Added per-game font overrides with name and dialogue size settings.
- Standardized menu-item font sizes.

## v1.2.2 — 2026-09-11

- The host game menu uses a small, pre-baked atlas loaded on demand and released on close, reducing menu-opening cost.
- Retained native game-menu priority and adjusted startup pixel-test regions to reduce interference from performance overlays.

## v1.2.1 — 2026-09-11

- Increased the heap request to 320 MiB and the shared image cache to 192 MiB.
- Added separate chapter-mask and animation preload channels, limited to 16 MiB and 32 MiB respectively.
- Added missing inline transition-mask preloads in background, foreground, and CG commands while retaining exact resource paths.
- Improved PNG metadata preparation and cache-category statistics.

## Early versions

The original two-part tag names below are preserved; `v1.10` and `v1.20` are not rewritten as three-part versions.

### v1.20 — 2026-09-11

- Retained the tested 96 MiB shared-cache baseline, with a 32 MiB idle cache and a 192 MiB heap request.
- Improved preload priority, source/decoded data retention, PNG metadata queries, and opacity preparation. Reverted the bidirectional cache-window experiment due to insufficient benefit.
- Retained the original build binary; its packaged `APP_VER` was still `01.10`.

### v1.10 — 2026-09-09

- Established the native GXM host test baseline with per-game Vita resources, semantic input, and the host game menu.
- Optimized text layout, message snapshots, and draw-list reuse to reduce repeated scene-update allocations.
- Improved opaque drawing, visible-region clipping, background audio preparation, and GPU wait timing.

## Beta tag mapping

| Beta tag | Official version tag | Date |
| --- | --- | --- |
| `beta1.3.0` | `v1.2.20` | 2026-09-27 |
| `beta1.2.0` | `v1.2.16` | 2026-09-22 |
| `beta1.1.0` | `v1.2.15` | 2026-09-22 |
| `beta1.0.0` | `v1.2.13` | 2026-09-20 |

Related `*-core` tags, `dev` tags, and rollback snapshots are development references, not additional application releases.
