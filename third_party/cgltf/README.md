# cgltf

glTF 2.0の解析に使う単一ヘッダーライブラリ。MITライセンスの原文は隣接する `LICENSE` に保持する。

- 配布元: https://github.com/jkuhlmann/cgltf
- バージョン: v1.15
- 固定コミット: `360db1a95480fe102ae9c69b27c5d101167ff5ba`
- 取り込んだファイル: `cgltf.h`、`LICENSE`（無改変）
- `src/assets/CharacterAsset.cpp` だけで `CGLTF_IMPLEMENTATION` を定義する。

本プロジェクトのローダーは制作済みの単一メッシュ・11ボーン・単色マテリアルのGLBに対象を限定する。cgltf自体が対応する全機能をゲームが扱えるという意味ではない。
