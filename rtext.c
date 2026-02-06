#define STB_TRUETYPE_IMPLEMENTATION
#include "rtext.h"
#include "stb_truetype.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h> // for strcasecmp/strncasecmp (non-standard but common)

// -------------------------------------------------------------------------
// 參數設定 (可根據需求調整)
// -------------------------------------------------------------------------

// 最大快取字形數 (針對中文環境，建議設為 4096 或更高)
#define MAX_GLYPHS 4096

// 紋理圖集大小 (2048x2048 可容納更多字，減少 Flush 頻率)
#define ATLAS_SIZE 2048

// 最大行數與標籤長度限制
#define MAX_TAG_LEN 32

// -------------------------------------------------------------------------
// 內部結構與全局變數
// -------------------------------------------------------------------------

// 字形結構（儲存每個字元的紋理資訊）
typedef struct {
    int codepoint;          // Unicode碼點
    Rectangle srcRec;       // 在圖集中的矩形區域
    int bearingX, bearingY; // 字形偏移量
    int advance;            // 文字前進寬度
    bool active;            // 此插槽是否被佔用
} AdvGlyph;

// 全局上下文
static struct {
    unsigned char* fontData;      // 原始字型檔案資料
    stbtt_fontinfo info;          // stb_truetype 字型資訊
    
    AdvGlyph cache[MAX_GLYPHS];   // 字形資料陣列
    int hashLookup[MAX_GLYPHS];   // [NEW] 雜湊表 (Codepoint -> Cache Index)
    
    Texture2D atlas;              // 紋理圖集 (GPU)
    int atlasX, atlasY, rowHeight;// 圖集游標位置與目前行高
    
    float scale;                  // 字型縮放比例
    int ascent, descent, lineGap; // 字型度量資訊
    bool loaded;                  // 模組是否已初始化
} g_ctx = { 0 };

// -------------------------------------------------------------------------
// 內部輔助函數 (Private)
// -------------------------------------------------------------------------

// 跨平台字串比對 (Windows/Linux 相容)
static int SafeStrNCaseCmp(const char* s1, const char* s2, size_t n) {
#if defined(_WIN32)
    return _strnicmp(s1, s2, n);
#else
    return strncasecmp(s1, s2, n);
#endif
}

// 解析顏色標籤 [color=xxx]
static Color GetTagColor(const char* tag_start)
{
    // tag_start 指向 "color=" 後面的內容
    // 為了安全與效能，我們只比對有限長度
    if (SafeStrNCaseCmp(tag_start, "red", 3) == 0) return RED;
    if (SafeStrNCaseCmp(tag_start, "green", 5) == 0) return GREEN;
    if (SafeStrNCaseCmp(tag_start, "blue", 4) == 0) return BLUE;
    if (SafeStrNCaseCmp(tag_start, "yellow", 6) == 0) return YELLOW;
    if (SafeStrNCaseCmp(tag_start, "purple", 6) == 0) return PURPLE;
    if (SafeStrNCaseCmp(tag_start, "orange", 6) == 0) return ORANGE;
    if (SafeStrNCaseCmp(tag_start, "darkblue", 8) == 0) return DARKBLUE;
    if (SafeStrNCaseCmp(tag_start, "darkgray", 8) == 0) return DARKGRAY;
    if (SafeStrNCaseCmp(tag_start, "maroon", 6) == 0) return MAROON;
    if (SafeStrNCaseCmp(tag_start, "white", 5) == 0) return WHITE;
    if (SafeStrNCaseCmp(tag_start, "black", 5) == 0) return BLACK;
    if (SafeStrNCaseCmp(tag_start, "gray", 4) == 0) return GRAY;
    
    return RAYWHITE; // 預設顏色
}

// [NEW] 清空快取與圖集 (當空間不足時呼叫)
static void FlushCache(void)
{
    // 1. 清空 GPU 紋理 (填入全透明)
    // 建立一個全空的緩衝區來重置紋理
    void* clearData = calloc(ATLAS_SIZE * ATLAS_SIZE, 4); // RGBA * 4 bytes
    if (clearData) {
        UpdateTexture(g_ctx.atlas, clearData);
        free(clearData);
    }

    // 2. 重置圖集游標
    g_ctx.atlasX = 2;
    g_ctx.atlasY = 2;
    g_ctx.rowHeight = 0;

    // 3. 重置所有快取資料
    for (int i = 0; i < MAX_GLYPHS; i++) {
        g_ctx.cache[i].active = false;
        g_ctx.hashLookup[i] = -1; // -1 表示空
    }

    TraceLog(LOG_INFO, "AdvText: Cache flushed (Atlas full or Limit reached).");
}

