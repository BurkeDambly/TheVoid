# Coldzone

Open `ColdLoot.uproject` in Unreal Engine 5.7. **IcebergNetwork** opens by default. Press **Play**.

See [the workspace guide](AGENTS.md) for the asset and code map.

You start on **Base Island**. Head straight from spawn toward **Expedition Island** and keep moving
into the channel. In water, point the penguin with the mouse and hold **W** to kick forward.
Release **W** to sink. Build speed underwater and swim up to breach the surface.
Look down to dive and up to surface. Releasing **W** lets the penguin coast and slow down.
Fast entries carry momentum into the water. **M** opens the live island map.
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
ice sheet on the coastal slope. **R** returns to the summit, and **Q** toggles blue Form.

In Editor builds, **Up/Down** selects a location on the map and **Enter** visits it. Press **E** for red
Editor Penguin Form, **C** to slide, and hold **W** to power the slide at sixteen times sprint speed.

### Zone references for Blender

In the Outliner, open **Coldzone → Zone Reference Cameras** and pilot a numbered camera to frame its
zone. During an Editor playtest, **F10** captures the active view without the HUD into
`Saved/Screenshots/WindowsEditor`. The matching camera and zone coordinates are in
`SourceAssets/World/ColdzoneZoneViews.csv` in Unreal centimeters. Use the zone center as a reference
when fitting a Blender asset, then place the imported mesh in Unreal. The cameras are editor-only.

| Control | Action |
| --- | --- |
| WASD | Move relative to the view. A/D strafe and S backpedals. |
| Mouse | Look around and steer your aim |
| Shift | Sprint while moving |
| C | Start or cancel a belly slide. Build speed with Shift and WASD for a boosted entry. |
| Hold left mouse | Fire the starter weapon automatically |
| Hold right mouse | Aim over the shoulder and interrupt sprint |
| 1 or R | Reset to the flat pad |
| 2 | Reset on the downhill slope |
| 3 | Reset facing uphill |

The starter weapon fires visible projectiles along the barrel at **15 rounds per second** with a suppressed shot sound. Sprint hip-fire has a wider spread and stronger camera shake. Firing is blocked during a belly slide and its recovery. Aiming while holding Shift returns movement to walk speed. While aiming, the penguin keeps facing the reticle as you strafe.

**Content → Weapons → Starter → DA_StarterWeapon** holds the fitted weapon meshes, grip pose, shot sound, rounds per second, projectile speed, and damage.

## Tune movement

Stop Play, select **Penguin** in the World Outliner, and open **Details → Penguin**.

- **Locomotion → Walk Distance per Cycle / Sprint Distance per Cycle** control full-speed travel per complete animation cycle. Defaults are **91 cm** for the shuffle and **450 cm** for the sprint.
- **Animation → Upper Body Jiggle** scales the torso and head follow-through during movement. **0** turns off the added motion. Holding or releasing Shift blends walk and sprint over roughly **0.3 seconds** and carries the current step through the transition.
- **Slide → Flat Slide Distance** sets the baseline at **420 cm**. **Sprint Slide Distance Multiplier** sets the minimum sprint launch from the flat-distance setting. **Sprint Slide Speed Boost** adds up to **130 cm/s** to your entry speed, subject to the slide speed limit. A full-speed entry travels about **9.3 m** on flat snow.
- **Slide → Flat Deceleration, Slope Gravity, Slide Speed Limit, and Slide Steering** control grip, grade response, top speed, and turning. Distance settings work up to the configured speed limit. The HUD shows current speed and grade, plus distance and peak speed for the current or last slide.

Select **Slide Park Terrain** to change **Hill Seed** and **Hill Height Scale**. Head straight from the flat pad to reach the descent, flat runout, and uphill bank. Explore to either side for the generated hills.

The camera follows just behind and to the right of the penguin. Select the **CameraArm** component to adjust **Target Arm Length** or **Socket Offset** for a wider or tighter view.

## Project files

- **Unarmed master penguin** [Cold Loot - Penguin.blend](<SourceAssets/Penguin/Blender/Cold Loot - Penguin.blend>). Use this rig and base geometry for character work.
- **Approved grip** [Starter Grip - Approved.blend](<SourceAssets/Weapons/Blender/Starter Grip - Approved.blend>). Frame 1 shows the outside hold. Frame 21 shows the optional tip tuck.
- **Starter weapon in Unreal** [Cold Loot - Penguin Weapon Fit.blend](<SourceAssets/Weapons/Blender/Cold Loot - Penguin Weapon Fit.blend>) and the assets under `Content/Weapons/Starter`.
- **Archived source art and pose studies** [InactiveSourceArt.zip](Archives/InactiveSourceArt.zip). Archive entries retain their original project-relative paths.
