# Polygon Fighter

DirectX 12とJolt Physicsで、アニメーションと物理を組み合わせる3D格闘ゲームのプロトタイプ。

現在は **Phase 6のBlenderキャラクター組み込みまで完了**。男女のローポリモデルをglTFから読み込み、物理姿勢からGPU Skinningで描画する。パンチ・ガード・HP・硬直・KO・被弾時の揺れと復帰が動作する。

男性は白い道着と短髪、女性は赤い武術服とポニーテール。編集可能な[男性.blend](assets/characters/models/male-fighter.blend)・[女性.blend](assets/characters/models/female-fighter.blend)、ゲーム用GLB、プレビューを保存した。パンチは腕と上体の予備動作、打点での短い保持、反動からの収束を含む。制作・再出力手順は[Phase 6資料](docs/reference/phase-6-blender.md)を参照する。

## ビルド・起動

Windows x64、Visual Studioの「C++によるデスクトップ開発」、Windows SDK、CMake 3.24以上、Gitが必要。初回のCMake構成時にJoltとImGuiを取得するため、ネットワーク接続が必要。

リポジトリ直下のPowerShellで実行する。以下はこの環境で確認済みのVisual Studio 2026向け。Visual Studio 2022では `$generator` を `Visual Studio 17 2022` に変更する（2022でのビルドは未検証）。

```powershell
$sourceDir = '.'
$buildDir = 'build'
$generator = 'Visual Studio 18 2026'
$configuration = 'Debug'
cmake -S $sourceDir -B $buildDir -G $generator -A x64
cmake --build $buildDir --config $configuration --parallel 8
$appPath = Join-Path $buildDir "$configuration/polygon_fighter.exe"
& $appPath
```

終了したコマンドが失敗した場合は、そのエラーを解消してから次へ進む。Release版は `$configuration` を `Release` に変更してビルドする。実行ファイルと隣接する `shaders/`・`assets/characters/` は一緒に配置する。通常のビルドと起動にはBlenderは不要で、保存済みGLBを使用する。

## 操作

| 操作 | キー / UI |
| --- | --- |
| 一時停止・再開 | `Space` / Pause |
| 停止中に1フレーム進める | `N` / Step |
| HP・戦闘状態・姿勢を初期化する | `R` / Reset match |
| 箱を押し上げて回転させる（物理実験モード） | `I` / Impulse |
| 人形の胴体を押す | `H` / Push torso |
| P1（白）／P2（赤）がパンチする | `P` / `K` / 各Punchボタン |
| P1／P2がガードする | `G` / `O`を押し続ける、または各Auto guard |
| 攻撃・被攻撃判定を表示する | Hitboxes / Hurtboxes |
| 命中位置とインパルス方向を表示する | Impact point / impulse |
| 被弾時の力とガード軽減を調整する | Hit impulse（0〜40 Ns）/ Guard impulse scale（0〜1倍） |
| 被弾設定を初期値へ戻す | Default hit reaction |
| アニメーションの再生速度を変更（物理実験モード） | Playback speed（0.25〜2倍）。対戦は1倍固定 |
| 身体・物理Skeleton・目標Skeletonの表示切替 | Physical bodies / Physical skeleton / Target skeleton |
| Blenderモデルと検証用Boxを切り替える | Blender models（OFFでBox表示） |
| 目標を横に並べる・重ねる | Target offset +1.8 m |
| 人形だけ初期姿勢へ戻す（物理実験モード） | Reset pose |
| 関節の剛性・減衰・最大トルクを変更 | Stiffness / Damping / Max torque |
| 関節モーターを切り替える | Joint motors |
| 関節設定を初期値に戻す | Default motors |
| 重力を変更する | Gravityスライダー |
| 終了 | `Esc` / ウィンドウを閉じる |

ImGuiがキーボード入力を使用している間はゲーム側のショートカットを抑止する。リセットは身体の位置・姿勢・速度を戻し、重力・関節設定・一時停止状態は維持する。モーターを弱めて胴体を押すと反応の違いを比較できる。骨盤の固定はモーターOFF時にも維持する。

パンチ中の追加入力は無視する。一時停止中にパンチを指定した場合は、Stepまたは再開で進む。リセットはパンチを中断しIdleへ戻す。水色は実際の物理Skeleton、緑色は目標Skeletonで、目標の横移動は表示だけに適用する。

ガード・硬直・KO中もパンチを受け付けない。HPは100、通常命中は12ダメージ、ガード時は3ダメージ。攻撃判定は橙色、被攻撃判定は青色で表示する。移動・相手AI・ラウンド制は未実装。

被弾インパルスの初期値は20 Ns、ガード時は25%の5 Ns。命中位置と方向を通常は黄色、ガード時は青緑色で0.5秒間表示する。線の長さは1 Nsあたり0.025 mで、先端の小さな箱が力の向きを示す。0 Nsにすると追加インパルスだけを止め、身体同士の衝突反応と比較できる。試合リセットでも調整値を維持する。

従来の箱と1体の人形による物理実験は次で起動する。

```powershell
$appPath = 'build/Debug/polygon_fighter.exe'
& $appPath --physics-lab
```

## 検証

```powershell
$buildDir = 'build'
$configuration = 'Debug'
ctest --test-dir $buildDir -C $configuration --output-on-failure
```

GLB・Skinning行列・読み込んだモーションでの戦闘、既存の物理・戦闘ルール、DX12起動を含む計7件のテストを実行する。スモークテストはGPUの使えるWindowsデスクトップセッションで実行する。実行ファイルの隣に `smoke.log`、`smoke.bmp`、予備動作の `smoke-anticipation.bmp`、打点の `smoke-punch.bmp`、反動の `smoke-recoil.bmp` が生成される。失敗時の詳細は `error.log` を確認する。

ハードウェアGPUが利用できない場合はWARPへ自動的に切り替える。明示的な確認には次を使う。

```powershell
$appPath = 'build/Debug/polygon_fighter.exe'
& $appPath --smoke-test --warp
```

## 資料

- [仕様](docs/SPEC.md)
- [目的と完成条件](docs/plan/goals.md)
- [実装計画](docs/plan/plan.md)
- [進捗・検証結果](docs/plan/progress.md)
- [Phase 1の構成と依存ライブラリ](docs/reference/phase-1-foundation.md)
- [Phase 2の物理人形](docs/reference/phase-2-humanoid.md)
- [Phase 3の物理アニメーション](docs/reference/phase-3-animation.md)
- [Phase 4の戦闘判定](docs/reference/phase-4-combat.md)
- [Phase 5の被弾反応と姿勢復帰](docs/reference/phase-5-hit-reaction.md)
- [Phase 6のBlenderモデル・モーションと組み込み](docs/reference/phase-6-blender.md)
- [キャラクター設定画](docs/reference/character-concepts.md)
- [変更履歴](docs/changelog.md)

`docs/references/` はローカルの参考画像置き場としてGitから除外する。設定画・制作済みモデル・再生成スクリプトは管理対象に含める。