// [NEW] 取得字形 (核心優化：Hash Map + Auto Flush)
static AdvGlyph* GetGlyph(int cp)
{
    // --- 步驟 1: 查表 (Hash Lookup) ---
    // 使用簡單的模數雜湊
    int hashIndex = cp % MAX_GLYPHS;
    int step = 0;

    // 線性探測 (Linear Probing) 處裡碰撞
    while (step < MAX_GLYPHS) {
        int cacheIdx = g_ctx.hashLookup[hashIndex];
        
        // 若該位置為 -1，表示真的沒快取過
        if (cacheIdx == -1) break;

        // 若找到了對應的 Codepoint 且該快取有效
        if (g_ctx.cache[cacheIdx].active && g_ctx.cache[cacheIdx].codepoint == cp) {
            return &g_ctx.cache[cacheIdx]; // 命中！直接回傳
        }

        // 發生碰撞 (Collision)，找下一個位置
        hashIndex = (hashIndex + 1) % MAX_GLYPHS;
        step++;
    }

    // --- 步驟 2: 沒找到，準備新增字形 ---
    
    // 找一個空閒的快取插槽
    int newIdx = -1;
    // 這裡可以使用另一個變數追蹤最後使用的 index 來加速，目前先簡單遍歷
    // 由於我們有 Hash Map，這裡通常不會很慢，除非快取快滿了
    for (int i = 0; i < MAX_GLYPHS; i++) {
        if (!g_ctx.cache[i].active) {
            newIdx = i;
            break;
        }
    }

    // 若快取陣列全滿 -> 強制清空 (Flush)
    if (newIdx == -1) {
        FlushCache();
        return GetGlyph(cp); // 遞迴重試 (這時一定有空位)
    }

    // --- 步驟 3: 使用 stb_truetype 產生字形 ---
    int w, h, xoff, yoff, adv;
    // 取得 Bitmap 與 Metrics
    unsigned char* bmp = stbtt_GetCodepointBitmap(&g_ctx.info, 0, g_ctx.scale, cp, &w, &h, &xoff, &yoff);
    stbtt_GetCodepointHMetrics(&g_ctx.info, cp, &adv, NULL);

    // --- 步驟 4: 檢查圖集空間 ---
    // 檢查水平空間
    if (g_ctx.atlasX + w + 2 >= ATLAS_SIZE) {
        g_ctx.atlasX = 2; // 換行
        g_ctx.atlasY += g_ctx.rowHeight + 2;
        g_ctx.rowHeight = 0;
    }

    // 檢查垂直空間 (圖集滿了?)
    if (g_ctx.atlasY + h + 2 >= ATLAS_SIZE) {
        if (bmp) stbtt_FreeBitmap(bmp, NULL);
        FlushCache(); // 圖集滿了 -> 強制清空
        return GetGlyph(cp); // 遞迴重試
    }

    // --- 步驟 5: 上傳像素到 GPU ---
    if (w > 0 && h > 0) {
        // stbtt 回傳的是單通道 (Alpha)，我們轉成 RGBA (白色 + Alpha)
        unsigned char* px = (unsigned char*)MemAlloc(w * h * 4);
        for (int i = 0; i < w * h; i++) {
            px[i * 4] = 255;     // R
            px[i * 4 + 1] = 255; // G
            px[i * 4 + 2] = 255; // B
            px[i * 4 + 3] = bmp[i]; // Alpha from font
        }
        
        // 更新圖集局部區域
        UpdateTextureRec(g_ctx.atlas, (Rectangle) { (float)g_ctx.atlasX, (float)g_ctx.atlasY, (float)w, (float)h }, px);
        MemFree(px);
    }
    
    if (bmp) stbtt_FreeBitmap(bmp, NULL);

    // --- 步驟 6: 寫入快取結構 ---
    AdvGlyph* g = &g_ctx.cache[newIdx];
    g->codepoint = cp;
    g->srcRec = (Rectangle){ (float)g_ctx.atlasX, (float)g_ctx.atlasY, (float)w, (float)h };
    g->bearingX = xoff;
    g->bearingY = yoff;
    g->advance = (int)(adv * g_ctx.scale);
    g->active = true;

    // --- 步驟 7: 更新 Hash Map ---
    // 重新計算 Hash 位置 (因為可能經過線性探測)
    hashIndex = cp % MAX_GLYPHS;
    while (g_ctx.hashLookup[hashIndex] != -1) {
        hashIndex = (hashIndex + 1) % MAX_GLYPHS;
    }
    g_ctx.hashLookup[hashIndex] = newIdx; // 紀錄 Codepoint 對應到的 Cache Index

    // 更新圖集游標
    g_ctx.atlasX += w + 2;
    if (h > g_ctx.rowHeight) g_ctx.rowHeight = h;

    return g;
}

