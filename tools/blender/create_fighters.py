"""Blenderで編集可能な男女のローポリモデルとglTFを制作する。座標定義はゲームのY-up/-Z正面。"""
import bpy
import bmesh
import math
import json
from pathlib import Path
from mathutils import Vector, Quaternion

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / 'assets/characters/models'
OUTPUT.mkdir(parents=True, exist_ok=True)
NAMES = ['Pelvis', 'Torso', 'Head', 'LeftUpperArm', 'LeftLowerArm', 'RightUpperArm',
         'RightLowerArm', 'LeftUpperLeg', 'LeftLowerLeg', 'RightUpperLeg', 'RightLowerLeg']
PARENTS = [-1, 0, 1, 1, 3, 1, 5, 0, 7, 0, 9]
PIVOTS = [(0,1.02,0), (0,1.16,0), (0,1.66,0), (-.4,1.6,0), (-.4,1.205,0),
          (.4,1.6,0), (.4,1.205,0), (-.14,.89,0), (-.14,.475,0), (.14,.89,0), (.14,.475,0)]

def coord(point):
    x, y, z = point
    return (x, -z, y)

class Mesh:
    def __init__(self):
        self.vertices, self.faces, self.materials, self.weights = [], [], [], []

    def shape(self, vertices, faces, material, bone):
        offset = len(self.vertices)
        self.vertices.extend(coord(p) for p in vertices)
        weights = {bone: 1.0} if isinstance(bone, int) else bone
        self.weights.extend([weights.copy() for _ in vertices])
        for face in faces:
            # 大きな面を三角形にして面の陰影を残す。
            for index in range(1, len(face)-1):
                self.faces.append(tuple(offset + i for i in (face[0], face[index], face[index+1])))
                self.materials.append(material)

    def loft(self, rings, material, bone):
        vertices = []
        for x, y, z, width, depth in rings:
            for a, b in [(-.7,-1),(.7,-1),(1,-.5),(1,.5),(.7,1),(-.7,1),(-1,.5),(-1,-.5)]:
                vertices.append((x+a*width, y, z+b*depth))
        faces = [tuple(range(7,-1,-1)), tuple((len(rings)-1)*8+i for i in range(8))]
        for ring in range(len(rings)-1):
            for i in range(8):
                j = (i+1)%8
                faces.append((ring*8+i, ring*8+j, (ring+1)*8+j, (ring+1)*8+i))
        self.shape(vertices, faces, material, bone)

    def panel(self, points, material, bone):
        if bone == 1:
            points = [(x,y,min(z,-.183)) for x,y,z in points]
        self.shape(points, [tuple(range(len(points)))], material, bone)

def material(name, color):
    item = bpy.data.materials.new(name)
    item.diffuse_color = (*color, 1)
    item.use_nodes = True
    shader = item.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Base Color'].default_value = (*color,1)
    shader.inputs['Roughness'].default_value = .85
    return item

