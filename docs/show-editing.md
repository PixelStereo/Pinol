# Editing snapshots, presets, and sequences

## Include, exclude, or remove

The checkbox in a snapshot's content tree controls **inclusion on recall**. Unchecking a layer, interaction, fixture, or composition keeps its stored contents but leaves that object alone when recalled. It does not delete the stored contents or the live layer.

Select rows in the contents tree and use **Remove elements**, Delete/Backspace, or the context menu to actually remove their stored data. Shift and Ctrl/Cmd allow multiple selection. Selecting a category selects its descendants; selecting a layer removes all its stored contents. Structural source, effect-chain, mask, mesh, routing and animation-list entries represent complete definitions.

Removing contents makes the snapshot **Partial**, indicated in its inspector. Partial snapshots leave unmentioned layers and interactions alone, including objects created later. Ordinary, unedited full snapshots retain their existing full-scene behavior. Removed properties do not reset to defaults. A parameter-only fragment cannot recreate a missing source; include its source definition when recreation is required.

**Update** on a partial snapshot updates only the owners, values and definitions it still holds. Removed items do not reappear. The preset's Update follows the same existing-values rule. Stored edits leave the live composition untouched until a recall.

## Duplicate, copy, and paste

Use **Duplicate** in a snapshot/preset's menu or its duplicate button; Ctrl/Cmd+D also works in the library. Copies receive new identities, so existing sequence targets continue to point to their original objects. Snapshot copies stay beside their original in its group. These operations can be undone/redone.

In the snapshot/preset **contents** tree:

- Select one or more elements, or a whole layer/category.
- Use the Copy, Cut, Paste buttons, context menu, or Ctrl/Cmd+C/X/V.
- Select the destination snapshot or preset and paste. Existing matching elements are replaced; unrelated stored content is retained.
- Per-value delay, transition time and easing travel with the copied value. Paste into an empty snapshot produces a partial snapshot.

The same typed clipboard transfers supported values, structural definitions, animations and interactions between snapshots and presets. It uses the original stable layer/interaction/fixture identities: use it within the same project. It does not clone or retarget live layers. Source/FX/mask definitions remain whole definitions, as in the preset inspector; their existing structural recall rules apply. Layer visibility explicitly copied into a preset is a cut, with an optional delay.

Library Delete removes the entire snapshot/preset. Contents-tree Delete removes only selected stored elements. Copy/paste shortcuts are scoped to their tree/table, leaving text editors' ordinary clipboard behavior available. All new content edits and duplicates support the main undo stack.

## Edit a timecoded sequence

Open **Sequences** and enable **Edit mode · mute TC / auto-follow** for the sequence you want to edit.

- Its pending pre-waits and follows are stopped.
- Incoming timecode no longer triggers its cues; the camera/audio/timecode source itself keeps running.
- Manual GO or playing a step auditions one cue immediately, without its pre-wait, tracking, or automatic continuation.
- Disabling Edit mode resumes at the clock's current position. Cues crossed while muted are not replayed; future crossings trigger normally.

The mode is per sequence and saved in the project. Other running sequences remain independent. It is an editing control, not a global pause or a mute of timecode outputs.

## Steps and action groups

The steps table provides multi-selection Copy/Cut/Paste and Delete/Backspace. **Duplicate** or Ctrl/Cmd+D copies the selected steps after the last selected row. Duplicates keep their waits, targets, text and actions, receive fresh identities, and clear their absolute timecodes to avoid two cues firing at the same timestamp. Clipboard paste preserves the stored timecodes, so check them when pasting into the same sequence.

**+ Step** inserts after the current row; Insert and the context menu also offer insertion before/after. Existing step identities follow moves/insertions. Step definition edits support Undo/Redo.

Select consecutive steps and choose **Group actions** to make one cue containing their actions. Its first step supplies the pre-wait, timecode, comment and description; the last supplies post-wait and continuation. The constituent actions share one GO and one pre-wait and dispatch in list order during the same cue-clock turn. If they set the same property, the later action wins. Auto-follow waits for the longest action, including finite timeline playback; starting another sequence launches that independent sequence. A group supports up to 64 actions.

Right-click a cue → **Edit action group…** to add, duplicate, remove, or reorder snapshot/preset/timeline/sequence actions. Timeline actions expose play/pause/stop/rewind, seek, speed and loop settings. Cancel discards draft changes. The group is saved with the project and described in cue feedback/OSC catalogs.

**Ungroup actions** turns a group into consecutive cues with Follow between them, preserving the common pre-wait on the first and post-wait/continuation on the last. Review continuation timing after splitting a parallel group: subsequent cues now follow the last split action. Use Undo to restore the original group.
