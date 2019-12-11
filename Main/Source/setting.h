

//DIO設定ピンの定数
#define PIN_DIO_SETTING         18
#define PIN_ADDRESS             19
#define PIN_DEBUG_OUTPUT        17
#define PIN_DEBUG_LEVEL         15
#define PIN_DEBUG_DEVICE        8
#define PIN_PROTECT_MODE        10
#define PIN_CHANNEL_B0          2
#define PIN_CHANNEL_B1          3
#define PIN_HW_FLOW_CONTROL     1


#define SETTING_RESULT_HAS_ERROR(r) (r & (SETTING_RESULT_UNKNOWN_COMMAND_BIT | SETTING_RESULT_VALUE_OUTOFRANGE_BIT | SETTING_RESULT_UNKNOWN_CHARACTER_BIT))

typedef enum {
    SETTING_RESULT_NOT_YET = 0,                 //設定は未完了
    SETTING_RESULT_NO_CHANGE = 1,               //設定完了。何も設定されなかった
    SETTING_RESULT_COMMAND_ACCEPTED = 2,        //設定完了。設定を受け付けた → 再起動
    SETTING_RESULT_UNKNOWN_COMMAND_BIT = 4,     //不明なコマンド
    SETTING_RESULT_VALUE_OUTOFRANGE_BIT = 8,    //値が範囲外
    SETTING_RESULT_UNKNOWN_CHARACTER_BIT = 16   //不明な文字
} E_SETTING_RESULT;


//デフォルト／シリアル／DIOピン設定を読み込む。setup()から呼び出す
uint8_t setting_load();

//現在の設定値を文字列で返す
const char *setting_current();

//シリアルから設定コマンドを読み込む
//この関数は設定コマンドと、設定モード終了コマンドを読み取って解釈する
//この関数は設定モード開始ワード"+++WSS"は解釈しない
//関数が SETTING_RESULT_COMMAND_ACCEPTED を返したときはスリープ、起床後に
// setting_load() を実行することで、シリアル設定値が有効になる
E_SETTING_RESULT setting_readFromSerial(uint16_t bytesToRead);
