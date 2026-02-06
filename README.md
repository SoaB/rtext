# rtext - Raylib Advanced Text Renderer

**rtext** 是一個專為 [Raylib](https://www.raylib.com/) 設計的輕量級進階文字渲染模組。它基於 `stb_truetype`，特別針對 **大量文字（如 RPG/AVG 對話）** 與 **CJK（中日韓）字元** 進行了優化。

本模組解決了 Raylib 原生文字繪製在處理大量動態中文字時的效能瓶頸，並提供了豐富的樣式功能（描邊、陰影、背景框、打字機效果）。

[![o_O](rTextRander.mp4)](https://github.com/SoaB/soab.github.io)

## ✨ 主要功能

* **高效能渲染**：
* 內建 **Hash Map (雜湊表)** 緩存機制，將字形查找時間從  降至 。
* **Auto-Flush (自動清空)** 機制：當快取或圖集滿時自動重置，防止記憶體溢出或文字消失。


* **Rich Text (富文本) 支援**：支援 `[color=red]文字[/color]` 標籤，可在一行文字中混合多種顏色。
* **完整樣式控制**：
* **描邊 (Outline)** 與 **陰影 (Shadow)**。
* **背景框 (Background)**：支援「逐行背景」與「全段落全域背景」。
* **對齊方式**：左對齊、置中、右對齊。


* **打字機效果**：內建邏輯支援逐字顯示，並正確處理富文本標籤（不會顯示標籤代碼）。
* **自動換行**：設定最大寬度後自動折行。

---

## 🚀 快速開始

### 整合方式

將 `rtext.c`, `rtext.h` 以及 `stb_truetype.h` 加入你的專案中。

### 範例程式碼

```c
#include "raylib.h"
#include "rtext.h"

int main() {
    InitWindow(800, 600, "rtext Demo");
    
    // 1. 初始化 (載入字型檔，設定字號)
    InitAdvText("assets/font.ttf", 24);

    // 2. 設定樣式
    AdvTextStyle style = {
        .baseColor = WHITE,
        .align = TEXT_ALIGN_LEFT,
        .enableOutline = true,
        .outlineColor = BLACK
    };

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(GRAY);

        // 3. 繪製文字 (支援顏色標籤)
        DrawRichTextStyled("Hello [color=yellow]World[/color]!", (Vector2){10, 10}, -1, style);

        EndDrawing();
    }

    // 4. 釋放資源
    UnloadAdvText();
    CloseWindow();
    return 0;
}

```

---

## ⚙️ 參數詳解 (API Documentation)

### 1. `AdvTextStyle` 結構體

這是控制文字外觀的核心結構。

| 參數名稱 | 類型 | 說明 | 預設建議值 |
| --- | --- | --- | --- |
| `baseColor` | `Color` | 文字的預設顏色。 | `WHITE` |
| `align` | `Enum` | `TEXT_ALIGN_LEFT`, `TEXT_ALIGN_CENTER`, `TEXT_ALIGN_RIGHT`。 | `LEFT` |
| `maxWidth` | `float` | 文字最大寬度。超過此寬度會自動換行。設為 `0` 表示不限制（不換行）。 | `0` 或視UI而定 |
| `lineSpacing` | `float` | 行距倍數。`1.0` 為標準行高，`1.5` 為 1.5 倍行高。 | `1.0f` |
| **背景設定** |  |  |  |
| `enableBackground` | `bool` | 是否啟用背景框繪製。 | `false` |
| `enableGlobalBackground` | `bool` | `true`: 繪製一個包覆整段文字的大框。<br>

<br>`false`: 為每一行文字單獨繪製背景條。 | `false` |
| `backgroundColor` | `Color` | 背景框的顏色。 | `BLANK` |
| `bgPaddingX` | `float` | 背景左右的內距 (Padding)。 | `8.0f` |
| `bgPaddingY` | `float` | 背景上下的內距 (Padding)。 | `6.0f` |
| **裝飾設定** |  |  |  |
| `enableShadow` | `bool` | 是否啟用陰影。 | `false` |
| `shadowColor` | `Color` | 陰影顏色。 | `BLACK` |
| `shadowOffset` | `Vector2` | 陰影偏移量 `(x, y)`。 | `{2, 2}` |
| `enableOutline` | `bool` | 是否啟用描邊。 | `false` |
| `outlineColor` | `Color` | 描邊顏色。 | `BLACK` |
| `outlineThickness` | `float` | 描邊厚度（像素）。 | `1.0f` |

### 2. `DrawRichTextStyled` 函數

```c
void DrawRichTextStyled(const char* text, Vector2 pos, int charLimit, AdvTextStyle style);

```

* **text**: 要顯示的字串，支援 `[color=xxx]` 標籤。
* **pos**: 繪製的起始座標 (x, y)。如果是 Center/Right 對齊，這是基準點。
* **charLimit**: 限制顯示的字元數量（用於打字機效果）。
* 傳入 `-1` 表示顯示全部文字。
* 傳入 `0` 或正整數表示只顯示前 N 個字。


* **style**: 上述的樣式結構。

### 3. `Typewriter` 與 `UpdateTypewriter`

用於計算 `charLimit`。

* `Typewriter.speed`: 每秒顯示幾個字。
* `Typewriter.elapsed`: 內部計時器（初始化設為 0）。
* `Typewriter.isFinished`: 是否播放完畢的旗標。
* **注意**: `UpdateTypewriter` 會自動過濾掉 `[color]` 標籤，確保打字節奏是依照「可見字元」計算的。

---

## 🎨 富文本標籤 (Rich Text Tags)

目前支援簡單的顏色標籤。標籤不區分大小寫。

* **語法**: `[color=顏色名稱]文字內容[/color]`
* **支援的顏色名稱**:
* `red`, `green`, `blue`, `yellow`, `purple`, `orange`
* `darkblue`, `darkgray`, `maroon`, `white`, `black`, `gray`


* **範例**:
```
這是一段[color=red]紅色警告[/color]和[color=blue]藍色提示[/color]。

```



---

## ⚠️ 限制與效能配置 (Limitations & Config)

### 1. 硬體限制與配置 (`rtext.c` 宏定義)

為了支援大量中文字，本模組使用了動態紋理圖集 (Texture Atlas)。請根據目標平台調整 `rtext.c` 頂部的定義：

* **`MAX_GLYPHS` (預設 4096)**:
* 決定了快取能存多少個不同的字元。
* **限制**: 如果畫面同時間顯示超過 4096 個**不重複**的字，會頻繁觸發 Flush，導致掉幀。
* **建議**: 一般中文遊戲對話 4096 足夠；若為純文字閱讀器可增至 8192。


* **`ATLAS_SIZE` (預設 2048)**:
* 生成的字型紋理大小 (2048x2048)。
* **限制**: 較老的顯示卡可能不支援超過 4096 或 8192 的紋理。
* **影響**: 越小越容易滿，滿了會觸發重繪 (Flush)。



### 2. 功能限制

1. **單一字型**: `InitAdvText` 只能載入一個 `.ttf`。若需要粗體 (`Bold`) 或斜體 (`Italic`)，目前需要載入另一個字型檔並自行管理切換（或者擴充此模組）。
2. **標籤嵌套**: 目前的解析器較簡單，**不支援** 標籤嵌套（例如 `[color=red][color=blue]...[/color][/color]` 可能會解析錯誤）。
3. **Hex 顏色碼**: 目前僅支援英文單字顏色，尚未支援 `#FF0000` 格式。

### 3. 效能注意事項 (Performance)

* **Flush 代價**: 當字形快取滿時，系統會執行 `FlushCache`。這會瞬間清空整個紋理並重新繪製當前幀的文字。這會造成該幀輕微的 CPU 負載。
* *解法*: 如果發現經常卡頓，請加大 `MAX_GLYPHS` 和 `ATLAS_SIZE`。


* **描邊成本**: `enableOutline` 會使繪製呼叫次數增加 4 倍（上下左右各畫一次）。大量文字時請謹慎使用，或減少 `outlineThickness`。

---

## 🛠️ 依賴函式庫

* **Raylib 4.0+**
* **stb_truetype.h** (v1.26+) - 已包含在大多數 Raylib 發行版中，或需單獨下載。

---

## 📝 版本紀錄

* **v1.0**: 初始版本。
* **v1.1 (Optimized)**:
* 新增 Hash Map  查找。
* 新增 Auto-Flush 機制，修復圖集滿時文字隱形 Bug。
* 修復 Global Background 大小計算邏輯。
