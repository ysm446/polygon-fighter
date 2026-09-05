# Phase 6 — Blenderモデル・モーションと組み込み

作成日時: 2026-09-05 15:28
更新日時: 2026-09-05 16:10

## 制作物

Phase 7の0.7.0で男女にWalkを追加した。さらに0.8.0で[Kick](phase-7-kick.md)を追加し、現在の.blend・GLBと再出力スクリプトは5クリップを扱う。[移動資料](phase-7-movement.md)を参照する。下記のPhase 6検証結果は0.6.0時点の記録。

Blender 5.2.1 LTSで、参考画像と[男女の設定画](character-concepts.md)をもとにローポリモデルを制作した。開発版0.6.0では通常起動時にこのモデルを表示する。

| キャラクター | デザイン | 三角形数 | 編集用 | ゲーム用 | プレビュー |
| --- | --- | --- | --- | --- | --- |
| 男性 / P1 | 白い道着、黒い帯とパンツ、短髪、裸足 | 1,109 | [male-fighter.blend](../../assets/characters/models/male-fighter.blend) | [GLB](../../assets/characters/models/male-fighter.glb) | [PNG](../../assets/characters/models/male-fighter-preview.png) |
| 女性 / P2 | 赤い立ち襟の武術服、黒い帯とパンツ、ポニーテール、靴 | 1,209 | [female-fighter.blend](../../assets/characters/models/female-fighter.blend) | [GLB](../../assets/characters/models/female-fighter.glb) | [PNG](../../assets/characters/models/female-fighter-preview.png) |

男女で共通の11ボーンを使う。Blender内の頂点数は男性658・女性708。フラット法線とマテリアル境界でglTFの頂点が分割され、読み込み後は男性2,415・女性2,625頂点になる。腕・顔・髪・帯などは単一メッシュ内の複数の形状で構成し、部位を主なボーンへ、首と帯の一部を2ボーンへ重み付けした。初期のローポリ版であり、体格差と細部の造形は今後調整できる。

`docs/references/` はユーザーの指定でGitから除外する。ローカルにある画像は削除していない。設定画と制作物は管理対象とし、モデル再生成やビルドは参考画像ファイルに依存しない。

## モーションの段階

要望に従い、予備動作・インパクト・反動と収束を区別した。Punchは60Hzで1秒、戦闘データのStartup / Active / Recoveryは21 / 8 / 31のまま。

| 時間 | 動作 |
| --- | --- |
| 0〜0.23秒 | 腕を引き、肘を95度まで曲げる。上体も逆方向へ7度ひねって溜める |
| 0.23〜0.35秒 | 腕を伸ばし、上体を8度ひねり返して前へ傾ける |
| 0.35〜0.40秒 | 肩90度・肘0度の打点を短く保持し、インパクトを示す |
| 0.40〜0.60秒 | 肘を戻し、上体も逆方向へ返して反動を表す |
| 0.60〜1.00秒 | 小さな揺り戻しを経てIdleへ収束する |

打点の保持はアニメーションの目標姿勢で行い、シミュレーション全体を停止するHitstopは実装していない。実際の腕は関節モーターで追従するため遅れや接触による差がある。被弾側にはPhase 5の命中インパルスを適用する。

Idleは2秒の静止姿勢ループ、Guardは0.15秒で両腕を構え、その姿勢を保持する。Blenderから出力したキーを読み込み、現在のガード量に応じてGuardの導入部分をサンプルする。モーションは男女共通で、今後のキックや被弾動作でも段階構成を維持する。

## 制作と再出力

通常のビルドにはBlenderは不要。CMakeが保存済みのGLBを実行ファイル隣の `assets/characters/` へコピーする。配布時は実行ファイル・shaders・assetsを一緒に置く。

制作スクリプトから初期モデルを再生成する場合は以下を使う。この処理は男女の.blend・GLB・プレビューを上書きするため、手作業で編集した.blendの保存先とは区別する。

```powershell
$blenderPath = 'C:/Program Files/Blender Foundation/Blender 5.2/blender.exe'
$scriptPath = 'tools/blender/create_fighters.py'
& $blenderPath --background --factory-startup --python-exit-code 1 --python $scriptPath
```

