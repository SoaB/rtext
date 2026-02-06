#ifndef __R_TEXT_H__
#define __R_TEXT_H__

#include "raylib.h"
#include <stdbool.h>

// -------------------------------------------------------------------------
// 樣式與結構定義
// -------------------------------------------------------------------------

// 文字對齊枚舉
typedef enum {
    TEXT_ALIGN_LEFT = 0,
    TEXT_ALIGN_CENTER,
    TEXT_ALIGN_RIGHT
} AdvTextAlign;

// 打字機結構（控制顯示速度與進度）
typedef struct {
    float speed;        // 每秒顯示字元數
    float elapsed;      // 累積時間
    int currentChars;   // 目前顯示字元數（忽略標籤）
    bool isFinished;    // 是否完成顯示
} Typewriter;

// 文字樣式結構（控制顏色、對齊、陰影、描邊、背景等）
typedef struct {
    Color baseColor;            // 基礎顏色
    AdvTextAlign align;         // 對齊方式
    float maxWidth;             // 最大寬度（超過自動換行，0為不限制）
    float lineSpacing;          // 行距倍數（預設1.0f）

    // 背景相關
    bool enableBackground;       // 是否啟用背景框（每行或全域）
    bool enableGlobalBackground; // true: 整個段落一個大框 / false: 每行獨立背景
    Color backgroundColor;       // 背景顏色
    float bgPaddingX;            // 背景水平內距
    float bgPaddingY;            // 背景垂直內距

    // 陰影相關
    bool enableShadow;           // 是否啟用陰影
    Color shadowColor;           // 陰影顏色
    Vector2 shadowOffset;        // 陰影偏移（x,y）

    // 描邊相關
    bool enableOutline;          // 是否啟用描邊
    Color outlineColor;          // 描邊顏色
    float outlineThickness;      // 描邊厚度（預設1.0f）
} AdvTextStyle;

// -------------------------------------------------------------------------
// 函數宣告
// -------------------------------------------------------------------------

// 初始化模組（載入字型，設定大小）
void InitAdvText(const char* fontPath, int fontSize);

// 釋放資源
void UnloadAdvText(void);

// 繪製富文本（核心函數：支援顏色標籤、樣式、字元限制）
void DrawRichTextStyled(const char* text, Vector2 pos, int charLimit, AdvTextStyle style);

// 更新打字機狀態（計算目前應顯示字元數，忽略標籤）
void UpdateTypewriter(Typewriter* tw, const char* text, float delta);

#endif // __R_TEXT_H__