## BedrockOnLinux で Vector を動かす場合

このブランチは、Linux 上の Minecraft Bedrock で Vector クライアントを動かすための WineGDK 修正版です。

- **`d2d1`**: Vector の描画に必要なコマンドリスト画像、各種エフェクト、カラーコンテキストに対応しました。多重描画の同期や範囲計算も手を入れています。ワールド全体が真っ黒になる問題は直っていますが、NameTags のちらつきはまだ調査中です。
- **`winewayland.drv`**: 新しめの Wayland 環境（Hyprland など）でポインタやクリップボードのイベントを取りこぼして落ちる問題を直しました。また、GDK-Proton 側の `win32u.so` とやり取りする内部番号を合わせています。これで Super+ドラッグでウィンドウサイズを変えてもクラッシュしなくなりました。

以下は **BedrockOnLinux が使う GDK-Proton** に修正済みバイナリを組み込む手順です。システム全体の Wine を書き換えるものではありません。元の WineGDK の説明は後半に残してあります。

### 1. 準備と環境の確認

ビルドには一般的な C コンパイラ、GNU make、Wayland の開発用ヘッダー、32bit 向けツールチェーン（32bit DLL もビルドする場合）が必要です。具体的なパッケージ名はディストリビューションごとに異なるので、下にある元の README や Wine 公式の案内を見て入れておいてください。

**【重要】Wayland ドライバの互換性について**
`winewayland.so` は Wine 本体の奥深く（`win32u.so`）と強い結びつきがあります。導入先 GDK-Proton のベースとなった Wine ソースと一致していないと、シンボル不足や内部番号の食い違いでクラッシュします。よく分からない場合は `d2d1.dll` だけを導入し、Wayland ドライバの差し替えは見送るのが無難です。

まず、この README がある WineGDK のルートディレクトリで変数を設定します（bash で実行してください）。

```bash
set -euo pipefail
SRC="$(pwd)"
BOL_DATA="${XDG_DATA_HOME:-$HOME/.local/share}/bedrock-on-linux"
PROTON_DIR="$BOL_DATA/proton/<使用中のGDK-Protonディレクトリ名>"
BUILD64="$HOME/build/winegdk-vector-win64"
BUILD32="$HOME/build/winegdk-vector-win32"
PREFIX="$BOL_DATA/compatdata/pfx"

test -f "$SRC/configure"
test -f "$PROTON_DIR/files/lib/wine/x86_64-unix/win32u.so"
```

`<使用中のGDK-Protonディレクトリ名>` は、実際にランチャーで選んでいるディレクトリ名（例: `GDK-Proton-xuser` など）に変えてください。

### 2. ソース外で必要なモジュールをビルドする

ソースディレクトリを汚さないよう、別の場所で configure して必要な DLL だけビルドします。

```bash
mkdir -p "$BUILD64" "$BUILD32"
(cd "$BUILD64" && "$SRC/configure" --enable-win64 --disable-tests)
(cd "$BUILD32" && "$SRC/configure" --disable-tests)

make -C "$BUILD64" -j"$(nproc)" dlls/d2d1/all dlls/winewayland.drv/all
make -C "$BUILD32" -j"$(nproc)" dlls/d2d1/all
```

ビルドされるファイルは次の通りです。
- 64bit D2D: `"$BUILD64/dlls/d2d1/x86_64-windows/d2d1.dll"`
- 32bit D2D: `"$BUILD32/dlls/d2d1/i386-windows/d2d1.dll"`
- Wayland ドライバ: `"$BUILD64/dlls/winewayland.drv/winewayland.so"`

※32bit ツールチェーンがなくて 32bit 側がコケる場合は、32bit の手順は飛ばして 64bit のみ導入してください。

ビルドした Wayland ドライバを使う場合は、導入先とリンクできるか事前に確認しておきます（エラーが出なければ OK です）。

```bash
LD_LIBRARY_PATH="$PROTON_DIR/files/lib/wine/x86_64-unix" \
    ldd -r "$BUILD64/dlls/winewayland.drv/winewayland.so"
```

### 3. バックアップを取って配置する

**必ずゲームを終了した状態で作業してください。**
後から元に戻せるよう、タイムスタンプ付きのディレクトリに元ファイルを退避してから上書きします。

