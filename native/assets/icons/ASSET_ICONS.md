# Impulso asset icons

Put the final PNG icons in this directory. Recommended source size: 128x128 px,
transparent background, sRGB. Keep important artwork inside a 108x108 px safe area.

## Browser entries

| File | Asset kind | Extensions / discriminator |
|---|---|---|
| `folder.png` | Folder | directory |
| `folder_open.png` | Open folder | directory |
| `level.png` | Level / Scene | `.imp` |
| `prefab.png` | Prefab | `.pfb` |
| `blueprint.png` | Blueprint component/script | `.bp`, base class |
| `blueprint_gamemode.png` | GameMode Blueprint | `.bp`, class `GameMode` |
| `blueprint_gameinstance.png` | GameInstance Blueprint | `.bp`, class `GameInstance` |
| `blueprint_playercontroller.png` | PlayerController Blueprint | `.bp`, class `PlayerController` |
| `blueprint_savegame.png` | SaveGame Blueprint | `.bp`, class `SaveGame` |
| `blueprint_interface.png` | Blueprint Interface | `.bpi` |
| `enum.png` | Enumeration | `.enum` |
| `curve.png` | Float Curve | `.curve` |
| `animation_clip.png` | Animation Clip | `.anim` |
| `animator_controller.png` | Animator Controller | `.animctrl` |
| `mesh_obj.png` | OBJ Mesh | `.obj` |
| `mesh_fbx.png` | FBX Mesh | `.fbx` |
| `mesh_gltf.png` | glTF Mesh | `.gltf` |
| `mesh_glb.png` | Binary glTF Mesh | `.glb` |
| `mesh_dae.png` | Collada Mesh | `.dae` |
| `mesh_3ds.png` | 3D Studio Mesh | `.3ds` |
| `mesh_stl.png` | STL Mesh | `.stl` |
| `texture.png` | Texture | `.png` |
| `audio_wav.png` | WAV Audio | `.wav` |
| `audio_mp3.png` | MP3 Audio | `.mp3` |
| `audio_ogg.png` | Ogg Vorbis Audio | `.ogg` |
| `audio_class.png` | Audio Class | `.aclass` |
| `audio_attenuation.png` | Audio Attenuation | `.atten` |
| `audio_concurrency.png` | Audio Concurrency | `.concurrency` |
| `material.png` | Material | `.mat` |
| `widget.png` | UI Widget | `.wgt` |
| `generic_asset.png` | Recognized fallback asset | any future registered type |
| `unknown_file.png` | Unknown file | unregistered extension |

The `.bp` variants share an extension, so their icon is selected from the class
metadata stored inside the asset rather than from the filename.

## Outliner entity icons

Drawn to the left of each row in the world outliner (replacing the old text
prefixes). Selected by entity component/shape, via `UI::treeItem(... iconImage)`.

| File | Entity kind |
|---|---|
| `ent_light.png` | Light component |
| `ent_camera.png` | Camera component |
| `ent_mesh.png` | Imported mesh (has `meshAsset`) |
| `ent_cube.png` | Box primitive (also the default) |
| `ent_sphere.png` | Sphere primitive |
| `ent_cylinder.png` | Cylinder primitive |
| `ent_cone.png` | Cone primitive |
| `ent_capsule.png` | Capsule primitive |
