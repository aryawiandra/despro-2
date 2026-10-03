# SAFE-EXIT — brag plan

**What it is:** A floor-level LED evacuation path that re-routes itself around fire, running Dijkstra on an ESP32.
**For:** People trapped in a burning building, crawling under smoke where ceiling exit signs can't be seen.
**Sets it apart:** Guidance lives on the floor, below the smoke. Routes change live when a smoke or flame sensor trips. It runs locally with no network or central alarm panel.
**Most impressive claim:** A tripped sensor sets that corridor's weight to ∞, and the LEDs redraw the route to a different exit.
**Visual hook:** A glowing EXIT sign swallowed by smoke, then a green line lights up along the floor.
**Real material:** firmware `kodeIntegrasi.cpp` (15 nodes / 20 edges, room 4 default), the team's graph visualizer layout, the breadboard photo with the WS2812B strip lit, and photos of the team building the 1:50 Gedung K model.
**Tone:** `cinematic`, restrained: dark, smoky, with green as the only safe color and red for danger.
**Share caption:** Exit signs hang where the smoke goes first, so we put ours on the floor, and made it re-route around the fire.

## Angle
"The exit sign is in the wrong place." Open on the problem everyone has seen but never questioned, then move the sign to the floor and make it think.

## Storyboard (landscape 1920×1080, 30fps, 22.5s)

| # | Time | Scene | On-screen text |
|---|---|---|---|
| 1 | 0.0–3.2 | **Hook.** Dark ceiling with a green EXIT sign glowing. Smoke rolls down from the top and swallows it. | "Exit signs hang where smoke goes first." |
| 2 | 3.2–6.0 | **Stakes.** A big "90%" counts up in smoke-grey. | "90% of enclosed-fire deaths come from smoke inhalation." · small: U.S. Fire Administration |
| 3 | 6.0–9.0 | **Reveal.** Camera drops to the floor. A green LED line chases across the bottom of frame, and the SAFE-EXIT wordmark lands. | "SAFE-EXIT" / "Evacuation guidance on the floor, below the smoke." |
| 4 | 9.0–15.2 | **Highlight 1: it thinks.** The team's real graph (visualizer layout). Room 4 is marked "You are here". Green chase along j8→j7→j6→e2. A flame icon ignites on j6–j7, the edge turns red and reads "weight = ∞", and the route redraws j8→j7→j9→j10→j12→e3. | "Dijkstra finds the shortest way out." → "Fire detected. Route recomputed." · code chip: `e.weight = sensorTriggered(e) ? INFINITY : e.baseDistance;` |
| 5 | 15.2–18.6 | **Highlight 2: real hardware.** Breadboard photo (LED strip glowing red) with a slow push-in. Three spec lines land one by one. | "ESP32, runs offline" · "20 smoke & flame sensors" · "20 LED path segments" |
| 6 | 18.6–20.6 | **Highlight 3: being built.** Team photo around the foam-board model. | "Now building it at 1:50, Gedung K FTUI." |
| 7 | 20.6–22.5 | **Outro.** Wordmark with a green floor line. | "SAFE-EXIT · Adaptive fire-evacuation path lighting" / "Group 21 · Electrical Engineering, Universitas Indonesia" |

## Sound
Original score synthesized in D minor: low drone plus filtered-noise "smoke" bed. A soft kick pulse enters at the reveal. Sensor/LED blips are pitched to the D-minor pentatonic scale and sit under the music. A low boom lands on the reveal and on the fire trigger, and a filtered riser leads into each. Tail fades on the outro.

## Honesty notes
- Hardware on the full model isn't tested yet (Week 5 report), so the video says "now building", and the ≤2s response target isn't claimed as achieved.
- The route shown is the actual Dijkstra output on the firmware's edge weights.
