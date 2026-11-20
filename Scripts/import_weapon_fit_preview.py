"""Install an authored hold on the Iceberg Network player for playtesting."""
import json
from pathlib import Path
import sys

import unreal

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from build_slide_park import actors, import_fbx, levels
from import_starter_weapon import DEST, SOURCE, transform


MAP = "/Game/Maps/IcebergNetwork"
PREVIEW = f"{DEST}/FitPreview"
BASE_DEFINITION = f"{DEST}/DA_StarterWeapon"
PREVIEW_DEFINITION = f"{PREVIEW}/DA_StarterWeapon_FitPreview"
LIBRARY = unreal.EditorAssetLibrary


def main():
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    fit = json.loads((SOURCE / "FitPreview/ReceiverFit.json").read_text(encoding="utf-8"))
    skeleton = unreal.load_asset("/Game/Penguin/Mesh/SK_Penguin_Skeleton")
    hold = import_fbx("A_StarterWeaponHold_Fitted", PREVIEW, skeleton, SOURCE / "FitPreview", replace_existing=True)
    root, flipper = unreal.AnimationLibrary.get_bone_poses_for_frame(hold, ["Root", "Flipper_R"], 0, False)
    math = unreal.MathLibrary
    flipper_on_penguin = math.compose_transforms(flipper, root)

    definition = (unreal.load_asset(PREVIEW_DEFINITION) if LIBRARY.does_asset_exist(PREVIEW_DEFINITION)
                  else LIBRARY.duplicate_asset(BASE_DEFINITION, PREVIEW_DEFINITION))
    if not definition:
        raise RuntimeError("Cannot create the preview weapon definition")
    # Keep the original receiver, stock and socket. Solve the grip relative
    # to the fitted wing from the exact receiver position in the Blender file.
    receiver_on_grip = definition.get_editor_property("receiver_on_grip")
    receiver_on_penguin = transform(fit["receiver_on_penguin"])
    grip_on_penguin = math.compose_transforms(math.invert_transform(receiver_on_grip), receiver_on_penguin)
    grip = math.make_relative_transform(grip_on_penguin, flipper_on_penguin)
    hinge = math.inverse_transform_location(flipper_on_penguin, unreal.Vector(*fit["hinge_on_penguin"]))
    definition.set_editor_property("hold_pose", hold)
    definition.set_editor_property("grip_on_flipper", grip)
    definition.set_editor_property("hold_hinge_on_flipper", hinge)
    definition.set_editor_property("use_fitted_flipper_transforms", True)
    if not LIBRARY.save_loaded_asset(definition, only_if_is_dirty=False):
        raise RuntimeError("Cannot save the fitted hold")

    if not levels.load_level(MAP):
        raise RuntimeError(f"Cannot load {MAP}")
    pawn = next(actor for actor in actors.get_all_level_actors() if isinstance(actor, unreal.PenguinPawn))
    pawn.set_editor_property("weapon_definition", definition)
    if not levels.save_current_level():
        raise RuntimeError(f"Cannot save {MAP}")
    unreal.log(f"Installed the fitted hold in IcebergNetwork with flipper-local hinge {hinge}")


if __name__ == "__main__":
    main()
