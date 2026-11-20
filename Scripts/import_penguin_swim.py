"""Import the authored swim clip onto the existing Unreal penguin skeleton."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_slide_park import PENGUIN, actors, import_fbx, levels, unreal

mesh = unreal.load_asset(f"{PENGUIN}/Mesh/SK_Penguin")
skeleton = mesh.get_editor_property("skeleton")
unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
clip = import_fbx("A_Swim_Paddle", f"{PENGUIN}/Animations", skeleton)
if not 0.77 <= clip.get_play_length() <= 0.83:
    raise RuntimeError(f"Expected a 0.8-second swim loop, got {clip.get_play_length():.3f}")
if not levels.load_level("/Game/Maps/IcebergNetwork"):
    raise RuntimeError("Could not load the Iceberg Network playtest")
pawn_class = unreal.load_class(None, "/Script/ColdLoot.PenguinPawn")
pawn = next(actor for actor in actors.get_all_level_actors() if actor.get_class() == pawn_class)
pawn.set_editor_property("swim_animation", clip)
if not levels.save_current_level():
    raise RuntimeError("Could not save the swim clip assignment")
unreal.log(f"Imported swim paddle animation ({clip.get_play_length():.3f}s)")
