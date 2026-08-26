# PRD-03: Photo cutout on the screenshot

Product requirements for putting a camera selfie without background onto the current screenshot. Stage 1 architecture (cloud, quota, auth, capture, hotkey) and PRD-02 annotate tools are unchanged. This document is requirements only. Implementation is a later task and must follow this document only.

Stage 1 TZ: [SeenShot — полный план (этап 1)](/Users/alexign/.cursor/plans/seenshot_full_architecture_c5152778.plan.md)

PRD-02: [Annotate tools](/Users/alexign/Desktop/seenshot/docs/PRD-02-annotate-tools.md)

The product gap: nobody else lets the author appear on the screenshot pointing at a UI detail, with the room background removed. A photo with background is forbidden. The cutout is an object **on the frame**, not a second window.

UI strings: English. Logs: English.

---

## Now / must become

| Area | Now | Must become |
| --- | --- | --- |
| Camera | Missing. No `NSCameraUsageDescription`. `MacPermissions` is Screen Recording only. | Photo button. Camera TCC once, on first Photo press. |
| Person on shot | Missing. | After 3-2-1 and a flash, a person cutout is placed on the shot. |
| Background | — | Official Vision person segmentation. No person / empty mask → no item. Photo with background is forbidden. |
| Move / scale | Annotation items are not dragged. Text has a width grip only. | Photo item can be dragged inside `shotRect` and scaled proportionally from one corner grip. |

---

## 1. Photo is a button, not a drawing tool

- Toolbar action **Photo**, next to Blur / Save / Share. Same pattern as Share: one click starts the cycle.
- Not `Tool::Photo`. Not a second way to draw Highlight / Arrow / Line / Text / Blur.
- Pressing Photo again after a successful place adds another independent Photo object.
- A second Photo press while a cycle is already running is ignored. Log only.

---

## 2. Camera permission — once

- `NSCameraUsageDescription` in the app Info.plist.
- Official AVFoundation: `authorizationStatusForMediaType:` / `requestAccessForMediaType:` for `AVMediaTypeVideo`.
- Request on the first Photo press. Not on first-run wizard. Not in parallel with Screen Recording.
- Denied / restricted / no camera: ErrorCatalog message. No capture. No item on the scene.

---

## 3. Preview, countdown, flash

- Live preview over the shot (`AVCaptureVideoPreviewLayer`) so the user can point a finger at a place on the screenshot.
- Preview is mirrored like a selfie. The saved cutout uses the same orientation as the preview.
- Countdown **3**, **2**, **1** (UI English) on the preview.
- Escape cancels. Camera session stops. No item is created.
- At zero: white flash on the annotate window, one still via `AVCapturePhotoOutput`.
- Closing the annotate window during countdown stops the session. No item is created.

---

## 4. Background must be removed

- Official Vision only: `VNGeneratePersonSegmentationRequest` (macOS 14+).
- Alpha comes from the person mask.
- No person or empty mask: error. The item is not placed. A photo with background must not appear.
- Not Qt Multimedia. Not a third-party cutout service. Not “blur the background instead of cutting”.

---

## 5. Object on the screenshot

- Kind `AnnotateKind::Photo`. Own item type, like Text. Not a second Blur pixmap.
- Start: center of `shotRect()`. Start size about 30% of the shot’s shorter side. Aspect ratio kept.
- Drag stays inside `shotRect()` (same clamp as drawing).
- One corner grip scales both axes together. Minimum size so the item cannot collapse to zero. Maximum size so the item stays fully on the shot.
- Color / Stroke / Fill / Amount / Background do not recolor Photo.
- Undo: add, move, and scale are separate stack steps. One drag or scale gesture = one undo (same as sliders).
- Click on a Photo item does not start Highlight / Arrow / Line / Text / Blur.
- Save / Share / export render the Photo item with the scene.
- Gradient canvas: Photo lives in `shotRect()` coordinates, not on the padding.

---

## 6. Out of scope for this PRD

- Camera picker.
- Photo library / gallery.
- Video.
- Cutout of a non-person subject.
- Cloud / remote segmentation.
- Stage 1 (cloud, quota, auth, capture, hotkey).
- PRD-02 tools, except Photo must not steal their clicks.

Do not add a second Photo tool or a second cutout pipeline.

---

## Corner cases

- No camera / no person / TCC denied: error. Nothing is added to the scene.
- Several Photo items on one frame are allowed. Each is independent.
- Window closed during countdown: session stops. No item.
- Cloud encoder may still shrink a large PNG.
- After Undo of a Photo add, that item is gone. Other annotations are unchanged.
- All UI labels: English. All logs: English.
