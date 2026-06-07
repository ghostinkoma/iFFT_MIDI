#include "config.h"
#include "song_list.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ----------------------------------------------------------------
// show_on_oled
// ----------------------------------------------------------------
void SongList::show_on_oled(void* disp_ptr, int idx) const {
    if (idx<0||idx>=_count||!disp_ptr) return;
    Adafruit_SSD1306& d = *(Adafruit_SSD1306*)disp_ptr;
    const SongInfo& s = _songs[idx];

    d.clearDisplay();
    d.setTextColor(SSD1306_WHITE);
    d.setTextSize(1);

    // タイトル (最大2行 各21文字)
    int tlen = strlen(s.title);
    if (tlen <= 21) {
        d.setCursor(0, 2); d.println(s.title);
    } else {
        char buf[22];
        strncpy(buf, s.title, 21); buf[21]='\0';
        d.setCursor(0, 2); d.println(buf);
        strncpy(buf, s.title+21, 21); buf[21]='\0';
        d.setCursor(0, 14); d.println(buf);
    }

    d.drawFastHLine(0, 28, 128, SSD1306_WHITE);

    // Author
    d.setCursor(0, 32); d.print("By: ");
    char abuf[17]; strncpy(abuf, s.author, 16); abuf[16]='\0';
    d.println(abuf);

    // Arranger (あれば)
    if (s.has_arranger && s.arranger[0]!='\0') {
        d.setCursor(0, 44); d.print("Arr:");
        char rbuf[15]; strncpy(rbuf, s.arranger, 14); rbuf[14]='\0';
        d.println(rbuf);
    }

    // 曲番号
    d.setCursor(90, 56);
    char nbuf[12];
    snprintf(nbuf, sizeof(nbuf), "[%d/%d]", idx+1, _count);
    d.print(nbuf);

    d.display();
}

// ----------------------------------------------------------------
// ユーティリティ
// ----------------------------------------------------------------
void SongList::_unquote_trim(const char* src, char* dst, int max_len) {
    int j=0;
    for (int i=0; src[i]&&j<max_len-1; ++i) {
        char c=src[i];
        if (c=='"') continue;
        if (c=='\r'||c=='\n') break;
        dst[j++]=c;
    }
    dst[j]='\0';
    int start=0;
    while (dst[start]==' '||dst[start]=='\t') start++;
    if (start>0) memmove(dst, dst+start, strlen(dst+start)+1);
    int e=(int)strlen(dst)-1;
    while (e>=0&&(dst[e]==' '||dst[e]=='\t')) dst[e--]='\0';
}

bool SongList::_is_null_or_empty(const char* s) {
    if (!s||s[0]=='\0') return true;
    char tmp[8]; strncpy(tmp,s,7); tmp[7]='\0';
    for (int i=0;tmp[i];++i) tmp[i]=(char)tolower((unsigned char)tmp[i]);
    return strcmp(tmp,"null")==0;
}

