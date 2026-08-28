# logue-sdk 

[English](./README.md)

このリポジトリには [prologue](https://www.korg.com/jp/products/synthesizers/prologue/), [minilogue xd](https://www.korg.com/jp/products/synthesizers/minilogue_xd/), [Nu:Tekt NTS-1 digital kit](https://www.korg.com/jp/products/dj/nts_1/), [Nu:Tekt NTS-1 digital kit mkII](https://www.korg.com/jp/products/synthesizers/nts_1_mk2), [microKORG2](https://www.korg.com/products/synthesizers/microkorg2) synthesizers, the [Nu:Tekt NTS-3 kaoss pad kit](https://www.korg.com/jp/products/dj/nts_3), and [drumlogue](https://www.korg.com/jp/products/drums/drumlogue/) の6製品で使用できる自作オシレーターやエフェクトのビルドに必要なファイルが全て揃っています.

このリポジトリは KORG 公式の [logue-sdk](https://github.com/korginc/logue-sdk) の個人フォークであり、独自に開発された非公式のシンセ/エフェクトユニット（例: [platform/drumlogue/](platform/drumlogue/) 以下の `OmniPress`, `NeonLabirinto`, `LuceAlNeon`, `ScrutaAstri`, `EffeESP32`, `EffeMD`, `PortaCassette`, `Brachetti`, `delay_tribal` など）も併せて公開しています。**これらをダウンロード、ビルド、インストール、使用する前に、必ず下記の[免責事項](#免責事項)をお読みください。**

## 免責事項

本リポジトリで配布されているカスタムシンセ/エフェクトユニットは、**非公式かつ独自に開発された第三者制作のもの**です。KORG株式会社、またはその他本リポジトリに記載・示唆されるハードウェアメーカーによって作成・レビュー・検証・認証・推奨・サポートされたものではありません。「KORG」「drumlogue」「prologue」「minilogue」「Nu:Tekt」「NTS-1」「NTS-3」「microKORG」等の製品名・プラットフォーム名は各権利者の商標であり、対応ハードウェアを示すためにのみ使用しています。提携、後援、推奨関係を意味するものではありません。

これらのユニットは実験的な個人制作ソフトウェアとして無償で配布されており、**動作の正確性、安定性、音質、安全性、また過去・現在・将来のファームウェアやハードウェアとの互換性について、いかなる保証もありません**。品質保証された完成品の商用製品ではありません。

本リポジトリのユニット、ソースコード、ツールをダウンロード、ビルド、インストール、または使用することにより、以下に同意したものとみなされます。

* すべては**「現状有姿(AS IS)」「入手可能な限り(AS AVAILABLE)」で、いかなる保証もなく**提供されます。商品性、特定目的への適合性、権原、第三者権利の非侵害についての黙示の保証を含め、明示・黙示を問わずいかなる保証もありません。
* **利用は完全に自己責任で行うものとします。** ご自身のハードウェアに第三者制作のユニットファイルをインストール・実行する前に、そのリスクを評価する責任はご自身にあります。
* これらのユニットの開発者、本リポジトリの管理者、およびこれらを配布・ミラー・ホストする第三者（以下「開発者および配布者」）は、本ソフトウェアのダウンロード、ビルド、インストール、使用（または使用不能）に起因または関連して生じた、損傷、故障、欠陥、データ損失、ハードウェアの破損・劣化、予期しない音声出力による聴覚被害その他の傷害、時間的損失、収益損失、その他直接・間接・付随的・特別・懲罰的・結果的損害について、それがあらかじめ通知されていた場合であっても、また契約・不法行為・過失その他いかなる法的根拠に基づく主張であっても、一切の責任を負いません。
* 本コードの公開は、公式の技術サポート、保守、バグ修正の約束を意味しません。開発者による対応があったとしても、それは純粋にベストエフォート・任意ベースのものであり、KORGその他のハードウェアメーカーによる公式サポートに代わるものではありません。
* 本リポジトリ内のユニット、ソースファイル、ドキュメントは、事前の通知なく変更・改名・移動・削除される場合があります。
* 準拠法上、特定の保証や責任の除外・制限が認められない場合、上記の除外・制限はその法律が許容する最大限の範囲でのみ適用されます。

上記の条件に同意されない場合は、本リポジトリで公開されているユニットまたはコードのダウンロード、ビルド、インストール、使用を行わないでください。

## ニュース

[websim](websim/) に、NTS-1 mkii と NTS-3 向けの新しいWebベースのシミュレーターが追加されました. 
Webブラウザ内でDSPコードの開発、テスト、デバッグができ、開発サイクルを大幅に高速化できます.
使い方は [websim/README.md](websim/README.md) を参照してください.

## まずは使ってみよう

既に公開されているオシレーターやエフェクトの情報は [Unit Index](https://korginc.github.io/logue-sdk/ja/unit-index/) にあります.
具体的な入手方法については各デベロッパーのウェブサイトにてご確認下さい.
[logue-SDK-filter](https://logue-sdk.vercel.app/) という、より検索しやすいユニットインデックスページもあります.

## プラットフォームと互換性に関して

| 製品                           | SDK    | ファームウエア | プロセッサー  | ユニットフォーマット                                        |
|--------------------------------|--------|----------------|---------------|-------------------------------------------------------------|
| prologue                       | v1.1.0 | >= v2.00       | ARM Cortex-M4 | Custom 32-bit LSB executable, ARM, EABI5 v1 (SYSV), static  |
| minilogue-xd                   | v1.1.0 | >= v2.00       | ARM Cortex-M4 | Custom 32-bit LSB executable, ARM, EABI5 v1 (SYSV), static  |
| Nu:Tekt NTS-1 digital kit      | v1.1.0 | >= v1.02       | ARM Cortex-M4 | Custom 32-bit LSB executable, ARM, EABI5 v1 (SYSV), static  |
| drumlogue                      | v2.0.0 | >= v1.0.0      | ARM Cortex-A7 | ELF 32-bit LSB shared object, ARM, EABI5 v1 (SYSV), dynamic |
| Nu:Tekt NTS-1 digital kit mkII | v2.0.0 | >= v1.0.0      | ARM Cortex-M7 | ELF 32-bit LSB shared object, ARM, EABI5 v1 (SYSV), dynamic |
| Nu:Tekt NTS-3 kaoss pad kit    | v2.0.0 | >= v1.0.0      | ARM Cortex-M7 | ELF 32-bit LSB shared object, ARM, EABI5 v1 (SYSV), dynamic |
| microKORG2                     | v2.1.0 | >= v2.0.0      | ARM Cortex-A7 | ELF 32-bit LSB shared object, ARM, EABI5 v1 (SYSV), dynamic |

#### バイナリ互換性について

prologue, minilogue xd, Nu:Tekt NTS-1 dgital kitの3製品のために作成されたユニットは, SDKのバージョンが一致する限りバイナリレベルで互換性があります. しかし各製品へのユニットの最適化が推奨されますので、可能であれば各プラットフォームの専用のビルドを優先してください.

#### リポジトリー構造:
* [platform/prologue/](platform/prologue/) : *prologue*専用のファイル, テンプレートとデモプロジェクト
* [platform/minilogue-xd/](platform/minilogue-xd/) : *minilogue xd*専用のファイル, テンプレートとデモプロジェクト
* [platform/nutekt-digital/](platform/nutekt-digital/) : *Nu:Tekt NTS-1 digital kit*専用のファイル, テンプレートとデモプロジェクト
* [platform/drumlogue/](platform/drumlogue/) : *drumlogue*専用のファイルとテンプレート
* [platform/nts-1_mkii/](platform/nts-1_mkii/) : *Nu:Tekt NTS-1 digital kit mkII*専用のファイル, テンプレートとデモプロジェクト
* [platform/nts-3_kaoss/](platform/nts-3_kaoss/) : *Nu:Tekt NTS-3 kaoss pad kit*専用のファイル, テンプレートとデモプロジェクト
* [platform/microkorg2/](platform/microkorg2/) : *microKORG2*専用のファイル, テンプレートとデモプロジェクト
* [platform/ext/](platform/ext/) : 外部依存ファイルとサブモジュール
* [docker/](docker/) : ホストOSに依存せずあらゆるプラットフォーム向けのプロジェクトを構築するためのdocker containerのソース
* [tools/](tools/) : プロジェクトのビルド、またはビルド成果物の操作に必要なツールとドキュメント. dockerを使用する場合は必要ありません
* [devboards/](devboards/) : 限定配布された開発ボードに関する情報やファイル

## 自作コンテンツを共有する

自作のオシレーターやエフェクトをKORGチームに紹介して下さい.
連絡先は *logue-sdk@korg.co.jp* です.

## サポート

KORGはlogue-sdkに関しての技術的なサポートを提供しません.



