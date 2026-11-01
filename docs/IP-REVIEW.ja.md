# 権利関係の調査メモ（著作権・特許・商標）

対象: deltarec 0.1.0（2026-11-01 時点）
性質: 開発時に行った調査の記録であり、法的助言ではありません。特許調査は網羅的ではなく、
Web 上で確認できた範囲です。商用利用や大規模配布の前に専門家の確認を受けることを勧めます。

## 1. 結論（要約）

- **著作権**: 本体コード（`src/*.hpp`, `src/main.cpp`, `tools/*.py`, CMake/VS プロジェクト）は
  このプロジェクトのために新規に書いたもので、他のソースコードの複製・翻案は含まない。
  同梱の外部コードは `third_party/dr_wav.h`, `third_party/dr_flac.h`（David Reid 作、
  public domain / MIT-0 のデュアルライセンス）のみ。いずれも MIT ライセンスでの再配布と両立する。
- **特許**: 使用しているアルゴリズムはすべて 1960〜90 年代に公刊された教科書的手法で、
  基本特許は存続期間（出願から 20 年）を過ぎている。PCM→1bit 変換を汎用的に独占する
  有効な特許は確認できなかった。
- **商標**: "DSD" / "Direct Stream Digital" / "SACD" はソニー等の商標。プロジェクト名には
  使わず、フォーマットを指す説明語としてのみ使用している（記述的使用）。

## 2. 著作権：コードの出所

| 部分 | 出所 | ライセンス |
|---|---|---|
| `src/ntf.hpp` NTF 設計 | 新規実装。Schreier & Temes の教科書に記載の一般手法（Lee の基準、Butterworth 極、最適零点）を自前で導出・実装 | MIT |
| `src/modulator.hpp` 誤差帰還形 1bit 変調器 | 新規実装。誤差帰還構造は Spang & Schultheiss (1962), Tewksbury & Hallock (1978) に遡る公知技術 | MIT |
| `src/resampler.hpp` ポリフェーズ FIR | 新規実装。Kaiser 窓 (1974)、ポリフェーズ補間 (Crochiere & Rabiner, 1983) | MIT |
| `src/dsdwriter.hpp` DSF/DFF 書き出し | 公開仕様書（Sony DSF Spec v1.01、Philips DSDIFF 1.5）に基づく新規実装 | MIT |
| `src/decoder.hpp`, `src/main.cpp` | 新規実装 | MIT |
| `third_party/dr_wav.h`, `dr_flac.h` | David Reid (mackron/dr_libs) | public domain または MIT-0（選択可） |
| `tools/verify_dsf.py` | 新規実装（numpy / scipy を利用、同梱せず） | MIT |

補足:

- NTF の最適零点位置（`optimalZeros()` の数表）は Gauss–Legendre 求積の節点＝Legendre 多項式の
  根という数学的事実で、開発時に数値最適化で独立に算出した。MATLAB Delta-Sigma Toolbox
  （Schreier、BSD ライセンス）や Python 移植 `deltasigma` のコードは参照・複製していない
  （開発環境で `deltasigma` の import を試みたが動作せず、一切利用していない）。
- 既存の PCM→DSD 実装（foobar2000 用 SACD プラグイン、SoX の dsf.c、MPD の DSF/DSDIFF
  デコーダなど GPL/LGPL のもの）のコードは参照していない。DSF/DSDIFF の実装は SoX (LGPL)、
  MPD (GPL)、TagLib (LGPL)、FFmpeg などが長年公開しており、フォーマット仕様の実装自体に
  ライセンス上の障害がないことの傍証になっている。

## 3. 特許

### 3.1 使用している要素技術と出典年

| 要素 | 初出 | 状況 |
|---|---|---|
| ΔΣ変調（1bit ノイズシェーピング） | 猪瀬・安田・村上 (1962) | 失効 |
| 誤差帰還形ノイズシェーパ | Spang & Schultheiss (1962), Tewksbury & Hallock (1978) | 失効 |
| 高次 ΔΣ の安定設計指針（Lee の基準 ‖NTF‖∞ ≈ 1.5） | Lee (1987, MIT 修士論文), Chao et al. (1990) | 失効 |
| NTF 零点の帯域内最適配置 | Schreier (1993) ほか教科書 | 公知の数学 |
| Butterworth 極配置の NTF | Schreier & Temes 教科書 (2005) 第 4 章 | 公知 |
| TPDF ディザ | Vanderkooy & Lipshitz (1984–87) | 失効 |
| Kaiser 窓 FIR 設計 | Kaiser (1974) | 公知 |
| 多段ポリフェーズ補間 | Crochiere & Rabiner (1981–83) | 失効 |
| 過負荷時の積分器リセット | 1980 年代から広く用いられる定石 | 公知 |

