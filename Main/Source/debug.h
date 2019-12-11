
//デバッグ出力する設定かどうかを返すマクロ
#define IS_DEBUG()              (g_bDioSettingMode ? (dio_read(PIN_DEBUG_OUTPUT) == LOW) : g_bDebugOutput)

//デバッグレベルが高に設定されているかどうかを返すマクロ
#define DEBUG_LEVEL()           (g_bDioSettingMode ? (dio_read(PIN_DEBUG_LEVEL) == LOW) : g_bDebugLevel)

typedef enum {
    DEBUG_INFO,
    DEBUG_WARNING,
    DEBUG_ERROR
} DEBUGMESSAGETYPE;

//デバッグモジュールの初期化
void debug_init();

//複数行のデバッグ出力を行う前にこのブロックを挟んで出力すれば
//余計な改行出力を抑えられる
void debug_begin();
void debug_end();

//デバッグで文字列を出力する
void debug_puts(DEBUGMESSAGETYPE msgType, const char *str);

//デバッグで書式化した文字列を出力する
void debug_printf(DEBUGMESSAGETYPE msgType, const char *fmt, ...);

