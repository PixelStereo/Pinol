# Controls and the phone remote

## Controls

The **Controls** tab (Shift+5), next to the Media Bin and the Interactions, holds the operator's own values: a **Slider**
(range, step, unit, default), a **Toggle**, a **Selector** (a list of choices), a **Button** (1 while it is held down) and a
**Color**. **+ New** makes one; **⋯** edits, duplicates, moves or deletes it, each one undo step. They are saved with the
project (not in snapshots: a control is the operator's, not the scene's).

A control is a value of the show, `control/<name>` (a color: `control/<name>/r`, `g`, `b`), like a layer's opacity. So:

* an **interaction follows it** (Source *Internal*, *Controls › name*) to drive anything: one control read by several
  interactions drives several things at once, each with its own filters (a Remap to invert, an Envelope, a Curve);
* an **interaction drives it** (a key, a MIDI fader, an audio band, a hit as the source; the control as the target), so
  several gestures meet in one value;
* a **timeline animates it** (sliders and colors), an event track sets a switch;
* **OSC** reads and sets it: `/control/<name>` (a slider: float in its range; a toggle or a button: 0 / 1; a selector: 1 is the
  first choice; a color: three floats 0..1), and OSCQuery lists it;
* the **phone remote** moves it.

The address is made from the name when the control is created and does not follow a rename (an interaction or a remote knows
it by that address); duplicates get `_2`.

A **Button** is a gate, not a trigger: its value is 1 while it is held. Feed it to an *Envelope* filter for a flash that fires
on the press and releases on the let-go.

## Phone remote

**Settings › OSC › Phone remote**: tick *Serve the Controls as a web page*, open the address shown on the phone (the same
network), enter the **PIN**. The page shows each control as something a thumb can use (sliders, big switches, selector
buttons, hold buttons, color pickers), and follows the show live: what moves in NyxMap moves on the phone, and back.

Safety, because it is a door into a show:

* it reaches **the controls only**, not the layers, the project or OSC;
* a **PIN** of 6 digits (made at first use, **New PIN** to change it, 5 to 12 digits), compared in constant time; eight wrong
  PINs lock an address out for a minute;
* the WebSocket is accepted only from the page this server served (same origin), so a page of another site cannot reach it;
* the page's script and style are the only code its Content-Security-Policy allows (hashes of exactly what is served);
* at most 16 phones, frames of at most 4 KB, 300 messages a second each;
* **no encryption**: the PIN and the values cross the network in clear, as OSC does. Use it on the show's network, not on a
  shared one. It listens where OSC does (*Listen on*).

It is off by default. To write your own client, the protocol is a WebSocket at `/ws?pin=<PIN>` with JSON text frames
(described in `src/engine/RemoteServer.h`).

Why not Open Stage Control inside NyxMap? It is a GPL-3.0 application on Electron: it cannot be part of an MIT program. The
existing Open Stage Control desk (`integrations/open-stage-control`) keeps working against the OSC namespace, controls
included (`/control/…`); this page is the integrated one.
