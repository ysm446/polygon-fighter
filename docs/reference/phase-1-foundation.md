# Phase 1 — 基盤実装

作成日時: 2026-09-05 14:10
更新日時: 2026-09-05 14:10

## 実装範囲

Windowsアプリの起動から、DX12描画・Joltの物理更新・ImGui表示までを接続した。初期シーンは20m四方の静的な床と、中心高さ4mから落下する一辺1m・質量10kgの動的な箱。床の上面をY=0とし、箱の接地時の中心高さは約0.5mになる。

床は描画・物理とも厚さ0.5mのBoxを使用する。Y軸が上方向、距離はm、時間は秒。重力の初期値はY方向に-9.81m/s²。

## コードの役割

| ファイル | 役割 |
| --- | --- |
| `CMakeLists.txt` | 依存取得、ライブラリ分割、ビルド、シェーダー配置、CTest |
| `package.json` | アプリバージョンの基準。CMakeが読み取る。npm依存はない |
| `src/app/Main.cpp` | Win32ウィンドウ、入力、サイズ変更、メインループ、スモークテスト |
| `src/app/FixedClock.h` | 描画時間から固定60Hz更新を進める蓄積時計 |
| `src/physics/PhysicsWorld.*` | Jolt初期化、衝突レイヤー、床と箱、リセット、重力、インパルス |
| `src/render/Renderer.*` | DX12リソース、カメラ、Mesh描画、ImGuiバックエンド、画面キャプチャ |
| `src/debug/DebugUI.*` | 状態表示と物理調整用のImGuiパネル |
| `shaders/Primitive.hlsl` | 基本ライティングと床のグリッド表示 |
| `tests/PhysicsTests.cpp` | 落下・接地・リセット・固定更新などの検証 |

PhysicsWorldの公開インターフェースは位置・Quaternionなどの標準C++型を返す。描画側にJoltの型を公開せず、物理側はDirectXへ依存しない。Joltの登録と解除はRAIIで管理し、現在は同時に1つのPhysicsWorldを使用する構成。

## 固定更新と停止

物理を1/60秒単位で更新し、描画とは独立させる。長い停止から復帰した際の追いつき処理は最大0.25秒分に制限する。一時停止中は蓄積を破棄し、Stepで1回だけ更新する。ウィンドウ最小化中は更新と描画を休止する。

## DX12の管理

- Swap Chainは2バッファ。Depth Buffer、頂点・インデックス・定数バッファを使用する。
- 床と箱は共通のBox Meshを使い、最終的な物理姿勢を描画へ反映する。
- 初期版では毎フレームFenceでGPU完了を待ち、共有バッファを安全に再利用する。
- サイズ変更時はGPU完了後にバックバッファとDepth Bufferを作り直す。
- Debugビルドでは利用可能ならDX12 Debug Layerを有効化する。
- シェーダーは起動時にコンパイルする。シェーダーだけを編集した場合もビルドで実行ファイル側へコピーする。
- キャプチャは描画結果をReadback BufferからBMPへ保存する。スモークテストの目視確認に使用する。

## 依存バージョン

| 依存 | 固定値 | 用途 |
| --- | --- | --- |
| Jolt Physics | v5.3.0 / `0373ec0dd762e4bc2f6acdb08371ee84fa23c6db` | 剛体シミュレーション |
| Dear ImGui | v1.91.9b / `f5befd2d29e66809cd1110a152e375a7f1981f06` | デバッグUIとWin32 / DX12バックエンド |
| DirectXMath / DirectX 12 | Windows SDK | 数学処理・描画API |

JoltとImGuiはFetchContentでコミットを固定し、`build/_deps/` に取得する。Joltのサンプルと外部ライブラリ側テストはビルド対象外。MSVCのDLLランタイムを使用し、AVX2などの追加CPU命令は必須にしない。

Visual Studio 2026とWindows SDKの組み合わせでJoltの `/Wall` が外部ヘッダーにも警告を出したため、Joltターゲットに `/external:anglebrackets /external:W0` を指定した。プロジェクト自身のソースは `/W4 /permissive- /utf-8` で確認する。

## 検証方法

[README](../../README.md) にビルド・起動・CTestのコマンドを記載している。

物理テストでは床を貫通しないこと、静止高さ、インパルス、位置と速度のリセット、無重力を確認する。描画間隔を30 / 60 / 144Hz相当に変え、同じ0.5秒で30物理ステップとなり、箱の高さが一致することを確認する。一時停止・単一ステップ・長時間停止時の上限も検証する。

`--smoke-test` は実際のDX12アプリを起動し、2回のウィンドウサイズ変更と240ステップ以上の物理更新後、接地高さとDX12警告・エラー数を検証して終了する。正常時の終了コードは0。ハードウェアGPUと `--warp` の双方で確認した。

## 初期版の制限

キャラクター・関節・戦闘は未実装。物理姿勢の描画補間、GPUとCPUの並列化、動的なカメラ、デバイスロストからの自動復帰は未実装。GPU完了待ちは初期版の単純さを優先したもので、性能最適化は後続作業で判断する。ImGuiのSRVは固定したバージョンで必要なフォント用1枠のみ。画像表示やImGui更新時は割り当て方式を拡張する。

## 参照先

- [Jolt Physics公式リポジトリ](https://github.com/jrouwe/JoltPhysics/tree/v5.3.0)
- [Dear ImGui公式DX12サンプル](https://github.com/ocornut/imgui/tree/v1.91.9b/examples/example_win32_directx12)