```bash
WINE_LIB="$PROTON_DIR/files/lib/wine"
BACKUP_DIR="$HOME/winegdk-vector-backup-$(date +%Y%m%d-%H%M%S)"
PREFIX_DLL64="$PREFIX/drive_c/windows/system32/d2d1.dll"
PREFIX_DLL32="$PREFIX/drive_c/windows/syswow64/d2d1.dll"

# 元ファイルの存在確認
test -f "$BUILD64/dlls/d2d1/x86_64-windows/d2d1.dll"
test -f "$WINE_LIB/x86_64-windows/d2d1.dll"

mkdir "$BACKUP_DIR"

# 64bit D2D をバックアップ & 上書き
cp -p "$WINE_LIB/x86_64-windows/d2d1.dll" "$BACKUP_DIR/d2d1-x86_64.dll"
cp -p "$BUILD64/dlls/d2d1/x86_64-windows/d2d1.dll" "$WINE_LIB/x86_64-windows/d2d1.dll"

# prefix 側にも実体があれば同期
if test -f "$PREFIX_DLL64"; then
    cp -p "$PREFIX_DLL64" "$BACKUP_DIR/prefix-d2d1-x86_64.dll"
    cp -p "$BUILD64/dlls/d2d1/x86_64-windows/d2d1.dll" "$PREFIX_DLL64"
fi

# 32bit D2D（ビルドした場合のみ）
if test -f "$BUILD32/dlls/d2d1/i386-windows/d2d1.dll" && test -f "$WINE_LIB/i386-windows/d2d1.dll"; then
    cp -p "$WINE_LIB/i386-windows/d2d1.dll" "$BACKUP_DIR/d2d1-i386.dll"
    cp -p "$BUILD32/dlls/d2d1/i386-windows/d2d1.dll" "$WINE_LIB/i386-windows/d2d1.dll"
    if test -f "$PREFIX_DLL32"; then
        cp -p "$PREFIX_DLL32" "$BACKUP_DIR/prefix-d2d1-i386.dll"
        cp -p "$BUILD32/dlls/d2d1/i386-windows/d2d1.dll" "$PREFIX_DLL32"
    fi
fi

# Wayland ドライバ（互換性を確認できた場合のみ）
if test -f "$BUILD64/dlls/winewayland.drv/winewayland.so" && test -f "$WINE_LIB/x86_64-unix/winewayland.so"; then
    cp -p "$WINE_LIB/x86_64-unix/winewayland.so" "$BACKUP_DIR/winewayland.so"
    cp -p "$BUILD64/dlls/winewayland.drv/winewayland.so" "$WINE_LIB/x86_64-unix/winewayland.so"
fi
```

### 元に戻したいとき

もし動作がおかしくなった場合は、ゲームを閉じてからバックアップしたファイルを元の位置へ書き戻してください。

```bash
cp -p "$BACKUP_DIR/d2d1-x86_64.dll" "$WINE_LIB/x86_64-windows/d2d1.dll"
if test -f "$BACKUP_DIR/d2d1-i386.dll"; then
    cp -p "$BACKUP_DIR/d2d1-i386.dll" "$WINE_LIB/i386-windows/d2d1.dll"
fi
if test -f "$BACKUP_DIR/prefix-d2d1-x86_64.dll"; then
    cp -p "$BACKUP_DIR/prefix-d2d1-x86_64.dll" "$PREFIX_DLL64"
fi
if test -f "$BACKUP_DIR/prefix-d2d1-i386.dll"; then
    cp -p "$BACKUP_DIR/prefix-d2d1-i386.dll" "$PREFIX_DLL32"
fi
if test -f "$BACKUP_DIR/winewayland.so"; then
    cp -p "$BACKUP_DIR/winewayland.so" "$WINE_LIB/x86_64-unix/winewayland.so"
fi
```

# NOTES FOR PEOPLE TRYING TO RUN MINECRAFT'S GDK BUILD

Microsoft Services is WIP.