既存の.blendを編集した後、制作スクリプトを再実行せずGLBだけを書き出すには次を使う。モデルとアニメーションは入力.blendから取得する。

```powershell
$blenderPath = 'C:/Program Files/Blender Foundation/Blender 5.2/blender.exe'
$blendPath = 'path/to/edited-fighter.blend'
$scriptPath = 'tools/blender/export_fighter.py'
$outputPath = 'path/to/fighter.glb'
& $blenderPath --background $blendPath --python-exit-code 1 --python $scriptPath -- $outputPath
```

ゲームへ反映する場合は対象の `assets/characters/models/` 内のGLBを更新してからビルドする。.blendの変更だけではゲームへ反映しない。

## 座標と骨格の契約

制作スクリプトはゲームのY-up / -Z正面で形状を定義し、Blender座標へ `(x, -z, y)` で変換する。glTF出力時はY-upへ戻す。モデルの横方向の原点は0、接地面はY=0。P1の配置では物理骨盤のX=0.9へ移す。

骨名・親子関係・関節位置は `src/character/Humanoid.h` と対応する。Pelvis・Torso・Headと左右の上下腕・上下脚の11本。指・足・髪用の独立した骨は追加していない。男女で物理寸法を共通にして、見た目のメッシュと服で違いを付ける。

ローダーはglTFのバインド行列を読み、各ジョイントを同名の物理部位へ対応づける。最終Skinning行列は、glTFの逆バインド行列・バインド姿勢・物理部位の基準姿勢と現在姿勢から生成する。P2の180度回転配置もこの経路で扱う。頂点シェーダーで最大4つの重みを合成し、CPUから頂点位置を毎フレーム上書きしない。

アニメーションはノード階層のローカル回転からワールド姿勢を求め、バインド姿勢との差を物理関節の相対回転へ変換する。モデルの最終描画はアニメーションの目標ではなく物理姿勢を使う。

## 読み込み仕様と依存

[cgltf](https://github.com/jkuhlmann/cgltf) v1.15、コミット `360db1a95480fe102ae9c69b27c5d101167ff5ba` を `third_party/cgltf/` に無改変で同梱した。ライセンスはMIT、原文を同じディレクトリに保存している。

本ゲームのローダーは、単一メッシュ・単一Skin・11ジョイント・Idle / Punch / Guard / Walk / Kickの5クリップを持つ自己完結したGLBに限定する。POSITION / NORMAL / JOINTS_0 / WEIGHTS_0、三角形インデックス、単色マテリアルを使う。STEP / LINEARのアニメーションを読み込み、固定のTranslation / Scaleを検証する。画像テクスチャ・Morph・圧縮・任意のリグや動的なTranslation / Scaleは対象外。不正なGLBや異なる骨名・関節配置は明示的に拒否する。

glTFの単色マテリアルは線形色として読み、現在のUNORM出力へモデルの色を変換する。照明は既存の簡易拡散照明。髪や衣服は骨への追従のみで、独立した物理演算は行わない。

## 検証結果

- Debug / Releaseビルドと各7件のCTestに成功。最終ビルドで警告・エラーなし。
- バインド姿勢で頂点が変化しないこと、回転・移動したP2配置のSkinning行列を検証した。
- モーションの回転軸・打点のタイミング、予備動作・インパクト・反動をテストした。
- 読み込んだ男女のモーションで双方の命中・ガード・被弾後の復帰・KOと試合リセットを確認した。
- 切れたGLBと不明な骨名を拒否することを確認した。
- Blenderの制作・プレビュー生成、保存済み.blendからの再出力とPython構文確認に成功した。
- DX12で起動・リサイズ・正常終了に成功し、Debug Layerの警告・エラーは0件。予備動作・打点・反動の3枚のキャプチャでモデル表示を確認した。

スモークテストの画像とログは `build/Debug/` または `build/Release/` に出力する。UIの全ボタンの手動クリック確認と今回のWARP再実行は未実施。自律バランス・ラウンド制などの残作業は[進捗](../plan/progress.md)を参照する。
