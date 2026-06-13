// ============================================================================
// usb_storage.cpp — FatFS + FatFSUSB (USBマスストレージ) 管理
// ============================================================================
#include "usb_storage.h"
#include <FatFS.h>

#if USE_USB_MSC
#include <FatFSUSB.h>

static volatile bool s_connected   = false;  // PCマウント中
static volatile bool s_fs_busy     = false;  // 再生/ロード中 (PCマウント保留)
static volatile bool s_wants_mount = false;  // PCがマウント要求して待機
static volatile bool s_unplugged   = false;  // 取り外し直後

// PCがマウント完了 → Pico側はFSを手放す
static void cb_plug(uint32_t) {
    s_connected = true;
    FatFS.end();
}
// PCが取り外し → Pico側でFS再マウント、再スキャンを促す
static void cb_unplug(uint32_t) {
    s_connected = false;
    s_unplugged = true;
    FatFS.begin();
}
// PCがマウント可能か? 占有中(再生/ファイルオープン中)は不可。
static bool cb_mountable(uint32_t) {
    if (s_fs_busy) { s_wants_mount = true; return false; }
    return true;
}

bool usb_storage_begin() {
    // autoFormat=true: LittleFSからの初回はFAT未フォーマット → 自動でFAT化
    FatFS.setConfig(FatFSConfig(/*autoFormat*/true, /*useFTL*/true));
    if (!FatFS.begin()) {
        if (!FatFS.format() || !FatFS.begin()) return false;
    }
    FatFSUSB.onPlug(cb_plug);
    FatFSUSB.onUnplug(cb_unplug);
    FatFSUSB.driveReady(cb_mountable);
    FatFSUSB.begin();
    delay(2000);   // TinyUSB の初期化レース回避 (公式例どおり)
    return true;
}

bool usb_pc_connected()      { return s_connected; }
void usb_set_fs_busy(bool b) { s_fs_busy = b; }
bool usb_take_wants_mount()  { bool v = s_wants_mount; s_wants_mount = false; return v; }
bool usb_take_unplugged()    { bool v = s_unplugged;   s_unplugged   = false; return v; }

#else  // ---- USB公開を無効化 (FatFSは使うがUSBドライブにはしない) ----
bool usb_storage_begin() {
    FatFS.setConfig(FatFSConfig(true, true));
    if (!FatFS.begin()) { return FatFS.format() && FatFS.begin(); }
    return true;
}
bool usb_pc_connected()      { return false; }
void usb_set_fs_busy(bool)   {}
bool usb_take_wants_mount()  { return false; }
bool usb_take_unplugged()    { return false; }
#endif