### 3.2 Web で確認した関連特許と本実装との関係

- **US 10,547,323**（D&M Holdings、優先日 2016、2037 年まで有効）— 1bit PDM ストリームを
  Return-to-Zero クロックで再整形してアナログに変換する **DAC 側ハードウェア**の特許。
  独立請求項は RTZ 変換と後段フィルタを要件とし、本ツール（ファイルを書くだけ）には該当しない。
- **US 6,255,975**（1bit オーディオ用ノイズフィルタ回路）、**US 7,136,420**（PCM レコーダ
  への 1bit 信号記録）、**US 8,154,430**（ソニー、デジタル記録・再生・変換装置）— いずれも
  2000 年前後の出願で、PCM→DSD の一般的な ΔΣ 変換そのものを請求するものではなく、
  かつ存続期間満了または満了間近。
- ソニー/フィリップスの SACD 関連基本特許（1990 年代後半出願）は 2020 年前後までに満了。
- Trellis ノイズシェーピング（Philips）、Super Bit Mapping Direct（ソニー）など、
  特定社の高度な変調手法は**使用していない**。
- HQPlayer (Signalyst)、KORG AudioGate、JRiver、foobar2000 プラグインなど、ソフトウェアによる
  PCM→DSD 変換は 2010 年代から多数の商用・OSS 製品が公開されており、汎用的な手法を独占する
  特許が行使されている形跡は見当たらない。

### 3.3 使っていない／避けている要素

- DST（DSD の可逆圧縮）— 実装せず。
- 他社製変調器の係数・トポロジ — 使用せず（係数は実行時に自前設計）。
- 特定製品固有のフィルタ特性（"apodizing" 等の商標名を含む）— 名称・特性とも使用せず。

## 4. 商標

- "DSD", "Direct Stream Digital", "Super Audio CD", "SACD" はソニーグループおよび
  関連会社の商標。README では技術・フォーマットを指す説明語として用い、
  製品名・リポジトリ名（deltarec）には含めていない。ロゴは使用していない。
- "AK4499", "SMSL" 等の DAC 名は互換性の説明に用いる記述的使用。

## 5. フォーマット仕様書の扱い

- DSF: ソニー「DSF File Format Specification v1.01」（dsd-guide.com 等で公開配布）。
  仕様書本文は同梱せず、必要な構造だけを独自にコードに記述。
- DSDIFF: フィリップス「DSDIFF 1.5」（現在は Sonic Studio が管理、PDF 公開）。
  同上。米国議会図書館の形式記述でも「技術的保護なし」とされている。

## 6. 配布時の注意

- `LICENSE`（MIT）と `third_party/` 各ヘッダ末尾のライセンス文をそのまま保つこと。
- バイナリ配布物にも README と LICENSE を同梱する（release.yml はそうしている）。
- 他人の市販 DSD 音源・PCM 音源を変換して**再配布**するのは本ツールの権利とは別の問題
  （音源の著作権）なので、利用者向けの注意書きとして README に一言添えてもよい。

## 参考 URL

- DSDIFF 形式の説明（米国議会図書館）: https://loc.gov/preservation/digital/formats/fdd/fdd000245.shtml
- DSF 仕様（SoX 開発 ML でのリンク）: https://mail-archive.com/sox-devel@lists.sourceforge.net/msg00424.html
- US 10,547,323: https://patents.google.com/patent/US10547323
- US 8,154,430: https://patents.google.com/patent/US8154430
- US 7,136,420: https://patents.google.com/patent/US7136420
- Delta Sigma Toolbox (MathWorks File Exchange): https://www.mathworks.com/matlabcentral/fileexchange/19-delta-sigma-toolbox
- dr_libs: https://github.com/mackron/dr_libs
