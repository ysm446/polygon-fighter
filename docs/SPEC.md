# Physics Fighting Game Prototype — SPEC.md

## 1. Project Goal

DirectX 12 を使用して、初期の『バーチャファイター』のようなシンプルな3D格闘ゲームを開発する。

本プロジェクトの最大の特徴は、キャラクターの動きを完全なキーフレームアニメーションだけで処理するのではなく、

- 事前に作成されたモーション
- リアルタイム物理シミュレーション

を組み合わせてキャラクターを動かすことである。

最終的には、パンチやキックなどの技はある程度決められたモーションを再生しつつ、衝突、よろけ、姿勢変化、転倒などについては物理シミュレーションの影響を受ける仕組みを目指す。

---

# 2. Development Philosophy

最初から完成した格闘ゲームを作ろうとしない。

まずは

> 2体の簡易キャラクターが立っており、一方がパンチすると、もう一方が物理的に反応する

ところまでを最初のプロトタイプとする。

グラフィック品質よりも、

- Character Physics
- Animation
- Fighting Logic
- Collision
- Debug Visualization

の検証を優先する。

コードは将来的な拡張を想定してモジュール化するが、初期段階から過剰な抽象化は行わない。

---

# 3. Target Platform

Primary platform:

- Windows 11
- x64

Graphics API:

- DirectX 12

Language:

- C++20 以上

Build system:

- CMake

IDE想定:

- Visual Studio 2022
- VS Code

---

# 4. Main Libraries

## Rendering

DirectX 12

用途:

- Mesh Rendering
- Debug Rendering
- Camera
- Lighting
- Skinning

---

## Physics

Jolt Physics

用途:

- Rigid Body
- Collision
- Constraints
- Character body physics
- Ragdoll
- Joint motor
- Impulse

物理エンジンそのものは自作しない。

---

## Debug / Editor UI

Dear ImGui

用途:

- Character parameters
- Physics parameters
- Animation parameters
- Attack parameters
- Hitbox display
- Debug visualization
- Runtime tuning

---

## Asset Creation

Blender

Blenderでは以下を作成する。

- Character Mesh
- Skeleton
- Animation Clips

初期段階では非常に簡単なモデルでよい。

---

# 5. Dependency Management

可能なら CMake の FetchContent または git submodule を使用する。

想定Dependency:

- Jolt Physics
- Dear ImGui
- DirectXMath

必要に応じて以下を検討する。

- DirectXTex
- DirectXTK12

ただしライブラリを増やしすぎない。

---

# 6. Project Structure

基本構成案:

```text
/FightingGame
    /assets
        /characters
        /animations
        /textures

    /src
        /app
        /render
        /physics
        /animation
        /character
        /combat
        /input
        /debug
        /math

    /shaders

    /third_party

    CMakeLists.txt
    SPEC.md
    README.md
```

---

# 7. Major Systems

以下のシステムを可能な限り分離して実装する。

```text
Application
    |
    +-- Rendering
    |
    +-- Physics
    |
    +-- Animation
    |
    +-- Character
    |
    +-- Combat
    |
    +-- Input
    |
    +-- Debug UI
```

ゲームロジックがDirectX 12へ直接依存しすぎない構造にする。

---

# 8. Character Architecture

Characterは大きく以下から構成する。

```text
Character
|
+-- Skeleton
|
+-- Animation Controller
|
+-- Physics Rig
|
+-- Combat State
|
+-- Hurtboxes
|
+-- Hitboxes
```

---

# 9. Skeleton

最低限以下のようなBoneを想定する。

```text
Root
Pelvis
Spine
Chest
Neck
Head

LeftUpperArm
LeftLowerArm
LeftHand

RightUpperArm
RightLowerArm
RightHand

LeftUpperLeg
LeftLowerLeg
LeftFoot

RightUpperLeg
RightLowerLeg
RightFoot
```

必要に応じて増やす。

初期プロトタイプでは手指などは不要。

---

# 10. Phase 0 Character

最初の段階ではBlender Meshを使わなくてもよい。

