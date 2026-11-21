# Cold Loot workspace guide

## Start here

- Open `ColdLoot.uproject` in Unreal Engine 5.7. The only playable map is `/Game/Maps/IcebergNetwork`. Base Island is the player spawn. Base Island and Expedition Island are streamed from `Content/Maps/IcebergIslands`.
- C++ gameplay and automation tests live in `Source/ColdLoot` and `Source/ColdLoot/Tests`. The main gameplay test group is `ColdLoot.IcebergNetwork`.
- Edit Blender source in `SourceAssets`, export interchange files to its `FBX` folders, and use Unreal Editor to import them into `Content`. `Saved`, `Intermediate`, and `Binaries` contain local work and generated output.

## Find the right asset

| Task | Source of truth and runtime handoff |
| --- | --- |
| Unarmed penguin anatomy and shared rig | **Master** [Cold Loot - Penguin.blend](<SourceAssets/Penguin/Blender/Cold Loot - Penguin.blend>). It contains the approved base geometry and the shared walk, sprint, and slide actions. Keep this path stable because the faction Blender scenes link to it. Export with `Scripts/export_penguin.py` and `Scripts/export_penguin_swim.py`. Runtime mesh and clips are in `Content/Penguin/Mesh` and `Content/Penguin/Animations`. |
| Approved weapon hold design | [Starter Grip - Approved.blend](<SourceAssets/Weapons/Blender/Starter Grip - Approved.blend>). Frame 1 is the outside hold. Frame 21 is the optional tip tuck. Keep grip poses and weapon geometry in this separate scene when iterating. |
| Standalone weapon model | [Airsoft Prop.blend](<SourceAssets/Weapons/Blender/Airsoft Prop.blend>). See [the asset standards](WEAPON_ASSET_STANDARDS.md) for the visual conventions and socket names. |
| Starter weapon currently equipped in Unreal | [Cold Loot - Penguin Weapon Fit.blend](<SourceAssets/Weapons/Blender/Cold Loot - Penguin Weapon Fit.blend>) is the source for the current Unreal fit. Use `Scripts/export_starter_weapon.py` and `Scripts/import_starter_weapon.py` for the original setup. Use `Scripts/export_weapon_fit_preview.py` and `Scripts/import_weapon_fit_preview.py` for the fitted hold equipped in Iceberg Network. Runtime assets are in `Content/Weapons/Starter`. |
| Faction NPCs | The active Blender sources for Whiteout, Forgekeeper, Stormstitch, Sunmarshal, and Nightglass are listed in `Scripts/export_pinnacle_penguins.py`. Import with `Scripts/import_pinnacle_penguins.py`. Runtime meshes are in `Content/Penguin/Pinnacle` and all five actors are placed in Base Island. |
| Islands and water | Edit the Unreal level and terrain in `Content/Maps` and `Source/ColdLoot/Iceberg*.{h,cpp}`. World authoring scripts are `Scripts/build_iceberg_network.py` and `Scripts/build_iceberg_water.py`. The current Expedition Island Blender handoff is the `ExpeditionIsland-Detailed` pair in `SourceAssets/World/Icebergs/ExpeditionIsland`. |
| Island lighting | `Scripts/light_iceberg_network.py` configures the sun, skylight, cloud layer, and volumetric fog in the persistent Iceberg Network level. |

## Keep the workspace easy to scan

- Start pose experiments from the master proportions. Save fitting edits and comparison poses in the weapon scene.
- Keep active assets and their necessary references in `SourceAssets`. Put review renders in `Saved` and archive retired source models and concepts when the work is settled.
- `Archives/InactiveSourceArt.zip` holds retired source art, development-only models, and past fit studies. Extract at the project root to restore the original relative paths, and check for existing files before overwriting.
- `Archives/LegacyMaps.zip` holds Slide Park and Frozen Basin. Restore their `.umap` files to `Content/Maps` before running their legacy playtests.