As of [3414250](https://github.com/Weather-OS/WineGDK/commit/341425050f4f9b968b807dbd61942dabca8f6af1), Online functionality has been implemented. To get it working, resort to [GDK-Proton](https://github.com/Weather-OS/GDK-Proton)

### NOTES ABOUT THIS PROJECT

Unfortunately, since I don't have the right conditions to be able to   
push my changes upstream, I've decided to declare every part of my contributions that isn't    
derived from other parts of the wine project, CC0 (A.K.A "Public Domain") (i.e xgameruntime).
**What this means**:  
You're allowed to derive, redistribute and reimplement my code at will,  
without any attributions.
**THIS ONLY APPLIES TO THE CODE I HAVE WRITTEN, NOT THE REST OF WINE'S PROJECT!**

**ADDITIONAL NOTES**: 
- Code authored by "Olivia Ryan" is not covered by this clause.
- [Xodus](<https://github.com/xodus-gaming/xodus>) interopability is not upstream safe. Please refrain from pushing changes that include any part of this feature upstream.
  - This includes all code that run within the `xodus` wine debug channel.

## INTRODUCTION

Wine is a program which allows running Microsoft Windows programs
(including DOS, Windows 3.x, Win32, and Win64 executables) on Unix.
It consists of a program loader which loads and executes a Microsoft
Windows binary, and a library (called Winelib) that implements Windows
API calls using their Unix, X11 or Mac equivalents.  The library may also
be used for porting Windows code into native Unix executables.

Wine is free software, released under the GNU LGPL; see the file
LICENSE for the details.


## QUICK START

From the top-level directory of the Wine source (which contains this file),
run:

```
./configure
make
```

Then either install Wine:

```
make install
```

Or run Wine directly from the build directory:

```
./wine notepad
```

Run programs as `wine program`. For more information and problem
resolution, read the rest of this file, the Wine man page, and
especially the wealth of information found at https://www.winehq.org.


## REQUIREMENTS

To compile and run Wine, you must have one of the following:

- Linux version 2.6.22 or later
- FreeBSD 12.4 or later
- Solaris x86 9 or later
- NetBSD-current
- macOS 10.15 or later

As Wine requires kernel-level thread support to run, only the operating
systems mentioned above are supported.  Other operating systems which
support kernel threads may be supported in the future.

**FreeBSD info**:
  See https://wiki.freebsd.org/Wine for more information.

**Solaris info**:
  You will most likely need to build Wine with the GNU toolchain
  (gcc, gas, etc.). Warning : installing gas does *not* ensure that it
  will be used by gcc. Recompiling gcc after installing gas or
  symlinking cc, as and ld to the gnu tools is said to be necessary.

**NetBSD info**:
  Make sure you have the USER_LDT, SYSVSHM, SYSVSEM, and SYSVMSG options
  turned on in your kernel.

**macOS info**:
  You need Xcode/Xcode Command Line Tools or Apple cctools.

**Supported file systems**:
  Wine should run on most file systems. A few compatibility problems
  have also been reported using files accessed through Samba. Also,
  NTFS does not provide all the file system features needed by some
  applications.  Using a native Unix file system is recommended.

**Basic requirements**:
  You need to have the X11 development include files installed
  (called xorg-dev in Debian and libX11-devel in Red Hat).
  Of course you also need make (most likely GNU make).
  You also need flex version 2.5.33 or later and bison.

**Optional support libraries**:
  Configure will display notices when optional libraries are not found
  on your system. See https://gitlab.winehq.org/wine/wine/-/wikis/Building-Wine
  for hints about the packages you should install. On 64-bit
  platforms, you have to make sure to install the 32-bit versions of
  these libraries.


## COMPILATION

To build Wine, do:

```
./configure
make
```

This will build the program "wine" and numerous support libraries/binaries.
The program "wine" will load and run Windows executables.
The library "libwine" ("Winelib") can be used to compile and link
Windows source code under Unix.

To see compile configuration options, do `./configure --help`.

For more information, see https://gitlab.winehq.org/wine/wine/-/wikis/Building-Wine


## SETUP

Once Wine has been built correctly, you can do `make install`; this
will install the wine executable and libraries, the Wine man page, and
other needed files.

Don't forget to uninstall any conflicting previous Wine installation
first.  Try either `dpkg -r wine` or `rpm -e wine` or `make uninstall`
before installing.

Once installed, you can run the `winecfg` configuration tool. See the
Support area at https://www.winehq.org/ for configuration hints.


## RUNNING PROGRAMS

When invoking Wine, you may specify the entire path to the executable,
or a filename only.

For example, to run Notepad:

```
wine notepad            (using the search Path as specified in
wine notepad.exe         the registry to locate the file)

wine c:\\windows\\notepad.exe      (using DOS filename syntax)

wine ~/.wine/drive_c/windows/notepad.exe  (using Unix filename syntax)

wine notepad.exe readme.txt          (calling program with parameters)
```

Wine is not perfect, so some programs may crash. If that happens you
will get a crash log that you should attach to your report when filing
a bug.


## GETTING MORE INFORMATION

- **WWW**: A great deal of information about Wine is available from WineHQ at
	https://www.winehq.org/ : various Wine Guides, application database,
	bug tracking. This is probably the best starting point.

- **FAQ**: The Wine FAQ is located at https://gitlab.winehq.org/wine/wine/-/wikis/FAQ

- **Wiki**: The Wine Wiki is located at https://gitlab.winehq.org/wine/wine/-/wikis/

- **Gitlab**: Wine development is hosted at https://gitlab.winehq.org

- **Mailing lists**:
	There are several mailing lists for Wine users and developers; see
	https://gitlab.winehq.org/wine/wine/-/wikis/Forums for more
	information.

- **Bugs**: Report bugs to Wine Bugzilla at https://bugs.winehq.org
	Please search the bugzilla database to check whether your
	problem is already known or fixed before posting a bug report.

- **IRC**: Online help is available at channel `#WineHQ` on irc.libera.chat.