以下のPrimitiveで人体を表現できるようにする。

```text
Head        Sphere
Torso       Box / Capsule
Pelvis      Box
Upper Arm   Capsule
Lower Arm   Capsule
Upper Leg   Capsule
Lower Leg   Capsule
```

各Body PartをJolt PhysicsのRigid Bodyとして作成し、Constraintで接続する。

目的はPhysical Animationの成立を確認すること。

---

# 11. Animation System

Animation ClipはSkeletonに対して以下を持つ。

```text
Bone Transform
    Translation
    Rotation
    Scale
```

時間に応じてPoseをサンプリングする。

最低限以下を実装する。

- Animation Clip
- Animation Player
- Pose
- Pose interpolation
- Animation looping
- Playback speed

---

# 12. Initial Animation Clips

最初に必要なAnimationは以下のみ。

```text
Idle
Punch
Guard
```

次の段階で追加する。

```text
WalkForward
WalkBackward
Kick
HitReaction
KnockDown
GetUp
```

---

# 13. Physical Animation

このプロジェクトで最も重要なシステム。

通常のAnimationでは、

```text
Animation Pose
      ↓
Skeleton
      ↓
Mesh
```

となる。

本プロジェクトでは、

```text
Animation
     ↓

Target Pose

     ↓

Physics Controller

     ↓

Physical Skeleton

     ↓

Rendered Skeleton
```

とする。

Animation Poseは直接表示される最終姿勢ではない。

AnimationはPhysics Bodyが目指す

**Target Pose**

として扱う。

---

# 14. Joint Control

各JointはTarget Rotationへ追従する。

基本的にはPD Controllerを使用する。

概念:

```cpp
rotationError = TargetRotation - CurrentRotation;

torque =
    Kp * rotationError
    - Kd * angularVelocity;
```

実際にはQuaternionによるRotation Errorを適切に計算すること。

各Jointについて以下を調整可能にする。

```text
Strength
Stiffness
Damping
MaxTorque
```

---

# 15. Physical Animation Strength

Body PartごとにPhysics追従強度を変更できるようにする。

例:

```text
Pelvis       Strong
Spine        Strong
Head         Medium

UpperArm     Medium
LowerArm     Medium

UpperLeg     Strong
LowerLeg     Strong
```

パンチ中だけ腕のStrengthを上げるなど、Animation Stateから変更できる設計が望ましい。

---

# 16. Physics Blend

将来的には以下の値を持てるようにする。

```text
animationWeight
physicsWeight
```

ただし単純なTransform BlendだけでPhysical Animationを実現しようとしない。

基本はPhysics Bodyを動かし、AnimationはTargetとして扱う。

---

# 17. Balance

最初のプロトタイプでは、キャラクターが完全に自律してバランスを取る必要はない。

まずは

- 足を固定または強く拘束
- Pelvisを安定化
- Upper Bodyのみ物理反応

でもよい。

その後、

- Center of Mass
- Foot contact
- Balance controller
- Recovery step

を追加する。

---

# 18. Combat System

Combat判定とPhysics Collisionは完全に同一にしない。

格闘ゲームとしての再現性を維持するため、

```text
Combat Logic
```

と

```text
Physics Simulation
```

を分ける。

---

# 19. Attack Definition

技はデータとして定義する。

例:

```cpp
struct AttackData
{
    std::string name;

    int startupFrames;
    int activeFrames;
    int recoveryFrames;

    float damage;

    float hitImpulse;

    int hitBone;

    float hitboxRadius;
};
```

将来的にはJSONなど外部データへ移行可能な設計にする。

---

# 20. Frame System

格闘ゲーム部分は固定更新で動作させる。

基準:

```text
60 FPS
```

1フレーム:

```text
1 / 60 second
```

Attackは

```text
Startup
Active
Recovery
```

を持つ。

例:

```text
Punch

Startup  = 7 frames
Active   = 3 frames
Recovery = 12 frames
```

---

# 21. Hitbox

攻撃判定には簡単なPrimitiveを使用する。

例:

```text
Sphere
Capsule
```

