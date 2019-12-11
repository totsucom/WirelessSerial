#include "basicio.h"
#include "main.h"
#include "setting.h"
#include "debug.h"

//参照するグローバル変数
extern bool_t g_bDioSettingMode;
extern bool_t g_bDebugOutput;
extern bool_t g_bDebugLevel;
extern bool_t g_bDebugDevice;

extern bool_t __printf(bool_t (*__putc)(char), const char *fmt, va_list ap);


//デバッグ出力前に改行コードが必要か
static bool_t m_bNewLineRequired;

//debug_begin()～debug_end()ブロック内にいる
static bool_t m_bInBlock;

/* デバッグ出力するシリアル関数へのポインタ */
static bool_t (*s_putc)(char);
static bool_t (*s_puts)(const char *);

/* シリアル出力関数 */
static bool_t s0_putc(char c) { return serialx_putc(E_AHI_UART_0, c); }
static bool_t s1_putc(char c) { return serialx_putc(E_AHI_UART_1, c); }
static bool_t s0_puts(const char *str) { return serialx_puts(E_AHI_UART_0, str); }
static bool_t s1_puts(const char *str) { return serialx_puts(E_AHI_UART_1, str); }

//デバッグモジュールの初期化
//setting_load()後に呼び出すこと
void debug_init() {

    //シリアル０の場合は最初に改行が必要
    m_bNewLineRequired = !g_bDebugDevice;

    m_bInBlock = FALSE;

    //シリアル０または１に出力する関数をここでポインタに格納しておく
    s_putc = g_bDebugDevice ? s1_putc : s0_putc;
    s_puts = g_bDebugDevice ? s1_puts : s0_puts;
}

//複数行のデバッグ出力を行う前にこのブロックを挟んで出力すれば
//余計な改行出力を抑えられる
void debug_begin() {
    m_bInBlock = TRUE;
    if (m_bNewLineRequired && IS_DEBUG()) {
        s_puts("\r\n");
    }
}

void debug_end() {
    m_bInBlock = FALSE;
}

//デバッグで文字列を出力する
void debug_puts(DEBUGMESSAGETYPE msgType, const char *str) {
    if (!IS_DEBUG()) return;
    if (!DEBUG_LEVEL() && msgType != DEBUG_ERROR) return;

    if (m_bNewLineRequired && !m_bInBlock) {
        s_puts("\r\n");
    }

    switch (msgType) {
    case DEBUG_INFO:    s_puts("[I] "); break;
    case DEBUG_WARNING: s_puts("[W] "); break;
    case DEBUG_ERROR:   s_puts("[E] "); break;
    }

    s_puts(str);
    s_puts("\r\n");
}

//デバッグで書式化した文字列を出力する
void debug_printf(DEBUGMESSAGETYPE msgType, const char *fmt, ...) {
    if (!IS_DEBUG()) return;
    if (!DEBUG_LEVEL() && msgType != DEBUG_ERROR) return;

    if (m_bNewLineRequired && !m_bInBlock) {
        s_puts("\r\n");
    }

    switch (msgType) {
    case DEBUG_INFO:    s_puts("[I] "); break;
    case DEBUG_WARNING: s_puts("[W] "); break;
    case DEBUG_ERROR:   s_puts("[E] "); break;
    }

    //パラメータを取得
    va_list ap;
    va_start(ap, fmt);

    if (!__printf(s_putc, fmt, ap)) {
        //バッファオーバーフロー
        va_end(ap);
        return;
    }
    va_end(ap);

    s_puts("\r\n");
}

