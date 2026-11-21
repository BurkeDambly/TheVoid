# Coldzone

## Windows playtest

**[Download the Windows playtest](https://github.com/BurkeDambly/Coldzone/releases/latest/download/Coldzone-Windows-x64.zip)**

Extract the whole ZIP and double-click `ColdLoot.exe`. Unreal Engine is unnecessary.

Press **F2** for display mode and graphics quality.

### Multiplayer and solo tuning playtest

**[Download the Windows tuning playtest](https://github.com/BurkeDambly/Coldzone/releases/download/playtest-2026-11-20-multiplayer-tuning/ColdLoot-Windows-Multiplayer.zip)**

Everyone extracts the same ZIP. For multiplayer, the host runs `Host Cold Loot.cmd`, and guests run `Join Cold Loot.cmd` and enter the host's IPv4 address. For solo testing, open `ColdLoot.exe`. See `PLAYTEST.txt` in the ZIP for remote setup.

## Unreal Engine project

Open `ColdLoot.uproject` in Unreal Engine 5.7. **IcebergNetwork** opens by default. Press **Play**.

Press **F3** in game to search and tune movement, sliding, flight, swimming, gunplay, and the camera. Changes apply live. In multiplayer the host edits the settings, and guests see the active values. **Save preset** keeps those settings for future solo and hosted games on the same PC.

See [the workspace guide](AGENTS.md) for the asset and code map.

You start on **Base Island**. Head straight from spawn toward **Expedition Island** and keep moving
into the channel. In water, point the penguin with the mouse and hold **W** to kick forward.
Release **W** to sink. Build speed underwater and swim up to breach the surface.
Look down to dive and up to surface. Releasing **W** lets the penguin coast and slow down.
Fast entries carry momentum into the water and hold the entry pose for 0.4 seconds before blending into swimming.
A fast, shallow approach can skip up to twice. Press **C** in midair to dive straight into the water.
Both shores have a temporary practice ramp slightly to the side of the direct swim route. Line up with an
amber ramp, sprint toward it, and press **C** far enough back for the boost's 0.2-second ground lock to finish.
Press **Space** near the lip to jump through the ring. Turn off **Show Practice Ramp** on either island actor
to remove its ramp.
**M** opens the live island map.
**R** returns to Base Island. The future bunker site is the flat clearing left of spawn as you face the channel.

The **BaseIsland** and **ExpeditionIsland** sublevels under **Content → Maps → IcebergIslands** keep
each iceberg editable on its own. Open the **Levels** panel from the network map to make one current.
The updated `ExpeditionIsland-Detailed.blend` and matching vertex-colored PLY export live under
`SourceAssets/World/Icebergs/ExpeditionIsland`. The mesh uses meters and its origin matches the
Expedition Island terrain actor. The PLY mirrors Unreal Y into Blender Y. Keep the origin when
making additions, then place the imported assets relative to Expedition Island in Unreal.

## Previous terrain and movement prototypes

`Archives/LegacyMaps.zip` contains the Slide Park and Frozen Basin maps. To reopen either map,
extract its `.umap` file into `Content/Maps` first.

To roam Coldzone, open **Content → Maps → FrozenBasin** and press **Play**. You start at the 610 m
**Summit Crown**. Press **M** for the island map with your live location, all twelve zones, and seven
graded routes. The coastal loop and high traverse connect to the summit switchbacks, glacier link,
quarry link, gorge link, and the path past **Frost Basin**, a small U-shaped depression around a flat
ice sheet on the coastal slope. **R** returns to the summit.

In Editor builds, **Up/Down** selects a location on the map and **Enter** visits it.

### Zone references for Blender

In the Outliner, open **Coldzone → Zone Reference Cameras** and pilot a numbered camera to frame its
zone. During an Editor playtest, **F10** captures the active view without the HUD into
`Saved/Screenshots/WindowsEditor`. The matching camera and zone coordinates are in
`SourceAssets/World/ColdzoneZoneViews.csv` in Unreal centimeters. Use the zone center as a reference
when fitting a Blender asset, then place the imported mesh in Unreal. The cameras are editor-only.

| Control | Action |
| --- | --- |
| WASD | Move or steer in midair relative to the view. On land, A/D strafe and S backpedals. |
| Mouse | Look around and steer your aim |
| Shift | Sprint while moving |
| C | Boost into a belly slide on land or pitch straight down in midair |
| A / D while sliding | Keep turning left or right |
| S while sliding | Brake at any camera angle |
| Space | Jump from the ground or during a slide |
| Hold left mouse | Fire the starter weapon automatically |
| Hold right mouse | Aim over the shoulder and interrupt sprint |
| 1 or R | Reset to the flat pad |
| 2 | Reset on the downhill slope |
| 3 | Reset facing uphill |

The starter weapon fires visible projectiles along the barrel at **15 rounds per second** with a suppressed shot sound. Sprint hip-fire has a wider spread and stronger camera shake. Firing or aiming exits a slide. Aiming while holding Shift returns movement to walk speed. While aiming, the penguin keeps facing the reticle as you strafe.

**Content → Weapons → Starter → DA_StarterWeapon** holds the fitted weapon meshes, grip pose, shot sound, rounds per second, projectile speed, and damage.

## Tune movement

Stop Play, select **Penguin** in the World Outliner, and open **Details → Penguin**.

- **Locomotion → Walk Distance per Cycle / Sprint Distance per Cycle** control full-speed travel per complete animation cycle. Defaults are **182 cm** for the shuffle and **450 cm** for the sprint.
- **Animation → Upper Body Jiggle** scales the torso and head follow-through during movement. **0** turns off the added motion. Holding or releasing Shift blends walk and sprint over roughly **0.3 seconds** and carries the current step through the transition.
- **Slide → Stationary Slide Push** sets the speed when you start a slide from rest. A moving entry gets a boost that settles into a glide on flat ground. Downhill grades can build speed again.
- **Slide → Flat Deceleration, Fast Slide Deceleration, Slope Gravity, Slide Speed Limit, and Slide Steering** control grip, grade response, top speed, and turning. The HUD shows current speed and grade, plus distance and peak speed for the current or last slide.

Select **Slide Park Terrain** to change **Hill Seed** and **Hill Height Scale**. Head straight from the flat pad to reach the descent, flat runout, and uphill bank. Explore to either side for the generated hills.

The camera follows just behind and to the right of the penguin. Select the **CameraArm** component to adjust **Target Arm Length** or **Socket Offset** for a wider or tighter view.

## Project files

- **Unarmed master penguin** [Cold Loot - Penguin.blend](<SourceAssets/Penguin/Blender/Cold Loot - Penguin.blend>). Use this rig and base geometry for character work.
- **Approved grip** [Starter Grip - Approved.blend](<SourceAssets/Weapons/Blender/Starter Grip - Approved.blend>). Frame 1 shows the outside hold. Frame 21 shows the optional tip tuck.
- **Starter weapon in Unreal** [Cold Loot - Penguin Weapon Fit.blend](<SourceAssets/Weapons/Blender/Cold Loot - Penguin Weapon Fit.blend>) and the assets under `Content/Weapons/Starter`.
- **Archived source art and pose studies** [InactiveSourceArt.zip](Archives/InactiveSourceArt.zip). Archive entries retain their original project-relative paths.