PunchならRightHand BoneにHitboxをAttachする。

Active Frameのみ有効にする。

---

# 22. Hurtbox

Characterには身体部位ごとのHurtboxを設定する。

例:

```text
Head
Chest
Abdomen
LeftArm
RightArm
LeftLeg
RightLeg
```

Physics ShapeとCombat Hurtboxは必ずしも同じShapeでなくてよい。

---

# 23. Hit Detection

基本:

```text
Attack Hitbox
      ↓
Hurtbox Intersection
      ↓
Combat Hit
```

Combat Hitが成立したら、

```text
Damage
Hitstun
Physics Impulse
```

を発生させる。

---

# 24. Physics Reaction

Hit時には、攻撃位置と攻撃方向からPhysics BodyへImpulseを加える。

例:

```cpp
physicsBody.AddImpulse(
    attackDirection * attack.hitImpulse,
    contactPoint
);
```

これによって、

- 上半身が揺れる
- 頭が振られる
- 腕が弾かれる

などの反応を発生させる。

---

# 25. Important Rule

ゲームのHit判定は原則としてCombat Systemが決定する。

つまり、

```text
Physics CollisionしたからDamage
```

ではなく、

```text
Combat Hit成立
      ↓
Damage確定
      ↓
PhysicsへImpulse
```

を基本とする。

これにより格闘ゲームとして再現可能な挙動を維持する。

---

# 26. Physical Collision

ただし身体同士のPhysics Collisionは有効にする。

例:

- Punching arm vs Guard arm
- Body vs Body
- Character vs Ground

Physics CollisionによってAnimation Motionが多少阻害されることは許容する。

これが本プロジェクトの特徴となる。

---

# 27. Guard

初期Guardは単純なStateとして実装する。

Guard中に攻撃を受けた場合:

```text
Damage Reduction
Hitstun Reduction
Physics Impulse Reduction
```

を適用する。

さらにPhysical Collisionにより腕が押される挙動を許容する。

---

# 28. Input

初期操作例:

```text
A / D
Movement

P
Punch

G
Guard
```

Gamepad対応は後回しでよい。

将来的にはXbox Controllerを想定する。

---

# 29. Camera

初期カメラは固定の格闘ゲームCameraとする。

Perspective Camera。

常に両Characterが画面内に入るようにする。

初期段階では自動Zoom不要。

---

# 30. Stage

最初は単純なPlaneのみ。

```text
20m x 20m
```

Collision Groundとして使用する。

Ring Outはまだ実装しない。

---

# 31. Rendering

Phase 1では最低限の描画のみ。

必要機能:

- DX12 initialization
- Swap Chain
- Depth Buffer
- Vertex Buffer
- Index Buffer
- Constant Buffer
- Basic Mesh
- Basic Lighting
- Camera
- Primitive Rendering

---

# 32. Character Rendering Phase 1

最初はPhysics Body Primitiveをそのまま描画する。

例:

```text
Capsule
Sphere
Box
```

これによってBlender Asset Pipelineが未完成でもゲームシステムを開発可能にする。

---

# 33. Blender Asset Pipeline

Physics Prototype完成後にBlender Characterを導入する。

Blenderでは、

```text
Mesh
Skeleton
Skin Weight
Animation
```

を作成する。

---

# 34. Asset Format

第一候補:

```text
glTF 2.0
```

必要に応じてFBXを検討する。

可能なら独自FBX Importerは作らない。

glTF読み込みライブラリの使用を検討する。

---

# 35. Skinning

GPU Skinningを使用する。

Vertex ShaderへBone Matrixを渡す。

概念:

```text
Vertex
   ↓
Bone Weights
   ↓
Bone Matrices
   ↓
Skinned Vertex
```

---

# 36. Physics Skeleton and Render Skeleton

Physics SkeletonとRender Skeletonの関係を明確に分離する。

```text
Animation Skeleton
       ↓
Target Pose
       ↓
Physics Skeleton
       ↓
Final Bone Transform
       ↓
Skinning
```