// -------------------------------------------------------------------------
// 公開 API 實作
// -------------------------------------------------------------------------

void InitAdvText(const char* fontPath, int fontSize)
{
    if (g_ctx.loaded) UnloadAdvText(); // 防止重複初始化

    int size;
    g_ctx.fontData = LoadFileData(fontPath, &size);
    if (!g_ctx.fontData) {
        TraceLog(LOG_WARNING, "AdvText: Failed to load font data from %s", fontPath);
        return;
    }

    if (!stbtt_InitFont(&g_ctx.info, g_ctx.fontData, 0)) {
        TraceLog(LOG_ERROR, "AdvText: Failed to init stbtt font");
        UnloadFileData(g_ctx.fontData);
        return;
    }

    // 計算字型度量
    g_ctx.scale = stbtt_ScaleForPixelHeight(&g_ctx.info, (float)fontSize);
    stbtt_GetFontVMetrics(&g_ctx.info, &g_ctx.ascent, &g_ctx.descent, &g_ctx.lineGap);
    g_ctx.ascent = (int)(g_ctx.ascent * g_ctx.scale);
    g_ctx.descent = (int)(g_ctx.descent * g_ctx.scale);
    g_ctx.lineGap = (int)(g_ctx.lineGap * g_ctx.scale);

    // 建立紋理圖集
    Image img = GenImageColor(ATLAS_SIZE, ATLAS_SIZE, BLANK);
    g_ctx.atlas = LoadTextureFromImage(img);
    SetTextureFilter(g_ctx.atlas, TEXTURE_FILTER_BILINEAR);
    UnloadImage(img);

    // 初始化狀態
    g_ctx.atlasX = 2;
    g_ctx.atlasY = 2;
    g_ctx.rowHeight = 0;
    
    // 初始化雜湊表 (-1 代表空)
    for(int i=0; i<MAX_GLYPHS; i++) g_ctx.hashLookup[i] = -1;
    memset(g_ctx.cache, 0, sizeof(g_ctx.cache));

    g_ctx.loaded = true;
    TraceLog(LOG_INFO, "AdvText: Initialized with font %s size %d (Atlas: %dx%d)", fontPath, fontSize, ATLAS_SIZE, ATLAS_SIZE);
}

void UnloadAdvText(void)
{
    if (g_ctx.loaded) {
        UnloadTexture(g_ctx.atlas);
        UnloadFileData(g_ctx.fontData);
        g_ctx.fontData = NULL;
        g_ctx.loaded = false;
        TraceLog(LOG_INFO, "AdvText: Unloaded");
    }
}

