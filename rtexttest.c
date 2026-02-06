#include "raylib.h"
#include "rtext.h"

int main(void)
{
    InitWindow(800, 600, "AdvText Optimized Demo");
    SetTargetFPS(60);

    // 1. 初始化 (請確保 assets 資料夾下有支援中文的 ttf 字型，例如微軟正黑體或 NotoSans)
    // 注意：Windows 內建字型通常在 C:/Windows/Fonts/msjh.ttc (需複製出來改名或指定路徑)
    // 這裡假設你有一個 tpu.ttf
    InitAdvText("assets/tpu.ttf", 24);

    // 測試長中文文本與顏色混合
    const char* storyText = 
        "[color=orange]系統提示：[/color] [color=white]快取優化模組已啟動。[/color]\n\n"
        "[color=yellow]勇者[/color]走進了古老的遺跡，牆上刻滿了[color=green]神秘的符文[/color]。\n"
        "這裡的空氣充滿了[color=purple]魔力[/color]，彷彿隨時會有怪物跳出來。\n"
        "即使文字數量龐大，[color=blue]Hash Map[/color] 依然能保持高效渲染。\n"
        "這是一段用來測試[color=red]自動換行[/color]以及[color=darkgray]背景框自適應[/color]的長文本。\n\n"
        "當字形數量超過 4096 時，系統會自動執行 Flush 清空圖集，\n"
        "確保程式不會崩潰，且畫面保持流暢！";

    Typewriter tw = {
        .speed = 20.0f,
        .elapsed = 0.0f,
        .currentChars = 0,
        .isFinished = false
    };

    AdvTextStyle style = {
        .baseColor = RAYWHITE,
        .align = TEXT_ALIGN_CENTER,
        .maxWidth = 650.0f,
        .lineSpacing = 1.5f,
        
        .enableBackground = true,
        .enableGlobalBackground = true, // [FIX] 現在背景會一開始就顯示全框
        .backgroundColor = (Color){ 20, 30, 40, 200 },
        .bgPaddingX = 20.0f,
        .bgPaddingY = 20.0f,

        .enableShadow = true,
        .shadowColor = BLACK,
        .shadowOffset = { 2.0f, 2.0f },

        .enableOutline = true,
        .outlineColor = (Color){0, 0, 0, 128},
        .outlineThickness = 1.5f
    };

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        // 更新打字機
        UpdateTypewriter(&tw, storyText, dt);
        // 重播控制
        if (IsKeyPressed(KEY_SPACE)) {
            tw.elapsed = 0.0f;
            tw.currentChars = 0;
            tw.isFinished = false;
        }
        BeginDrawing();
        ClearBackground((Color){ 100, 100, 100, 255 }); // 灰色背景以凸顯半透明對話框
        // 繪製標題
        //DrawText("Press SPACE to Replay", 10, 10, 20, LIGHTGRAY);
        DrawRichTextStyled("Press SPACE to Replay", (Vector2){ GetScreenWidth()/2.0f, 10 }, -1, style);
        DrawFPS(10, 40);
        // 繪製富文本
        DrawRichTextStyled(storyText, (Vector2){ GetScreenWidth()/2.0f, 100 }, tw.currentChars, style);
        EndDrawing();
    }

    UnloadAdvText();
    CloseWindow();
    return 0;
}