最終MeshはPhysics Skeletonから生成したTransformを使用する。

---

# 37. Debug Visualization

以下を描画可能にする。

```text
Skeleton
Physics Bodies
Physics Constraints
Hitboxes
Hurtboxes
Center of Mass
Contact Points
Attack Direction
Impulse
```

それぞれImGuiからON/OFFできるようにする。

---

# 38. ImGui Panels

最低限以下のPanelを用意する。

## Character Panel

```text
Position
Rotation
Health
Current State
Current Animation
```

---

## Physics Panel

```text
Gravity
Joint Strength
Joint Damping
Joint Max Torque
Body Mass
```

---

## Combat Panel

```text
Attack Name
Startup
Active
Recovery
Damage
Impulse
```

---

## Debug Panel

```text
Show Skeleton
Show Physics Bodies
Show Hitboxes
Show Hurtboxes
Show Contacts
```

---

# 39. Runtime Editing

Physics parameterは可能な限りRuntimeで変更できるようにする。

例:

```text
Arm stiffness
Arm damping
Spine stiffness
Hit impulse
Body mass
```

ゲームを再起動しなくても挙動を比較できることが重要。

---

# 40. Game State

初期状態:

```text
Idle
Attacking
Guarding
Hitstun
KO
```

複雑なState Machineは後回し。

---

# 41. Health

Character:

```text
HP = 100
```

0以下でKO。

初期段階ではRound System不要。

---

# 42. Fixed Simulation

PhysicsおよびCombat LogicはFixed Timestepで更新する。

推奨:

```text
60Hz
```

描画Framerateから独立させる。

必要に応じてPhysicsのみ120Hzを将来的に検討する。

ただし初期版では60Hzでよい。

---

# 43. Determinism

完全なDeterministic Physicsは初期目標としない。

ただしCombat LogicはPhysics挙動から可能な限り独立させ、

同じ入力なら同じAttack Frameが発生する構造にする。

---

# 44. First Playable Prototype

最初のPlayable Buildでは以下のみ実装する。

Character A

Character B

Stage

Camera

Idle

Punch

Guard

HP

Physics Reaction

---

# 45. Prototype Success Condition

Phase 1完成条件:

1. Windows上でDX12 Windowが起動する
2. Groundが表示される
3. 2体の簡易Characterが表示される
4. CharacterがJolt Physics Bodyを持つ
5. CharacterがIdle Poseを維持する
6. ボタン入力でPunchする
7. Punch HitboxがActiveになる
8. 相手のHurtboxへHitする
9. Damageが発生する
10. Hit locationへPhysics impulseが入る
11. 相手の上半身が物理的に反応する
12. Characterが完全に崩壊せず姿勢へ戻ろうとする
13. Debug UIでPhysics parameterを変更できる

これが最初の大きなマイルストーン。

---

# 46. Development Phases

## Phase 1 — Engine Foundation

実装:

- CMake
- DX12 Window
- Renderer
- Camera
- ImGui
- Jolt Physics
- Ground

---

## Phase 2 — Physics Dummy

実装:

- Capsule / Box Character
- Skeleton-like constraints
- Physics body
- Joint motors
- Standing pose

ゴール:

物理人形が立っている。

---

## Phase 3 — Physical Animation

実装:

- Target Pose
- Joint PD controller
- Pose transition
- Idle
- Punch

ゴール:

物理人形がPunch motionを行う。

---

## Phase 4 — Combat

実装:

- Attack state
- Frame data
- Hitbox
- Hurtbox
- Damage
- Hitstun

ゴール:

パンチがゲームとしてHitする。

---

## Phase 5 — Physics Hit Reaction

実装:

- Contact point
- Impulse
- Joint reaction
- Recovery

ゴール:

パンチを受けたCharacterが自然に揺れる。

---

## Phase 6 — Blender Character

実装:

- glTF import
- Skeleton import
- Animation import
- Skinning

ゴール:

Primitive CharacterをBlender Characterへ置き換える。

---

## Phase 7 — Fighting Game Features

追加:

