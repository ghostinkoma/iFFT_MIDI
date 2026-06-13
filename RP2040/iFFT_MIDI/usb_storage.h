#pragma once
// ============================================================================
// usb_storage.h — 内蔵フラッシュFAT(FatFS)を USBメモリとしてPCへ公開する管理層
//
//   ・PCはLittleFSを読めないため、共有ストレージは FatFS(FAT) を使う。
//   ・FatFSUSB が FAT パーティションを USB マスストレージとして公開する。
//   ・PCがマウント中は Pico 側の FS アクセスは一切禁止 (FATは排他)。
//     → 接続を検知したら再生を止め、OLEDに "Connected PC" を表示する。
//   ・取り外し後は songlist を再スキャンして通常動作へ戻る。
//
//   ※ LittleFS から FatFS へ移行するため、初回起動時に内蔵FSは
//      自動的にFATへ再フォーマットされる(=既存のMIDIは消える)。
//      以後はPCからこのUSBドライブに MIDI / SongList.txt を置く。
// ============================================================================
#include <Arduino.h>
#include "config.h"

bool usb_storage_begin();        // FatFS.begin()(必要ならFAT自動フォーマット)+ USB公開
bool usb_pc_connected();         // PCがドライブをマウント中か
void usb_set_fs_busy(bool busy); // 再生/曲ロード中など FS占有中を通知 (PCマウントを保留)
bool usb_take_wants_mount();     // PCがマウントを要求して待っている (1回だけtrue)
bool usb_take_unplugged();       // PCが取り外した (1回だけtrue: 再スキャン契機)