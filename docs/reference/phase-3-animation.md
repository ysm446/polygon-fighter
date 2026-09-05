# Phase 3 — 物理アニメーション

作成日時: 2026-09-05 14:38
更新日時: 2026-09-05 14:38

## 実装した動作

P入力またはImGuiのPunchで右腕を前方（-Z）へ伸ばし、Idleへ戻る。パンチ中の再入力は無視する。骨盤固定と既存の物理衝突を維持する。命中判定・HP・ダメージはまだ追加していない。

`src/animation/Pose.h` はTranslation・Quaternion Rotation・Scaleを持つPoseを定義し、`AnimationPlayer.*` はClipの時間サンプリング、補間、Idleループ、Punchからの復帰、再生速度を管理する。回転はDirectXMathのSlerp、TranslationとScaleは線形補間を使う。

今回のクリップは回転のみを変化させる。Translationは0、Scaleは1で固定し、PhysicsRigへの適用は関節の目標回転だけとする。身体寸法・関節位置のアニメーションは未対応。

## パンチのクリップ

通常速度で1秒。右肩と右肘の親に対するX軸回転を次のように補間する。

| 時間（秒） | 右肩 | 右肘 | 動作 |
| --- | --- | --- | --- |
| 0 | 0度 | 0度 | Idle |
| 0.15 | 20度 | 75度 | 予備動作 |
| 0.35 | 90度 | 0度 | 伸展 |
| 0.48 | 90度 | 0度 | 保持 |
| 0.85 | 0度 | 0度 | 復帰 |
| 1.00 | 0度 | 0度 | Idleへ切替 |

Idleは静止姿勢を2秒周期でループする。Playback speedは0.25〜2倍。これらは物理アニメーションの検証用時間であり、格闘ゲームのStartup / Active / Recoveryの確定値ではない。

## 物理との接続

固定60Hzの各ステップで、アニメーションを進め、関節の `SetTargetOrientationBS` を更新してからJoltを進める。通常動作では剛体の位置・回転を直接書き換えない。右肩・右肘のSwing範囲はパンチ用に110度へ拡張した。肘の正確な解剖学的制限は引き続き未対応。

リセット時は再生状態をIdleへ戻し、関節目標と身体の初期姿勢・速度、Warm Startをクリアする。再生速度と関節設定は維持する。

## 可視化

- 水色：現在の物理剛体の中心と親子リンク。
- 緑色：目標回転から階層順に計算した中心と親子リンク。
- 両Skeletonは身体に隠れないデバッグ描画とし、個別に表示を切り替えられる。
- 目標Skeletonは既定でX方向に1.8mずらして表示する。オフにすると実際の身体へ重なる。このオフセットは物理に影響しない。

中心点とリンクによる簡易Skeletonであり、Blenderのスキニング用Skeletonではない。

## 検証

Debug / Releaseともビルドに成功し、各4件のCTestが成功した。パンチ中と復帰後のDX12キャプチャを生成し、パンチ中の画像で右腕の伸展と両Skeletonの表示を確認した。Debug Layerの警告・エラーは0件。

新しいテストは補間、Quaternionの符号違い、Idleループ、2倍速、再入力の抑止、物理的な伸展、モーターOFF、Idle復帰、リセット、30 / 60 / 144Hz相当での軌道一致を検証する。Debugで右前腕の中心は約0.615m前進し、モーターOFFでは約0.000005mだった。入力直後に剛体が移動しないことも確認した。

スモークテストは1秒後にパンチを開始し、伸展時の `smoke-punch.bmp` と復帰後の `smoke.bmp` を実行ファイルの隣へ保存する。最終時点のIdle復帰と姿勢誤差を判定する。UIの手動クリック検証は未実施。

## 次の段階

Phase 4で2体目、攻撃フレーム、Hitbox / Hurtbox、HP、Hitstun、KO、単純なGuardを導入する。

## 参照

- [DirectXMathのQuaternion補間](https://learn.microsoft.com/en-us/windows/win32/api/directxmath/nf-directxmath-xmquaternionslerp)
