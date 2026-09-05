"""現在開いている.blendのFighterRigを、ゲーム用の単一GLBへ書き出す。"""
import bpy
import sys
from pathlib import Path

arguments = sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
if len(arguments) != 1:
    raise RuntimeError('出力先.glbを -- の後に指定してください。')
output = Path(arguments[0]).resolve()
if output.suffix.lower() != '.glb':
    raise RuntimeError('出力拡張子は.glbにしてください。')
rig = bpy.data.objects.get('FighterRig')
if rig is None or rig.type != 'ARMATURE':
    raise RuntimeError('FighterRigアーマチュアが見つかりません。')
meshes = [obj for obj in bpy.data.objects if obj.type == 'MESH' and obj.parent == rig]
if len(meshes) != 1:
    raise RuntimeError('FighterRigの直下に1個のメッシュが必要です。')
for name in ['Idle','Punch','Guard','Walk','Kick']:
    if bpy.data.actions.get(name) is None:
        raise RuntimeError(f'{name}アニメーションが見つかりません。')
bpy.ops.object.select_all(action='DESELECT')
rig.select_set(True)
meshes[0].select_set(True)
bpy.context.view_layer.objects.active = rig
rig.animation_data.action = bpy.data.actions['Idle']
bpy.context.scene.frame_set(0)
output.parent.mkdir(parents=True,exist_ok=True)
bpy.ops.export_scene.gltf(filepath=str(output),export_format='GLB',use_selection=True,
    export_animations=True,export_animation_mode='ACTIONS',export_force_sampling=True,
    export_frame_range=False,export_skins=True,export_def_bones=True,export_yup=True,
    export_materials='EXPORT')
print('EXPORTED',output)