- Forward movement
- Backward movement
- Kick
- Multiple attacks
- Guard
- Hit reactions
- Knockdown
- Get up
- Combo
- Frame advantage

---

# 47. Future Experiments

以下はPrototype後に検討する。

## Physical Guard

攻撃してきた腕とGuarding Armを物理衝突させる。

---

## Dynamic Knockdown

一定以上、

```text
Center of Mass
Balance error
Impulse
```

が発生した場合に転倒する。

---

## Procedural Recovery

倒れかけた場合に、

- Step
- Body correction
- Arm movement

によって姿勢を回復する。

---

## Damage-dependent Physics

Damageによって身体能力を変化させる。

例:

```text
Leg Damage
    ↓
Joint Strength低下

Head Damage
    ↓
Balance低下

Arm Damage
    ↓
Punch Strength低下
```

---

## Weight Classes

Characterごとに、

```text
Mass
Center of Mass
Muscle Strength
Joint Strength
```

を変化させる。

これによって体格差を物理的に表現する。

---

# 48. Coding Guidelines

- C++20
- RAIIを使用する
- raw new/deleteを極力避ける
- std::unique_ptr / std::shared_ptrを適切に使用する
- RendererとGame Logicを分離する
- Global stateを極力避ける
- Debug Buildでassertを使用する
- Warningを無視しない
- 不要な巨大Frameworkを導入しない

---

# 49. Codex Development Instructions

Codexは一度にProject全体を完成させようとしない。

必ずPhase単位で実装する。

各Phase開始前に、

1. 現在のProject Structureを確認
2. SPEC.mdを確認
3. 実装計画を短く提示
4. 必要なFileのみ変更
5. Build
6. Compile Error修正
7. 実行可能状態を維持

すること。

---

# 50. Important Codex Rule

動作する小さなVersionを常に維持する。

以下は禁止。

- 大規模Rewriteを一度に行う
- 未使用のArchitectureを先に大量作成する
- 将来必要になるかもしれない抽象化を先に追加する
- Blender Assetが完成するまでゲーム実装を止める
- Physics Engineを独自実装する

---

# 51. First Codex Task

最初の実装タスクは以下とする。

```text
Create the initial project foundation.

Requirements:

- Windows 11
- C++20
- CMake
- DirectX 12
- Dear ImGui
- Jolt Physics

Create a DX12 application that:

1. Opens a window.
2. Initializes DirectX 12.
3. Displays a simple ground plane.
4. Initializes Jolt Physics.
5. Creates a static physics ground.
6. Creates one dynamic test box.
7. Shows an ImGui debug window.
8. Runs physics using a fixed timestep.
9. Keeps rendering and physics code separated.

Do not implement the fighting game systems yet.

Build and run the application before considering this task complete.
```

---

# 52. Second Codex Task

Phase 1完成後:

```text
Create a simple physics humanoid.

Use primitive physics shapes only.

Body parts:

- pelvis
- torso
- head
- upper arms
- lower arms
- upper legs
- lower legs

Connect them using Jolt constraints.

Create a simple target pose system that allows the humanoid to attempt to maintain a standing pose.

Expose joint stiffness and damping parameters in ImGui.

Do not implement Blender mesh loading yet.
```

---

# 53. Third Codex Task

その次:

```text
Add a simple physical punch.

Requirements:

- Define an idle target pose.
- Define a punch target pose.
- Pressing P performs a punch.
- The right arm should move using joint motor forces rather than directly setting bone transforms.
- The character should return to idle after the punch.
- Show the target skeleton and physical skeleton separately in debug rendering.

Do not add damage or hit detection yet.
```

---

# 54. Core Design Principle

このプロジェクトで最も重要な考え方は以下。

```text
Animation tells the body where it wants to go.

Physics determines where the body actually goes.

Combat logic determines whether an attack succeeds.
```

つまり、

**Animation = Intent**

**Physics = Motion**

**Combat = Rules**

として分離する。

この3つの相互作用によって、決められた格闘ゲームの操作感と、物理シミュレーションによる予測不能な反応を両立する。