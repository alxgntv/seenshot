# PRD-02: Annotate tools (Stage 2)

Product requirements for the SeenShot annotate editor. Stage 1 architecture (cloud, quota, auth, capture, hotkey) is unchanged. This document is requirements only. Implementation is a later task and must follow this document only.

Stage 1 TZ: [SeenShot — полный план (этап 1)](/Users/alexign/.cursor/plans/seenshot_full_architecture_c5152778.plan.md)

Stage 1 described the editor in one line: highlight, arrow, text, blur, color slider of the last item, `QUndoStack`. That is not enough for the product. Stage 2 replaces that one-liner with the rules below.

UI strings: English. Logs: English.

---

## Now / must become

As-is of Stage 1 in the current client:

| Area | Now | Must become |
| --- | --- | --- |
| Text | Click opens `QInputDialog` titled `Label`. No caret on the image. No wrap-by-width after commit. | Click places a caret on the frame. Type in place. After commit, drag block width so text wraps. |
| Highlight | One style only: stroke + fill alpha 80 (`applyItemColor` on `QGraphicsRectItem`). | One Square tool. Stroke / Fill sliders set border and fill. Separate Text and Steps tools. |
| Step | Missing. | Separate tool. Numbered markers `1…n` on this screenshot only. |
| Color slider | Gray `QSlider` 0–359 hue, label `Color`. Track is not a spectrum. Thumb does not show the current color. `onHueChanged` pushes a `ChangeColorCommand` on every tick. If last item is blur (`QGraphicsPixmapItem` type 7), `applyItemColor` logs `unsupported item type` and nothing visible changes. | Real hue-spectrum slider. Immediately recolors the last committed Highlight / Arrow / Text / Step / Fill+text. One gesture = one undo. Blur is not recolored. |

---

## 1. Text — type on the image

- Click on the frame places a text caret at that point and typing starts immediately. No `Label` dialog. No modal.
- While the caret is in the text block, input is normal typing. Enter inserts a newline.
- Leaving edit (click outside the block / Escape / choose another tool) commits the block.
- After commit, the user can drag the block width. Text wraps to that width (line length). Height is computed from content.
- Empty text after leave is discarded. No empty text item stays on the scene.
- Color of the text block in edit, or the last committed text if none is in edit, is set by the color slider (section 4).
- Undo: add, edit text, and change width are separate stack steps. Do not add a parallel second text tool.

---

## 2. Square — one tool, sliders for stroke and fill

One Square button. Not Border / Fill / Fill + text as three tools.

- **Stroke** slider — border width of the last Square or Steps rectangle (and its number badge).
- **Fill** slider — fill alpha of that same rectangle and badge. `0` = stroke only.
- Color slider (section 4) sets the hue. Fill stays translucent when alpha is below 255.
- Caption text is the separate Text tool. Do not add a second text tool on the square.

Steps stays a separate tool (section 3). Same Stroke / Fill sliders apply to a Steps rectangle and its number badge.

---

## 3. Step — numbered callouts

A separate tool. Not a fourth Highlight style. Used for step-by-step guides.

- First marker placed on this frame = `1`. Next = `2`. No cap.
- The number is visible on the marker (square / badge).
- Numbering exists only inside the current editor (one screenshot).
- Undo / delete of a step renumbers the remaining markers to `1…n` in creation order. Gaps such as 1, 2, 4 are forbidden.
- Marker style: stroke + fill of the current slider color. Digit is contrast and readable.

---

## 4. Color slider — a real color slider

Stage 1 already required “color slider of the last item”. Product-wise that requirement failed: the control is a gray left–right bar and often appears to do nothing.

Must be:

- The slider track is a hue spectrum (rainbow), not a gray bar. The thumb shows the current color.
- Drag immediately recolors the **last added** item on this frame (Highlight / Arrow / Text / Step / Fill+text).
- No items: the slider still moves and sets the color for the **next** stroke. Nothing on the scene is recolored.
- Blur is not recolored. The slider must not pretend blur changed color.
- One slider gesture = one undo step (not a command per movement tick).
- A new stroke uses the current slider color.

---

## 5. Out of scope for this PRD

- Arrow, except last-item color.
- Blur, except the rule “do not recolor”.
- Save / Share / Cloud.
- Hotkey.
- Capture.
- Backend, quota, auth.

Do not add extra tools that duplicate Highlight.

---

## Corner cases

- Text: click on an existing text block re-enters edit. It does not create a second block.
- Text: minimum block width so the block cannot collapse to zero.
- Fill+text: empty caption after leave — the rectangle stays, the empty caption is discarded. The rectangle is not deleted.
- Step: two markers at the same point are allowed. Numbers are different.
- Square vs Steps: the tool switch applies to the **new** stroke. Stroke / Fill sliders recolor the last Square or Steps item (width and alpha), not the tool.
- Color slider: last-item = the text block in edit if any, else the last successfully committed object. A discarded empty draft must not leave a color undo.
- Color slider: after Undo, if that last item is gone, the slider recolors the new last item, or only sets the color for the next stroke if none remain.
- All UI labels: English. All logs: English.
