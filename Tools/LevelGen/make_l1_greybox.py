"""
Level 1 "The Breach" greybox generator (T-0020). Spec: PRODUCT_BIBLE.md section 9, P-22.

Builds /Game/ETM/Maps/L1_Breach from scratch out of engine basic shapes, so the layout is reviewable as text
and can be regenerated after a change. Hand edits made to the map in the editor are lost on regeneration:
once the layout settles, stop regenerating and edit the map directly.

Run (headless, from the game repo root):
  UnrealEditor-Cmd.exe ETM.uproject -run=pythonscript -script="Tools/LevelGen/make_l1_greybox.py" -unattended -nullrhi

Frame: centimetres, the rig sits at the origin on a 1 m mount facing +X (down the centre lane).
Lanes run at yaw -35 (left), 0 (centre) and +35 (right), inside the cannons' +/-65 degree envelope.
  Left:   broken maintenance duct, ground attackers, low pipe cover.
  Centre: main blast door (boss entrance), the transfer equipment in front of and below the rig (P-22).
  Right:  elevated conveyor and drone shaft, evacuation progress lights on the wall.
Nothing the player must see or shoot is behind them.
"""
import math
import unreal

MAP_PATH = "/Game/ETM/Maps/L1_Breach"
CUBE = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
CYLINDER = unreal.load_asset("/Engine/BasicShapes/Cylinder.Cylinder")

RIG_BASE = 100.0          # rig mount height
FAR_WALL_X = 2600.0
SIDE_WALL_Y = 1700.0
WALL_HEIGHT = 1200.0
LANE_YAW = {"Left": -35.0, "Centre": 0.0, "Right": 35.0}

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def along(yaw, distance, lateral=0.0, z=0.0):
    """A point `distance` cm down the lane at `yaw`, `lateral` cm to its right."""
    r = math.radians(yaw)
    return unreal.Vector(distance * math.cos(r) - lateral * math.sin(r),
                         distance * math.sin(r) + lateral * math.cos(r), z)


def block(label, folder, centre, size, yaw=0.0, roll=0.0, mesh=None):
    """A static mesh box (or cylinder) `size` cm big, centred at `centre`."""
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, centre, unreal.Rotator(roll, 0.0, yaw))
    comp = actor.static_mesh_component
    comp.set_static_mesh(mesh or CUBE)
    comp.set_cast_shadow(False)  # no real-time shadows in the Quest baseline (bible section 14)
    actor.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    actor.set_actor_label(label)
    actor.set_folder_path(folder)
    return actor


