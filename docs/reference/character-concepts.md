# キャラクター設定画

作成日時: 2026-09-05 13:57
更新日時: 2026-09-05 15:28

## 成果物

参考画像の角張った輪郭、面ごとの陰影、簡潔な衣装を取り入れた男女各1体の設定画。内蔵 image_gen ツールで生成した。正面・側面・背面を並べ、将来のモデル制作の参考にする。

- [男性の設定画](../../assets/characters/concepts/male-fighter-v1.png)：白いノースリーブ道着、黒いパンツと帯、短い黒髪、裸足。
- [女性の設定画](../../assets/characters/concepts/female-fighter-v1.png)：赤い立ち襟の武術服、黒いパンツと帯、黒髪のポニーテール、平底の靴。

このページの成果物はPNG設定画。2026-09-05にこれをもとにBlenderで初期の男女モデル・共通骨格・アニメーションを制作し、ゲームへ組み込んだ。実際のモデルの三角形数、制作物と制約は[Phase 6資料](phase-6-blender.md)を参照する。設定画とモデルは別成果物で、共通の物理寸法に合わせて形状を調整している。

## 使用した参考画像

`docs/references/` はユーザーの指定でGitから除外したローカル資料。下記リンクは参考画像を配置した環境でのみ開ける。

- [近景の参考画像](../references/virtua-fighter-5-ultimate-showdowns-legendary-pack-dlc-inclu_bws5.1200.webp)
- [キャラクター開発資料の参考画像](../references/DKqvbfbX0AAD8bN.jpg)

## 生成プロンプト

参考画像2枚をスタイル参照として両方の生成に指定した。以下は使用したプロンプト全文。

### 男性

```text
Use case: stylized-concept. Asset type: male fighter character turnaround reference sheet for the polygon-fighter game, for future 3D modeling. Input images: both are visual style references only: early 1990s extremely low polygon arcade fighting characters, angular silhouette, broad flat color facets. Create one original adult male martial artist, athletic broad shoulders, short angular black hair, sleeveless off-white wrap training tunic, charcoal belt and loose charcoal training trousers, simple wrist wraps, bare feet. Strong readable geometric silhouette, chunky polygon fists, simple planar face. Match the reference's visibly sparse polygon geometry and hard flat shading, not modern smooth stylization. Composition: clean landscape sheet with three equal-scale full-body orthographic views of THE SAME character: front, left side, back, neutral relaxed A-pose with hands away from hips. Consistent clothes and anatomy across views. Head and feet entirely visible with generous margins. Plain warm light-gray background, neutral studio illumination, minimal ground shadow. No text, logos, watermarks, weapons, accessories, scenery, wireframe, photoreal textures, smooth rounded surfaces. This is a character design image, not a claim to supply a 3D mesh.
```

### 女性

```text
Use case: stylized-concept. Asset type: female fighter character turnaround reference sheet for polygon-fighter, for future 3D modeling. Input images: both are visual style references only: early 1990s extremely low polygon arcade fighters with angular silhouettes and broad flat colored planes. Create one original adult female martial artist, athletic practical proportions, angular black hair tied in one compact ponytail, sleeveless red high-collar martial arts tunic ending at upper thigh with modest side splits over full-length charcoal training trousers, black waist sash, simple black wrist wraps, flat black martial arts shoes. Distinct confident geometric face and readable strong silhouette. Match visibly sparse polygon geometry and hard flat shading from the references, not modern smooth stylization. Composition: clean landscape sheet with three equal-scale full-body orthographic views of THE SAME character: front, left side, back, neutral relaxed A-pose hands away from hips. Consistent anatomy and clothing across views. Head and feet fully visible with generous margins. Plain warm light-gray background, neutral studio illumination and minimal ground shadow. Same visual presentation as a male fighter modeling sheet: simple planar materials, strong facet shading, minimal texture. No text, logos, watermarks, weapons, jewelry, scenery, wireframe, photoreal textures or smooth rounded surfaces. This is a character design image, not a 3D mesh.
```
