# GitHub への公開・リリース手順

## 1. 初回公開

GitHub で空のリポジトリ（例: `yourname/deltarec`）を作ってから：

```sh
cd deltarec
git init -b main          # zip 版にはすでに .git が入っているので不要な場合あり
git add .
git commit -m "Initial release of deltarec 0.1.0"
git remote add origin https://github.com/yourname/deltarec.git
git push -u origin main
```

push すると `.github/workflows/build.yml` が走り、Windows / macOS / Linux の
3 環境でビルドとスモークテストが行われます（Actions タブで確認）。

## 2. リリースを切る

バージョンを上げるときは次の 3 箇所を合わせます：

- `src/main.cpp` の `kVersion`
- `CMakeLists.txt` の `project(... VERSION x.y.z)`
- `CHANGELOG.md` に `## [x.y.z] - YYYY-MM-DD` の節を追加

そしてタグを push します：

```sh
git tag -a v0.1.0 -m "deltarec 0.1.0"
git push origin v0.1.0
```

`.github/workflows/release.yml` が起動し、

- `deltarec-v0.1.0-windows-x64.zip`（MSVC ビルドの exe）
- `deltarec-v0.1.0-macos-universal.tar.gz`（arm64 + x86_64 のユニバーサルバイナリ）
- `deltarec-v0.1.0-linux-x64.tar.gz`（libstdc++ 静的リンク）

を作り、`CHANGELOG.md` の該当節を本文にして GitHub Release を自動作成します。
各アーカイブには README・LICENSE・`tools/verify_dsf.py` も同梱されます。

Actions の「Workflow permissions」が「Read and write」になっていないと
Release 作成で失敗するので、リポジトリの Settings → Actions → General で確認してください
（ワークフロー側でも `permissions: contents: write` を宣言しています）。

## 3. 手元でビルドして添付したい場合

Windows: `vs\deltarec.sln` を `Release | x64` でビルド → `vs\bin\x64\Release\deltarec.exe`
を zip にして Release ページの「Attach binaries」からアップロードできます。
Windows Defender SmartScreen の警告を避けたい場合はコード署名が必要ですが、
個人配布では未署名のまま配るのが一般的です。

## 4. リポジトリ設定のおすすめ

- About 欄: "Offline PCM (WAV/FLAC) to 1-bit DSD (DSF/DFF) converter — delta-sigma modulation done in software"
- Topics: `dsd`, `dsf`, `delta-sigma`, `audio`, `flac`, `wav`, `cpp17`, `cmake`, `sacd`
- License は MIT として自動認識されます。