def build():
    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        # Rebuild in place: open the map and clear it (keeps the asset and its references).
        if not levels.load_level(MAP_PATH):
            raise RuntimeError("Could not open " + MAP_PATH)
        for actor in actors.get_all_level_actors():
            if not isinstance(actor, unreal.WorldSettings):
                actors.destroy_actor(actor)
    elif not levels.new_level(MAP_PATH):
        raise RuntimeError("Could not create " + MAP_PATH)

    # --- Shell: floor, walls, the rig's mount -------------------------------------------------------------
    depth = FAR_WALL_X + 600.0
    block("Floor", "Greybox/Shell", unreal.Vector(FAR_WALL_X - depth / 2 + 200, 0, -5), (depth + 400, SIDE_WALL_Y * 2 + 200, 10))
    for side, y in (("L", -SIDE_WALL_Y), ("R", SIDE_WALL_Y)):
        block("SideWall_" + side, "Greybox/Shell", unreal.Vector(FAR_WALL_X - depth / 2, y, WALL_HEIGHT / 2), (depth, 40, WALL_HEIGHT))
    block("RearWall", "Greybox/Shell", unreal.Vector(-600, 0, WALL_HEIGHT / 2), (40, SIDE_WALL_Y * 2, WALL_HEIGHT))
    block("RigMount", "Greybox/Rig", unreal.Vector(-30, 0, RIG_BASE / 2), (120, 160, RIG_BASE))

    # --- Centre: far wall with the main blast door (boss entrance), transfer equipment (P-22) ---------------
    door_half, door_h = 400.0, 900.0
    seg = SIDE_WALL_Y - door_half
    for side, sign in (("L", -1), ("R", 1)):
        block("FarWall_" + side, "Greybox/Centre", unreal.Vector(FAR_WALL_X, sign * (door_half + seg / 2), WALL_HEIGHT / 2), (40, seg, WALL_HEIGHT))
    block("FarWall_Lintel", "Greybox/Centre", unreal.Vector(FAR_WALL_X, 0, (door_h + WALL_HEIGHT) / 2), (40, door_half * 2, WALL_HEIGHT - door_h))
    block("BlastDoor", "Greybox/Centre", unreal.Vector(FAR_WALL_X + 120, 0, door_h / 2), (40, door_half * 2 - 20, door_h - 20))

    objective = actors.spawn_actor_from_class(unreal.MissionObjective, unreal.Vector(350, 0, 0), unreal.Rotator(0, 0, 0))
    objective.set_actor_label("TransferEquipment_Objective")
    objective.set_folder_path("Gameplay")
    block("TransferEquipment_Bulk", "Greybox/Centre", unreal.Vector(350, 0, 45), (160, 220, 90))
    for i, y in enumerate((-150.0, 150.0)):
        block("TransferConduit_%d" % i, "Greybox/Centre", unreal.Vector(1450, y, 5), (2200, 20, 10))

    # --- Left: broken maintenance duct with low pipe cover --------------------------------------------------
    yaw = LANE_YAW["Left"]
    duct_mid, duct_len, duct_w, duct_h = 2150.0, 700.0, 320.0, 260.0
    for side, lat in (("L", -duct_w / 2 - 20), ("R", duct_w / 2 + 20)):
        block("Duct_Wall" + side, "Greybox/Left", along(yaw, duct_mid, lat, duct_h / 2), (duct_len, 40, duct_h), yaw)
    block("Duct_Roof", "Greybox/Left", along(yaw, duct_mid, 0, duct_h + 20), (duct_len, duct_w + 80, 40), yaw)
    block("Duct_BrokenPanel", "Greybox/Left", along(yaw, duct_mid - duct_len / 2 - 60, duct_w / 2 + 40, 40), (160, 30, 80), yaw + 25)
    for i, d in enumerate((900.0, 1350.0)):
        block("PipeCover_%d" % i, "Greybox/Left", along(yaw, d, 0, 30), (60, 60, 420), yaw, 90.0, CYLINDER)

    # --- Right: elevated conveyor, drone shaft, evacuation progress lights -----------------------------------
    yaw = LANE_YAW["Right"]
    deck_z, deck_from, deck_to, deck_w = 400.0, 1000.0, 2300.0, 380.0  # starts 10 m out so it never hides the shaft
    deck_len = deck_to - deck_from
    block("Conveyor_Deck", "Greybox/Right", along(yaw, (deck_from + deck_to) / 2, 0, deck_z - 20), (deck_len, deck_w, 40), yaw)
    for i, d in enumerate(range(int(deck_from) + 100, int(deck_to), 450)):
        for side, lat in (("L", -deck_w / 2 + 30), ("R", deck_w / 2 - 30)):
            block("Conveyor_Leg%d%s" % (i, side), "Greybox/Right", along(yaw, d, lat, (deck_z - 40) / 2), (40, 40, deck_z - 40), yaw)
    shaft_d = 2300.0
    for side, lat in (("L", -260.0), ("R", 260.0)):
        block("DroneShaft_Pillar" + side, "Greybox/Right", along(yaw, shaft_d, lat, 550), (60, 60, 1100), yaw)
    block("DroneShaft_Beam", "Greybox/Right", along(yaw, shaft_d, 0, 1080), (60, 580, 40), yaw)
    for i in range(10):
        block("EvacLight_%02d" % i, "Greybox/Right", unreal.Vector(300 + i * 230, SIDE_WALL_Y - 40, 300), (30, 30, 30))

    # --- Gameplay: rig start, entrances, encounter director, light ------------------------------------------
    start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, RIG_BASE), unreal.Rotator(0, 0, 0))
    start.set_actor_label("RigStart")
    start.set_folder_path("Gameplay")

    # Entrance 0 left duct mouth, 1 centre (in front of the blast door), 2 right drone shaft over the conveyor.
    entrance_points = [
        ("Entrance_0_Duct", along(LANE_YAW["Left"], duct_mid - duct_len / 2 + 50, 0, 0), LANE_YAW["Left"]),
        ("Entrance_1_BlastDoor", unreal.Vector(FAR_WALL_X - 300, 0, 0), 0.0),
        ("Entrance_2_DroneShaft", along(LANE_YAW["Right"], shaft_d - 150, 0, deck_z + 300), LANE_YAW["Right"]),  # drones leave the shaft above the deck
    ]
    entrances = []
    for label, at, lane_yaw in entrance_points:
        point = actors.spawn_actor_from_class(unreal.TargetPoint, at, unreal.Rotator(0, 0, lane_yaw + 180.0))
        point.set_actor_label(label)
        point.set_folder_path("Gameplay/Entrances")
        entrances.append(point)

    director = actors.spawn_actor_from_class(unreal.EncounterDirector, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    director.set_actor_label("EncounterDirector")
    director.set_folder_path("Gameplay")
    director.set_editor_property("entrances", entrances)
    director.set_editor_property("entrance_scatter", 120.0)
    # Phases stay empty: the game mode plays the stand-in waves until the Level 1 schedule lands (T-0026).

    sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1500), unreal.Rotator(0, -50, 20))  # from behind the rig: faces toward the player are lit
    sun.set_actor_label("ChamberLight")
    sun.set_folder_path("Lighting")
    light = sun.get_component_by_class(unreal.DirectionalLightComponent)
    light.set_mobility(unreal.ComponentMobility.MOVABLE)
    light.set_cast_shadows(False)
    light.set_intensity(6.0)

    if not levels.save_current_level():
        raise RuntimeError("Could not save " + MAP_PATH)
    unreal.log("L1 greybox generated: " + MAP_PATH)


build()
