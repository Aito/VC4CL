# VC4CL (VideoCore IV OpenCL) - Debian Trixie (Kernel 6.x) 環境構築ガイド

このドキュメントは、最新の Raspberry Pi OS (Debian Bookworm / Trixie 等、Kernel 6.x 系) において、VC4CL をビルド・構築するための公式な手順書です。

## 1. 背景と新しいアーキテクチャ
以前の VC4CL は Broadcom のプロプライエタリなレガシーライブラリ (`libbcm_host.so`, `libvcsm.so`) に依存していましたが、これらは最新の Linux 環境から完全に削除されています。

標準 Linux DRM (`vc4_drm`) ドライバを利用するアプローチも検証されましたが、カーネルのセキュリティ仕様により **VPM DMA (メモリ書き込み) がドライバレベルで完全にブロックされる** ため、Compute Shader の実行が不可能であることが判明しました。

最終的に VC4CL は、外部ライブラリに一切依存せず、最新カーネルに標準搭載されている **VCSM-CMA (`/dev/vcsm-cma`) への直接 IOCTL 発行によるメモリ確保** と、**Mailbox (`/dev/vcio`) による QPU ベアメタル実行** を自前で実装するアーキテクチャへと進化し、最新環境への完全対応を果たしました。

---

## 2. 実機側のシステム要件と設定 (超重要)

- **対象ハードウェア:** Raspberry Pi 1 / 2 / 3 / Zero (VideoCore IV 搭載機)
- **対象 OS:** Raspberry Pi OS (Debian Bookworm または Trixie / Kernel 6.x)

### ⚠️ KMS ドライバとの割り込み競合問題
モダンな OS でデフォルト採用されている Wayland デスクトップは、`vc4-kms-v3d` (KMSディスプレイドライバ) に依存しています。しかし、このドライバを有効にすると **V3D のハードウェア割り込み (IRQ) をカーネルが横取り** してしまい、VC4CL (Mailbox) 側で計算完了を検知できず 30 秒のタイムアウトを引き起こします。

VC4CL を高速かつ正常に動作させるには、**X11 (フレームバッファモード) または ヘッドレス環境** への切り替えが必須です。

#### 設定手順 (X11 デスクトップを維持する場合)
1. `sudo raspi-config` を実行。
2. `Advanced Options` -> `Wayland` から **`X11`** を選択。
3. `/boot/firmware/config.txt` (または `/boot/config.txt`) を開き、KMSオーバーレイをコメントアウトします。
   ```text
   #dtoverlay=vc4-kms-v3d
   ```
4. 保存後、Raspberry Pi を再起動 (`sudo reboot`) します。

---

## 3. 必要な開発パッケージのインストール

ビルドに必要なコンパイラ、CMake、および OpenCL 関連のヘッダ (ICD ローダー含む) をインストールします。

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    opencl-headers \
    ocl-icd-dev \
    ocl-icd-opencl-dev \
    clinfo
```

---

## 4. VC4CL (ランタイム) のビルドとインストール

```bash
# 1. クローン
git clone https://github.com/doe300/VC4CL.git
cd VC4CL

# 2. CMake 構成 (BUILD_ICD=ON が必須)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_ICD=ON

# 3. ビルド
cmake --build build -j$(nproc)

# 4. 単体テストの実行 (全パスすることを確認)
cd build
sudo ctest --output-on-failure

# 5. インストール
sudo cmake --install .

# 6. OpenCL システムへの登録 (ICD)
sudo mkdir -p /etc/OpenCL/vendors
echo "/usr/local/lib/libVC4CL.so" | sudo tee /etc/OpenCL/vendors/VC4CL.icd
sudo ldconfig
```

---

## 5. 動作確認

`clinfo` コマンドを実行し、`VideoCore IV GPU` が認識されることを確認します。
*(※ `/dev/vcio` デバイスへアクセスするため、ユーザー権限によっては `sudo` が必要です)*

```bash
sudo clinfo
```

正常に構成されていれば、以下のようにプラットフォームとデバイス情報が出力されます。

```text
Number of platforms                               1
  Platform Name                                   OpenCL for the Raspberry Pi VideoCore IV GPU
...
Number of devices                                 1
  Device Name                                     VideoCore IV GPU
...
```

---

## 6. 次のステップ: VC4C コンパイラの導入

ここまでの手順で構築された `VC4CL` は、OpenCL の **「ランタイム（実行基盤）」** です。
OpenCL アプリケーション内でカーネルのソースコード (OpenCL C 言語) をビルドするには、実行時に翻訳を行うためのコンパイラである **[VC4C](https://github.com/doe300/VC4C)** がシステムにインストールされている必要があります。

VC4C をシステムに導入することで、`clinfo` の出力が `Compiler Available: Yes` に変わり、完全な OpenCL 開発環境が整います。