// ----------------------------------------------------------------
// load
// ----------------------------------------------------------------
int SongList::load(const char* midi_dir) {
    _count=0;

    // MIDIファイル列挙
    struct MF { char path[64]; char name[64]; };
    static MF midi_files[MAX];
    int midi_count=0;

    Dir dir=LittleFS.openDir(midi_dir);
    while (dir.next()&&midi_count<MAX) {
        String fname=dir.fileName();
        if (fname.endsWith(".mid")||fname.endsWith(".MID")) {
            bool slash=(midi_dir[strlen(midi_dir)-1]=='/');
            snprintf(midi_files[midi_count].path,64,"%s%s%s",
                     midi_dir,slash?"":"/",fname.c_str());
            strncpy(midi_files[midi_count].name,fname.c_str(),63);
            midi_count++;
        }
    }
    DBG_PRINTF("[songlist] %d midi files\n", midi_count);

    // SongList.txt なし
    if (!LittleFS.exists(CFG_SONGLIST_FILE)) {
        DBG_PRINTLN("[songlist] no SongList.txt");
        for (int i=0;i<midi_count;++i) {
            SongInfo& s=_songs[_count++];
            strncpy(s.midi_path,midi_files[i].path,63);
            strncpy(s.title,midi_files[i].name,47);
            char* dot=strrchr(s.title,'.'); if(dot)*dot='\0';
            strncpy(s.author,"Unknown",31);
            s.arranger[0]='\0';
            s.has_title=false; s.has_arranger=false;
        }
        return _count;
    }

    File f=LittleFS.open(CFG_SONGLIST_FILE,"r");
    if (!f) return 0;

    char cur_hash[64]={}, cur_title[48]={};
    char cur_author[32]={}, cur_arranger[32]={};
    bool cur_has_title=false, cur_has_author=false, cur_has_arranger=false;

    auto commit=[&](){
        if (!cur_hash[0]) return;
        int matched=-1;
        for (int i=0;i<midi_count;++i) {
            if (strcasecmp(midi_files[i].name,cur_hash)==0||
                strcasecmp(midi_files[i].path,cur_hash)==0) {
                matched=i; break;
            }
        }
        if (matched<0||_count>=MAX) {
            DBG_PRINTF("[songlist] '%s' not found\n",cur_hash); return;
        }
        SongInfo& s=_songs[_count++];
        strncpy(s.midi_path,midi_files[matched].path,63);
        if (cur_has_title&&!_is_null_or_empty(cur_title)) {
            strncpy(s.title,cur_title,47); s.has_title=true;
        } else {
            strncpy(s.title,midi_files[matched].name,47);
            char* dot=strrchr(s.title,'.'); if(dot)*dot='\0';
            s.has_title=false;
        }
        if (cur_has_author&&!_is_null_or_empty(cur_author))
            strncpy(s.author,cur_author,31);
        else
            strncpy(s.author,"Unknown",31);
        if (cur_has_arranger&&!_is_null_or_empty(cur_arranger)) {
            strncpy(s.arranger,cur_arranger,31); s.has_arranger=true;
        } else {
            s.arranger[0]='\0'; s.has_arranger=false;
        }
    };

    while (f.available()) {
        String line=f.readStringUntil('\n');
        line.trim();
        if (!line.length()||line.startsWith(";")) continue;
        int eq=line.indexOf('=');
        if (eq<0) continue;
        String key=line.substring(0,eq); key.trim(); key.toLowerCase();
        char val_buf[64]; line.substring(eq+1).toCharArray(val_buf,64);
        char unq[64]; _unquote_trim(val_buf,unq,64);

        if (key=="filenamehash") {
            commit();
            strncpy(cur_hash,unq,63);
            cur_title[0]=cur_author[0]=cur_arranger[0]='\0';
            cur_has_title=cur_has_author=cur_has_arranger=false;
        } else if (key=="title") {
            strncpy(cur_title,unq,47); cur_has_title=true;
        } else if (key=="author") {
            strncpy(cur_author,unq,31); cur_has_author=true;
        } else if (key=="arranger") {
            strncpy(cur_arranger,unq,31); cur_has_arranger=true;
        }
    }
    f.close();
    commit();

    // 未掲載のMIDIを末尾に追加
    for (int i=0;i<midi_count;++i) {
        bool found=false;
        for (int j=0;j<_count;++j)
            if (strcmp(_songs[j].midi_path,midi_files[i].path)==0) {found=true;break;}
        if (!found&&_count<MAX) {
            SongInfo& s=_songs[_count++];
            strncpy(s.midi_path,midi_files[i].path,63);
            strncpy(s.title,midi_files[i].name,47);
            char* dot=strrchr(s.title,'.'); if(dot)*dot='\0';
            strncpy(s.author,"Unknown",31);
            s.arranger[0]='\0'; s.has_title=false; s.has_arranger=false;
        }
    }

    DBG_PRINTF("[songlist] %d songs\n",_count);
    for (int i=0;i<_count;++i)
        DBG_PRINTF("  [%d] \"%s\" / %s\n",i,_songs[i].title,_songs[i].author);
    return _count;
}