def build(female):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.preferences.filepaths.save_version = 0
    scene = bpy.context.scene
    scene.render.fps = 60
    label = 'female-fighter' if female else 'male-fighter'
    colors = [material('Skin', (.66,.39,.19) if female else (.72,.47,.25)),
              material('Tunic', (.57,.025,.02) if female else (.83,.79,.65)),
              material('Trousers', (.045,.055,.065)), material('Belt and wraps', (.012,.018,.022)),
              material('Hair', (.012,.009,.008)), material('Eyes', (.86,.86,.76)),
              material('Face detail', (.035,.016,.009)), material('Lapel', (.72,.61,.42) if not female else (.76,.045,.025)),
              material('Lips', (.36,.025,.018))]
    mesh = Mesh()
    # 骨盤・帯・裾。女性は長い赤い裾、男性は短い白い道着。
    mesh.loft([(0,.83,0,.19,.115),(0,1.05,0,.19,.12),(0,1.16,0,.19,.12)],2,0)
    mesh.loft([(0,.85 if female else .94,0,.265,.17),(0,1.12,0,.21,.145)],1,0)
    mesh.loft([(0,1.105,0,.221,.151),(0,1.175,0,.225,.153)],3,{0:.7,1:.3})
    for side in [-1,1]:
        mesh.panel([(side*.015,1.13,-.157),(side*.067,1.13,-.159),
                    (side*.09,.89,-.19),(side*.03,.88,-.19)],3,0)
    width = .255 if female else .295
    mesh.loft([(0,1.16,0,.205,.135),(0,1.34,0,.235 if female else .275,.17),
               (0,1.51,0,width,.17),(0,1.61,0,width*.9,.125)],1,1)
    # 襟と胸元の色面。
    if female:
        mesh.loft([(0,1.59,0,.115,.095),(0,1.69,0,.105,.09)],7,1)
        mesh.panel([(-.075,1.60,-.132),(.075,1.60,-.132),(0,1.43,-.176)],3,1)
        mesh.panel([(-.20,1.60,-.13),(-.10,1.63,-.133),(.055,1.18,-.143),(-.01,1.18,-.143)],7,1)
    else:
        mesh.panel([(-.15,1.608,-.13),(.15,1.608,-.13),(0,1.36,-.174)],0,1)
        mesh.panel([(.16,1.615,-.135),(.23,1.60,-.135),(-.04,1.17,-.145),(-.12,1.17,-.145)],7,1)
        mesh.panel([(-.21,1.60,-.137),(-.15,1.61,-.137),(.015,1.375,-.178),(-.03,1.33,-.177)],1,1)
    mesh.loft([(0,1.60,0,.08,.075),(0,1.77,0,.078,.078)],0,{1:.3,2:.7})
    # 頭は顎・頬・こめかみの輪郭を持たせ、髪と顔を別の色面で構成する。
    face_width = .118 if female else .135
    mesh.loft([(0,1.705,-.012,.075,.075),(0,1.76,-.005,face_width*.9,.115),
               (0,1.86,0,face_width,.127),(0,1.96,.005,face_width*.96,.12),
               (0,2.005,.015,.085,.08)],0,2)
    mesh.loft([(0,1.955,.015,face_width*1.035,.126),(0,2.045,.024,face_width*.95,.113),
               (0,2.075,.023,.063,.068)],4,2)
    # 後頭部の髪、角張った前髪。
    mesh.loft([(0,1.78 if female else 1.86,.075,face_width*.86,.06),
               (0,1.97,.07,face_width,.072)],4,2)
    for side in [-1,1]:
        mesh.panel([(side*face_width*.96,1.97,-.117),(side*.022,2.005,-.119),
                    (side*.045,1.925 if not female else 1.89,-.137)],4,2)
        mesh.loft([(side*(face_width+.012),1.81,0,.019,.035),
                   (side*(face_width+.012),1.88,0,.02,.03)],0,2)
        # 白目と瞳、内側を低くした眉。
        mesh.panel([(side*.024,1.876,-.130),(side*.096,1.889,-.130),
                    (side*.088,1.862,-.132),(side*.025,1.858,-.132)],5,2)
        mesh.panel([(side*.047,1.878,-.134),(side*.066,1.882,-.134),
                    (side*.066,1.860,-.135),(side*.047,1.858,-.135)],6,2)
        mesh.panel([(side*.018,1.886,-.133),(side*.104,1.907,-.128),
                    (side*.10,1.924,-.125),(side*.025,1.903,-.131)],4,2)
    mesh.shape([(-.019,1.876,-.126),(.019,1.876,-.126),(0,1.819,-.167),
                (-.028,1.813,-.126),(.028,1.813,-.126)],[(0,1,2),(0,2,3),(1,4,2),(3,2,4)],0,2)
    mesh.panel([(-.044,1.785,-.116),(0,1.793,-.125),(.044,1.785,-.116),
                (0,1.777,-.123)],8 if female else 6,2)
    if female:
        mesh.loft([(0,1.99,.13,.041,.036),(0,2.00,.18,.044,.033)],1,2)
        mesh.loft([(0,2.015,.17,.055,.055),(0,1.93,.25,.065,.055),
                   (0,1.76,.29,.045,.04),(0,1.68,.34,.005,.006)],4,2)
        for side in [-1,1]:
            mesh.panel([(side*.12,1.98,-.07),(side*.137,1.84,-.065),
                        (side*.115,1.72,-.05)],4,2)
    else:
        for x, height in [(-.09,2.085),(-.04,2.105),(.02,2.09),(.075,2.10)]:
            mesh.shape([(x-.035,2.015,-.075),(x+.035,2.025,-.07),(x+.02,height,.01),
                        (x-.02,2.035,.085)],[(0,1,2),(1,3,2),(3,0,2),(0,3,1)],4,2)
    for side, upper, lower, thigh, shin in [(-1,3,4,7,8),(1,5,6,9,10)]:
        x = side*.4
        muscle = .104 if female else .125
        mesh.loft([(x,1.19,0,.072,.077),(x,1.32,0,muscle,.105),
                   (x,1.49,0,muscle*1.05,.112),(side*.36,1.61,0,muscle*1.2,.10)],0,upper)
        mesh.loft([(x,.865,0,.062,.064),(x,1.04,0,.087,.085),
                   (x,1.205,0,.074,.077)],0,lower)
        mesh.loft([(x,.875,0,.07,.072),(x,.965,0,.077,.078)],3,lower)
        mesh.loft([(x,.75,-.012,.062,.058),(x,.80,-.016,.078,.075),
                   (x,.885,0,.065,.069)],0,lower)
        mesh.loft([(x-side*.072,.78,-.015,.024,.035),(x-side*.075,.85,-.025,.024,.034)],0,lower)
        lx = side*.14
        mesh.loft([(lx,.475,0,.105,.12),(lx,.68,0,.12,.14),(lx,.92,0,.118,.14)],2,thigh)
        mesh.loft([(lx,.085,0,.075 if female else .107,.10),
                   (lx,.25,0,.108,.12),(lx,.475,0,.106,.121)],2,shin)
        mesh.loft([(lx,.025,-.085,.089,.17),(lx,.07,-.085,.09,.17),
                   (lx,.135,-.015,.07,.095)],3 if female else 0,shin)
    data = bpy.data.meshes.new(label)
    data.from_pydata(mesh.vertices, [], mesh.faces)
    data.update()
    obj = bpy.data.objects.new(label, data)
    scene.collection.objects.link(obj)
    for mat in colors:
        data.materials.append(mat)
    for poly, index in zip(data.polygons,mesh.materials):
        poly.material_index = index
        poly.use_smooth = False
    bm = bmesh.new()
    bm.from_mesh(data)
    bmesh.ops.recalc_face_normals(bm,faces=bm.faces)
    bm.to_mesh(data)
    bm.free()
    arm = bpy.data.armatures.new('FighterSkeleton')
    rig = bpy.data.objects.new('FighterRig',arm)
    scene.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    for index,name in enumerate(NAMES):
        bone = arm.edit_bones.new(name)
        bone.head = coord(PIVOTS[index])
        # 共通のローカル軸で回転を記録する。ボーンの長さは表示用。
        bone.tail = Vector(bone.head) + Vector((0,.1,0))
        if PARENTS[index] >= 0:
            bone.parent = arm.edit_bones[NAMES[PARENTS[index]]]
    bpy.ops.object.mode_set(mode='OBJECT')
    rig.show_in_front = True
    for index,name in enumerate(NAMES):
        group = obj.vertex_groups.new(name=name)
        for vertex,weights in enumerate(mesh.weights):
            if index in weights:
                group.add([vertex],weights[index],'REPLACE')
    modifier = obj.modifiers.new('Physical skeleton skin','ARMATURE')
    modifier.object = rig
    obj.parent = rig
    rig.animation_data_create()
    # time / 肩 / 肘 / 上体のひねり / 上体の前後傾斜。
    # 引き→溜め→加速→打点の短い保持→反動→収束を1秒に収める。
    for name,keys in [('Idle',[(0,0,0,0,0),(2,0,0,0,0)]),
                      ('Punch',[(0,0,0,0,0),(.10,-8,45,-4,2),(.23,15,95,-7,3),
                                (.35,90,0,8,-5),(.40,90,0,8,-5),(.48,78,18,5,-2),
                                (.60,25,70,-3,2),(.82,-3,5,1,-1),(1,0,0,0,0)]),
                      ('Guard',[(0,0,0,0,0),(.15,20,75,0,0),(1,20,75,0,0)])]:
        action = bpy.data.actions.new(name)
        action.use_fake_user = True
        rig.animation_data.action = action
        for time,shoulder,elbow,twist,lean in keys:
            for index,bone in enumerate(rig.pose.bones):
                angle = shoulder if bone.name == 'RightUpperArm' or name == 'Guard' and bone.name == 'LeftUpperArm' else \
                        elbow if bone.name == 'RightLowerArm' or name == 'Guard' and bone.name == 'LeftLowerArm' else 0
                bone.rotation_mode = 'QUATERNION'
                bone.rotation_quaternion = Quaternion((1,0,0),math.radians(angle))
                if bone.name == 'Torso':
                    bone.rotation_quaternion = Quaternion((0,0,1),math.radians(twist)) @ Quaternion((1,0,0),math.radians(lean))
                elif bone.name == 'Head':
                    bone.rotation_quaternion = Quaternion((1,0,0),math.radians(-lean*.35))
                bone.keyframe_insert('rotation_quaternion',frame=time*60,group=bone.name)
        # 物理パンチのフレーム契約に合わせて線形補間する。
        for layer in action.layers:
            for strip in layer.strips:
                for bag in strip.channelbags:
                    for curve in bag.fcurves:
                        for key in curve.keyframe_points:
                            key.interpolation = 'LINEAR'
    rig.animation_data.action = bpy.data.actions['Idle']
    scene.frame_start, scene.frame_end = 0, 120
    scene.frame_set(0)
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.export_scene.gltf(filepath=str(OUTPUT / (label+'.glb')),export_format='GLB',
        use_selection=True, export_animations=True, export_animation_mode='ACTIONS',
        export_force_sampling=True, export_frame_range=False, export_skins=True,
        export_def_bones=True, export_yup=True, export_materials='EXPORT')
    # .blendではモデルを編集でき、カメラと照明で同じプレビューを再生成できる。
    bpy.ops.object.camera_add(location=coord((2.7,1.8,-5)))
    camera = bpy.context.object
    direction = Vector(coord((0,1.05,0))) - camera.location
    camera.rotation_euler = direction.to_track_quat('-Z','Y').to_euler()
    camera.data.type = 'ORTHO'
    camera.data.ortho_scale = 2.55
    scene.camera = camera
    for position,energy,size in [((2,4,-4),450,4),((-3,2,-1),220,3),((1,3,3),350,3)]:
        bpy.ops.object.light_add(type='AREA',location=coord(position))
        lamp = bpy.context.object
        lamp.data.energy, lamp.data.shape, lamp.data.size = energy,'DISK',size
        lamp.rotation_euler = (Vector(coord((0,1,0)))-lamp.location).to_track_quat('-Z','Y').to_euler()
    scene.world = bpy.data.worlds.new('Studio')
    scene.world.use_nodes = True
    scene.world.node_tree.nodes['Background'].inputs[0].default_value = (.16,.19,.23,1)
    scene.world.node_tree.nodes['Background'].inputs[1].default_value = .5
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = 32
    scene.render.resolution_x, scene.render.resolution_y = 900,1100
    scene.render.resolution_percentage = 100
    scene.view_settings.view_transform = 'Standard'
    scene.view_settings.exposure = -.7
    scene.render.image_settings.file_format = 'PNG'
    scene.render.filepath = str(OUTPUT / (label+'-preview.png'))
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT / (label+'.blend')))
    bpy.ops.render.render(write_still=True)
    return {'file':label, 'vertices':len(data.vertices),'triangles':len(data.polygons),
            'bones':len(NAMES),'animations':['Idle','Punch','Guard']}

results = [build(False),build(True)]
(OUTPUT / 'manifest.json').write_text(json.dumps({'blender':bpy.app.version_string,'models':results},indent=2)+'\n',encoding='utf-8')
print('FIGHTERS_CREATED',json.dumps(results))