// 核心繪製函數
void DrawRichTextStyled(const char* text, Vector2 pos, int charLimit, AdvTextStyle style)
{
    if (!g_ctx.loaded || !text) return;

    // 預設值保護
    if (style.bgPaddingX == 0) style.bgPaddingX = 8.0f;
    if (style.bgPaddingY == 0) style.bgPaddingY = 6.0f;
    if (style.outlineThickness == 0) style.outlineThickness = 1.0f;
    if (style.lineSpacing == 0) style.lineSpacing = 1.0f;

    float lineHeight = (float)(g_ctx.ascent - g_ctx.descent + g_ctx.lineGap) * style.lineSpacing;
    float maxLineW = 0.0f;
    float totalH = 0.0f;

    // ---------------------------------------------------------------------
    // 第一階段：預掃描 (Pre-scan)
    // 計算整體尺寸，用於繪製 Global Background
    // [FIX] 這裡移除了 charLimit 的限制，確保背景框總是顯示「全文」的大小
    // ---------------------------------------------------------------------
    {
        int scanIdx = 0;
        float curY = pos.y;
        
        while (text[scanIdx]) {
            float lineW = 0.0f;
            int lineScan = scanIdx;
            bool forcedNL = false;

            while (text[lineScan]) {
                // 處理標籤 (跳過不計寬度)
                if (text[lineScan] == '[') {
                    if (strncmp(&text[lineScan], "[/color]", 8) == 0) {
                        lineScan += 8; continue;
                    }
                    if (strncmp(&text[lineScan], "[color=", 7) == 0) {
                        char* end = strchr(&text[lineScan + 7], ']');
                        if (end) {
                            lineScan = (int)(end - text) + 1; continue;
                        }
                    }
                }

                int bytes = 0;
                int cp = GetCodepointNext(&text[lineScan], &bytes);
                
                if (cp == 0) break;
                if (cp == '\n') {
                    forcedNL = true;
                    lineScan += bytes;
                    break;
                }

                AdvGlyph* g = GetGlyph(cp);
                if (g) {
                    // 自動換行檢查
                    if (style.maxWidth > 0 && lineW + g->advance > style.maxWidth) {
                        break; // 此行結束，雖然還沒遇到 \n
                    }
                    lineW += g->advance;
                }
                lineScan += bytes;
            }

            if (lineW > maxLineW) maxLineW = lineW;
            totalH += lineHeight;

            scanIdx = lineScan;
        }
    }

    // ---------------------------------------------------------------------
    // 繪製 Global Background (如果啟用)
    // ---------------------------------------------------------------------
    if (style.enableBackground && style.enableGlobalBackground && maxLineW > 0) {
        float bgW = maxLineW + style.bgPaddingX * 2;
        float bgH = totalH + style.bgPaddingY * 2;
        float bgX = pos.x - bgW / 2.0f; // 預設 Center Align 的背景位置

        if (style.align == TEXT_ALIGN_LEFT)
            bgX = pos.x - style.bgPaddingX;
        else if (style.align == TEXT_ALIGN_RIGHT)
            bgX = pos.x - bgW + style.bgPaddingX;

        Rectangle bgRec = { bgX, pos.y - style.bgPaddingY, bgW, bgH };
        DrawRectangleRounded(bgRec, 0.1f, 8, style.backgroundColor);
    }

    // ---------------------------------------------------------------------
    // 第二階段：正式繪製文字
    // ---------------------------------------------------------------------
    float curY = pos.y;
    Color curColor = style.baseColor;
    int displayedChars = 0; // 已顯示的可見字元計數
    int idx = 0;

    while (text[idx] && (charLimit < 0 || displayedChars < charLimit)) {
        float lineW = 0.0f;
        int lineStartIdx = idx;
        int lineCharCount = 0; // 這行的可見字元數
        Color tempColor = curColor; // 暫存顏色狀態給寬度計算用
        bool forcedNewline = false;

        // 2.1 掃描當前行 (計算寬度與換行點)
        int scan = idx;
        while (text[scan]) {
            // 標籤處理
            if (text[scan] == '[') {
                if (strncmp(&text[scan], "[/color]", 8) == 0) {
                    tempColor = style.baseColor;
                    scan += 8; continue;
                }
                if (strncmp(&text[scan], "[color=", 7) == 0) {
                    char* end = strchr(&text[scan + 7], ']');
                    if (end) {
                        tempColor = GetTagColor(&text[scan + 7]);
                        scan = (int)(end - text) + 1; continue;
                    }
                }
            }

            int bytes = 0;
            int cp = GetCodepointNext(&text[scan], &bytes);
            if (cp == 0) break;
            if (cp == '\n') {
                forcedNewline = true;
                scan += bytes;
                break;
            }

            AdvGlyph* g = GetGlyph(cp);
            if (!g) { scan += bytes; continue; }

            // 打字機限制：這裡只用於判斷換行邏輯是否因為還沒打出來的字而改變
            // 但標準做法是排版應該是固定的，所以這裡不該受 charLimit 影響換行位置
            // 我們只判斷寬度自動換行
            if (style.maxWidth > 0 && lineW + g->advance > style.maxWidth) {
                break; 
            }

            lineW += g->advance;
            lineCharCount++;
            scan += bytes;
        }

        // 2.2 計算對齊偏移
        float offX = 0.0f;
        if (style.align == TEXT_ALIGN_CENTER) offX = -lineW / 2.0f;
        else if (style.align == TEXT_ALIGN_RIGHT) offX = -lineW;

        float startX = pos.x + offX;

        // 2.3 繪製行背景 (如果未啟用 Global Background)
        if (style.enableBackground && !style.enableGlobalBackground) {
            DrawRectangleRounded(
                (Rectangle) { startX - style.bgPaddingX, curY - style.bgPaddingY, lineW + style.bgPaddingX * 2, lineHeight + style.bgPaddingY * 2 },
                0.2f, 4, style.backgroundColor);
        }

        // 2.4 逐字繪製
        float drawX = startX;
        idx = lineStartIdx; // 回到行首開始畫
        
        while (idx < scan && (charLimit < 0 || displayedChars < charLimit)) {
            // 標籤處理 (真正改變顏色)
            if (text[idx] == '[') {
                if (strncmp(&text[idx], "[/color]", 8) == 0) {
                    curColor = style.baseColor;
                    idx += 8; continue;
                }
                if (strncmp(&text[idx], "[color=", 7) == 0) {
                    char* end = strchr(&text[idx + 7], ']');
                    if (end) {
                        curColor = GetTagColor(&text[idx + 7]);
                        idx = (int)(end - text) + 1; continue;
                    }
                }
            }

            int bytes = 0;
            int cp = GetCodepointNext(&text[idx], &bytes);
            
            // 換行符號不畫，但前面已經在 scan 迴圈處理過了，這裡主要是防禦
            if (cp == '\n') { idx += bytes; break; }

            AdvGlyph* g = GetGlyph(cp);
            if (!g) { idx += bytes; continue; }

            Vector2 p = { drawX + g->bearingX, curY + g_ctx.ascent + g->bearingY };

            // 繪製順序：陰影 -> 描邊 -> 本體
            if (style.enableShadow) {
                DrawTextureRec(g_ctx.atlas, g->srcRec, (Vector2){p.x + style.shadowOffset.x, p.y + style.shadowOffset.y}, style.shadowColor);
            }

            if (style.enableOutline) {
                float t = style.outlineThickness;
                // 簡單的 4 向描邊，要求高可用 8 向
                DrawTextureRec(g_ctx.atlas, g->srcRec, (Vector2){p.x - t, p.y}, style.outlineColor);
                DrawTextureRec(g_ctx.atlas, g->srcRec, (Vector2){p.x + t, p.y}, style.outlineColor);
                DrawTextureRec(g_ctx.atlas, g->srcRec, (Vector2){p.x, p.y - t}, style.outlineColor);
                DrawTextureRec(g_ctx.atlas, g->srcRec, (Vector2){p.x, p.y + t}, style.outlineColor);
            }

            DrawTextureRec(g_ctx.atlas, g->srcRec, p, curColor);

            drawX += g->advance;
            displayedChars++;
            idx += bytes;
        }
        
        // 處理這行剩下的標籤 (避免顏色沒切回來影響下一行，或是索引沒跟上)
        // 簡單來說，如果因為 charLimit 提早跳出，idx 會停在中間，下個 frame 會繼續
        // 但如果這行畫完了，idx 應該要等於 scan (除非有換行符)
        if (idx < scan) idx = scan; 

        // 如果是因為強制換行跳出的，要吃掉那個換行符
        if (forcedNewline) {
             // 確保下次迴圈從換行後開始
        }

        curY += lineHeight;
    }
}

void UpdateTypewriter(Typewriter* tw, const char* text, float delta) {
    if (tw->isFinished) return;
    
    tw->elapsed += delta;
    int targetChars = (int)(tw->elapsed * tw->speed);
    
    // 計算實際的可見字元總數
    int visibleLen = 0;
    int i = 0;
    
    // 預先計算總長度，判斷是否結束
    // 這裡其實可以優化，不用每幀重算總長，但為了 API 簡單先這樣做
    int tempIdx = 0;
    int totalVisible = 0;
    while(text[tempIdx]) {
        if (text[tempIdx] == '[') {
            if (strncmp(&text[tempIdx], "[/color]", 8) == 0) { tempIdx += 8; continue; }
            if (strncmp(&text[tempIdx], "[color=", 7) == 0) {
                char* end = strchr(&text[tempIdx + 7], ']');
                if (end) { tempIdx = (int)(end - text) + 1; continue; }
            }
        }
        int bytes;
        int cp = GetCodepointNext(&text[tempIdx], &bytes);
        if (cp != '\n') totalVisible++;
        tempIdx += bytes;
    }

    if (targetChars > totalVisible) targetChars = totalVisible;
    tw->currentChars = targetChars;
    tw->isFinished = (tw->currentChars >= totalVisible);
}