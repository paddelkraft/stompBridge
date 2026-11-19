#include "live_json.h"

#include <cstdio>
#include <cstring>

namespace stomp::ui {
namespace {

bool validUtf8(const uint8_t *data, size_t length) {
  if (data == nullptr) return false;
  for (size_t i = 0; i < length;) {
    const uint8_t first = data[i++];
    if (first <= 0x7f) continue;
    if (first >= 0xc2 && first <= 0xdf) {
      if (i >= length || (data[i++] & 0xc0) != 0x80) return false;
      continue;
    }
    if (first >= 0xe0 && first <= 0xef) {
      if (i + 1 >= length) return false;
      const uint8_t second = data[i++];
      const uint8_t third = data[i++];
      if ((third & 0xc0) != 0x80 ||
          (first == 0xe0 ? second < 0xa0 || second > 0xbf :
           first == 0xed ? second < 0x80 || second > 0x9f :
                           (second & 0xc0) != 0x80)) return false;
      continue;
    }
    if (first >= 0xf0 && first <= 0xf4) {
      if (i + 2 >= length) return false;
      const uint8_t second = data[i++];
      const uint8_t third = data[i++];
      const uint8_t fourth = data[i++];
      if ((third & 0xc0) != 0x80 || (fourth & 0xc0) != 0x80 ||
          (first == 0xf0 ? second < 0x90 || second > 0xbf :
           first == 0xf4 ? second < 0x80 || second > 0x8f :
                           (second & 0xc0) != 0x80)) return false;
      continue;
    }
    return false;
  }
  return true;
}

class Parser {
public:
  Parser(const uint8_t *data, size_t size) : data_(data), size_(size) {}
  bool validInput() const { return data_ != nullptr && size_ <= kMaxJsonSnapshotBytes; }
  void ws() { while (at_ < size_ && (data_[at_] == ' ' || data_[at_] == '\n' || data_[at_] == '\r' || data_[at_] == '\t')) ++at_; }
  bool done() { ws(); return at_ == size_; }
  bool take(char c) { ws(); if (at_ >= size_ || data_[at_] != uint8_t(c)) return false; ++at_; return true; }
  bool key(char *out, size_t capacity) {
    if (!string(out, capacity, true)) return false;
    return take(':');
  }
  bool string(char *out, size_t capacity, bool keyOnly = false) {
    ws();
    if (at_ >= size_ || data_[at_++] != '"' || capacity == 0) return false;
    size_t used = 0;
    while (at_ < size_) {
      uint8_t c = data_[at_++];
      if (c == '"') { out[used] = '\0'; return true; }
      if (c < 0x20 || used + 1 >= capacity) return false;
      if (c == '\\') {
        if (keyOnly) return false;
        if (at_ >= size_) return false;
        const uint8_t escaped = data_[at_++];
        switch (escaped) {
        case '"': case '\\': case '/': c = escaped; break;
        case 'b': c = '\b'; break;
        case 'f': c = '\f'; break;
        case 'n': c = '\n'; break;
        case 'r': c = '\r'; break;
        case 't': c = '\t'; break;
        case 'u': {
          if (at_ + 4 > size_) return false;
          uint16_t code = 0;
          for (uint8_t i = 0; i < 4; ++i) {
            const uint8_t h = data_[at_++];
            code = uint16_t(code << 4);
            if (h >= '0' && h <= '9') code |= h - '0';
            else if (h >= 'a' && h <= 'f') code |= h - 'a' + 10;
            else if (h >= 'A' && h <= 'F') code |= h - 'A' + 10;
            else return false;
          }
          if (code > 0x7f) return false; // v1 command strings are ASCII.
          c = static_cast<uint8_t>(code);
          break;
        }
        default: return false;
        }
      }
      out[used++] = static_cast<char>(c);
    }
    return false;
  }
  bool skipString() {
    ws(); if(at_>=size_ || data_[at_++]!='"') return false;
    while(at_<size_) {
      uint8_t c=data_[at_++];
      if(c=='"') return true;
      if(c<0x20) return false;
      if(c=='\\') {
        if(at_>=size_) return false;
        c=data_[at_++];
        if(c=='u') {
          if(at_+4>size_) return false;
          for(uint8_t i=0;i<4;i++) { const uint8_t h=data_[at_++]; if(!((h>='0'&&h<='9')||(h>='a'&&h<='f')||(h>='A'&&h<='F'))) return false; }
        } else if(c!='"'&&c!='\\'&&c!='/'&&c!='b'&&c!='f'&&c!='n'&&c!='r'&&c!='t') return false;
      }
    }
    return false;
  }
  bool number(uint32_t &value) {
    ws();
    if (at_ >= size_ || data_[at_] < '0' || data_[at_] > '9') return false;
    if (data_[at_] == '0' && at_ + 1 < size_ && data_[at_ + 1] >= '0' && data_[at_ + 1] <= '9') return false;
    value = 0;
    while (at_ < size_ && data_[at_] >= '0' && data_[at_] <= '9') {
      const uint8_t digit = data_[at_++] - '0';
      if (value > (UINT32_MAX - digit) / 10) return false;
      value = value * 10 + digit;
    }
    return true;
  }
  bool boolean(bool &value) {
    ws();
    if (match("true")) { value = true; return true; }
    if (match("false")) { value = false; return true; }
    return false;
  }
  bool skip(uint8_t depth = 0) {
    if (depth > 8) return false;
    ws();
    if (at_ >= size_) return false;
    if (data_[at_] == '"') return skipString();
    if (data_[at_] == '{') {
      ++at_; ws();
      if (take('}')) return true;
      char keys[12][32] = {}; uint8_t count = 0;
      do {
        char current[32];
        if (!key(current, sizeof(current)) || count == 12) return false;
        for (uint8_t i = 0; i < count; ++i) if (strcmp(keys[i], current) == 0) return false;
        strcpy(keys[count++], current);
        if (!skip(depth + 1)) return false;
        if (take('}')) return true;
      } while (take(','));
      return false;
    }
    if (data_[at_] == '[') {
      ++at_; ws();
      if (take(']')) return true;
      do { if (!skip(depth + 1)) return false; if (take(']')) return true; } while (take(','));
      return false;
    }
    uint32_t ignoredNumber;
    bool ignoredBool;
    if (number(ignoredNumber) || boolean(ignoredBool)) return true;
    if (match("null")) return true;
    return false;
  }
  size_t position() const { return at_; }
  size_t size() const { return size_; }
  bool consumeNumber(uint32_t &value) { return number(value); }
  bool consumeBool(bool &value) { return boolean(value); }
  bool consumeSkip() { return skip(); }

private:
  bool match(const char *text) {
    ws(); size_t n = strlen(text);
    if (at_ + n > size_ || memcmp(data_ + at_, text, n) != 0) return false;
    at_ += n; return true;
  }
  const uint8_t *data_;
  size_t size_;
  size_t at_ = 0;
};

enum Field : uint16_t { V = 1, Kind = 2, Min = 4, Max = 8, Epoch = 16,
  Request = 32, Generation = 64, Command = 128, Snapshot = 256,
  Target = 512, Enabled = 1024 };

bool setOnce(uint16_t &seen, Field field) {
  if (seen & field) return false;
  seen |= field;
  return true;
}

class KeySet {
public:
  bool add(const char *key) {
    if (count_ == 16) return false;
    for (uint8_t i = 0; i < count_; ++i) if (strcmp(keys_[i], key) == 0) return false;
    const size_t n = strlen(key);
    if (n >= sizeof(keys_[0])) return false;
    memcpy(keys_[count_++], key, n + 1);
    return true;
  }
private:
  char keys_[16][32] = {};
  uint8_t count_ = 0;
};

bool readString(Parser &p, char *value, size_t capacity) { return p.string(value, capacity); }

class Writer {
public:
  Writer(char *data, size_t capacity) : data_(data), capacity_(capacity) {}
  bool append(const char *s) {
    const size_t n = strlen(s);
    if (n >= capacity_ - used_) return false;
    memcpy(data_ + used_, s, n); used_ += n; data_[used_] = '\0'; return true;
  }
  bool escaped(const char *s, size_t maxLen) {
    size_t length = 0;
    while (length < maxLen && s[length]) ++length;
    if (!validUtf8(reinterpret_cast<const uint8_t *>(s), length)) return false;
    if (!append("\"")) return false;
    for (size_t i = 0; i < length; ++i) {
      const uint8_t c = static_cast<uint8_t>(s[i]);
      char escaped[7] = {};
      if (c == '"' || c == '\\') { escaped[0] = '\\'; escaped[1] = char(c); }
      else if (c < 0x20) snprintf(escaped, sizeof(escaped), "\\u%04X", c);
      else { escaped[0] = char(c); }
      if (!append(escaped)) return false;
    }
    return append("\"");
  }
  bool number(uint32_t n) { char text[11]; snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(n)); return append(text); }
  bool boolean(bool b) { return append(b ? "true" : "false"); }
  size_t finish() const { return used_; }
private:
  char *data_;
  size_t capacity_;
  size_t used_ = 0;
};

const char *certaintyName(Certainty c) {
  switch (c) { case Certainty::Unknown: return "unknown"; case Certainty::Assumed: return "assumed"; case Certainty::Confirmed: return "confirmed"; }
  return "unknown";
}
const char *phaseName(ConnectionPhase p) {
  switch (p) {
  case ConnectionPhase::Disconnected: return "disconnected"; case ConnectionPhase::Discovering: return "discovering";
  case ConnectionPhase::Identifying: return "identifying"; case ConnectionPhase::Synchronizing: return "synchronizing";
  case ConnectionPhase::Ready: return "ready"; case ConnectionPhase::Degraded: return "degraded";
  case ConnectionPhase::Unsupported: return "unsupported"; case ConnectionPhase::FaultBackoff: return "faultBackoff";
  }
  return "disconnected";
}
const char *syncName(SyncState s) {
  switch (s) { case SyncState::Idle: return "idle"; case SyncState::Pending: return "pending"; case SyncState::Confirmed: return "confirmed"; case SyncState::Failed: return "failed"; case SyncState::Interrupted: return "interrupted"; }
  return "idle";
}
const char *resultName(Result r) {
  switch (r) { case Result::Accepted: return "accepted"; case Result::Unsupported: return "unsupported";
  case Result::NotReady: return "notReady"; case Result::QueueFull: return "queueFull";
  case Result::TransportFailed: return "transportFailed"; case Result::TimedOut: return "timedOut";
  case Result::Invalid: return "invalid"; case Result::Interrupted: return "interrupted"; }
  return "invalid";
}
const char *phaseName(OperationPhase p) {
  switch (p) { case OperationPhase::Queued: return "queued"; case OperationPhase::Submitted: return "submitted";
  case OperationPhase::TransportComplete: return "transportComplete"; case OperationPhase::Observed: return "observed";
  case OperationPhase::Failed: return "failed"; }
  return "failed";
}
bool appendLiteral(Writer &w, const char *s) { return w.append("\"") && w.append(s) && w.append("\""); }

} // namespace

JsonStatus decodeHello(const uint8_t *bytes, size_t length, HelloRequest &out) {
  out = {};
  if (length > kMaxJsonCommandBytes) return JsonStatus::TooLarge;
  if (!validUtf8(bytes, length)) return JsonStatus::Malformed;
  Parser p(bytes, length); if (!p.validInput() || !p.take('{')) return JsonStatus::Malformed;
  uint16_t seen = 0; uint32_t v = 0; char kind[16] = {}; KeySet keys;
  do {
    char key[32]; if (!p.key(key, sizeof(key)) || !keys.add(key)) return JsonStatus::Malformed;
    Field f;
    if (!strcmp(key,"v")) f=V; else if (!strcmp(key,"kind")) f=Kind;
    else if (!strcmp(key,"min")) f=Min; else if (!strcmp(key,"max")) f=Max;
    else if (!strcmp(key,"epoch")) f=Epoch; else { if(!p.consumeSkip()) return JsonStatus::Malformed; if(p.take('}')) break; continue; }
    if(!setOnce(seen,f)) return JsonStatus::Malformed;
    if(f==Kind) { if(!readString(p,kind,sizeof(kind))) return JsonStatus::Malformed; }
    else { if(!p.consumeNumber(v)) return JsonStatus::Malformed;
      if((f==Min || f==Max) && v>UINT8_MAX) return JsonStatus::Malformed;
      if(f==V && v!=1) return JsonStatus::VersionMismatch;
      if(f==Min) out.minimumVersion=static_cast<uint8_t>(v);
      if(f==Max) out.maximumVersion=static_cast<uint8_t>(v);
      if(f==Epoch) out.epoch=v;
    }
    if(p.take('}')) break;
    if(!p.take(',')) return JsonStatus::Malformed;
  } while(true);
  if(!p.done() || (seen & (V|Kind|Min|Max|Epoch)) != (V|Kind|Min|Max|Epoch)) return JsonStatus::Malformed;
  if(strcmp(kind,"hello") || out.epoch==0 || out.minimumVersion>1 || out.maximumVersion<1 || out.minimumVersion>out.maximumVersion) return JsonStatus::Unsupported;
  return JsonStatus::Ok;
}

JsonStatus decodeRequest(const uint8_t *bytes, size_t length, UiRequest &out) {
  out = {};
  if (length > kMaxJsonCommandBytes) return JsonStatus::TooLarge;
  if (!validUtf8(bytes, length)) return JsonStatus::Malformed;
  Parser p(bytes,length); if(!p.validInput() || !p.take('{')) return JsonStatus::Malformed;
  uint16_t seen=0; uint16_t commandFields=0; uint32_t number=0;
  char kind[20] = {}; char commandKind[20] = {}; KeySet keys;
  bool targetIsIndex=false;
  do {
    char key[32]; if(!p.key(key,sizeof(key)) || !keys.add(key)) return JsonStatus::Malformed;
    Field f;
    if(!strcmp(key,"v")) f=V; else if(!strcmp(key,"kind")) f=Kind;
    else if(!strcmp(key,"request")) f=Request; else if(!strcmp(key,"epoch")) f=Epoch;
    else if(!strcmp(key,"generation")) f=Generation; else if(!strcmp(key,"command")) f=Command;
    else { if(!p.consumeSkip()) return JsonStatus::Malformed; if(p.take('}')) break; continue; }
    if(!setOnce(seen,f)) return JsonStatus::Malformed;
    if(f==Command) {
      if(!p.take('{')) return JsonStatus::Malformed;
      uint16_t nested=0; KeySet nestedKeys;
      do {
        char nk[32]; if(!p.key(nk,sizeof(nk)) || !nestedKeys.add(nk)) return JsonStatus::Malformed;
        Field nf;
        if(!strcmp(nk,"kind")) nf=Kind;
        else if(!strcmp(nk,"index")||!strcmp(nk,"id")) { nf=Target; targetIsIndex=!strcmp(nk,"index"); }
        else if(!strcmp(nk,"enabled")) nf=Enabled;
        else { if(!p.consumeSkip()) return JsonStatus::Malformed; if(p.take('}')) break; continue; }
        if(!setOnce(nested,nf)) return JsonStatus::Malformed;
        if(nf==Kind) { if(!readString(p,commandKind,sizeof(commandKind))) return JsonStatus::Malformed; }
        else if(nf==Target) { if(!p.consumeNumber(number)||number>UINT16_MAX) return JsonStatus::Malformed; out.target=static_cast<uint16_t>(number); }
        else if(!p.consumeBool(out.enabled)) return JsonStatus::Malformed;
        if(p.take('}')) break;
        if(!p.take(',')) return JsonStatus::Malformed;
      } while(true);
      commandFields=nested;
    } else if(f==Kind) { if(!readString(p,kind,sizeof(kind))) return JsonStatus::Malformed; }
    else {
      if(!p.consumeNumber(number)) return JsonStatus::Malformed;
      if(f==V && number!=1) return JsonStatus::VersionMismatch;
      if(f==Request) out.requestId=number; else if(f==Epoch) out.epoch=number;
      else if(f==Generation) out.generation=number;
    }
    if(p.take('}')) break;
    if(!p.take(',')) return JsonStatus::Malformed;
  } while(true);
  if(!p.done() || (seen & (V|Kind|Request|Epoch|Generation)) != (V|Kind|Request|Epoch|Generation)) return JsonStatus::Malformed;
  if(out.requestId==0) return JsonStatus::Malformed;
  if(!strcmp(kind,"snapshot")) { out.snapshot=true; return (seen & Command) ? JsonStatus::Malformed : JsonStatus::Ok; }
  if(strcmp(kind,"command") || !(seen & Command)) return JsonStatus::Unsupported;
  if(!strcmp(commandKind,"setPreset")) {
    if(!(commandFields & Target) || (commandFields & Enabled) || !targetIsIndex || out.target>=kMaxPresets) return JsonStatus::Malformed;
    out.command=CommandKind::SelectPreset;
  } else if(!strcmp(commandKind,"setBlock")) {
    if((commandFields & (Target|Enabled)) != (Target|Enabled) || targetIsIndex) return JsonStatus::Malformed;
    out.command=CommandKind::SetBlock;
  } else if(!strcmp(commandKind,"setTuner")) {
    if(!(commandFields & Enabled) || (commandFields & Target)) return JsonStatus::Malformed;
    out.command=CommandKind::SetTuner;
  } else return JsonStatus::Unsupported;
  return JsonStatus::Ok;
}

size_t encodeSnapshot(const ReadSnapshot &s, uint32_t epoch, bool bridgeConnected,
                      char *out, size_t capacity, const char *deviceId) {
  if (!out || capacity == 0) return 0;
  out[0] = '\0';
  Writer w(out, capacity);
  bool ok=w.append("{\"v\":1,\"kind\":\"snapshot\",\"epoch\":")&&w.number(epoch)&&
    w.append(",\"generation\":")&&w.number(s.generation)&&w.append(",\"revision\":")&&w.number(s.revision)&&
    w.append(",\"bridge\":")&&appendLiteral(w,bridgeConnected?"connected":"disconnected")&&
    w.append(",\"device\":")&&w.escaped(deviceId ? deviceId : "unknown", 32)&&
    w.append(",\"pedal\":{\"phase\":")&&appendLiteral(w,phaseName(s.connection))&&
    w.append(",\"sync\":")&&appendLiteral(w,syncName(s.sync))&&w.append(",\"syncResult\":")&&appendLiteral(w,resultName(s.syncResult))&&
    w.append("},\"bank\":")&&w.number(s.localBank)&&w.append(",\"capabilities\":{\"presetCount\":")&&w.number(s.capabilities.presetCount)&&
    w.append(",\"presetReadback\":")&&w.boolean(s.capabilities.presetReadback)&&w.append(",\"tuner\":")&&w.boolean(s.capabilities.tuner)&&w.append(",\"blocks\":[");
  for(uint8_t i=0;ok&&i<s.capabilities.blockCount;i++) {
    const auto &c=s.capabilities.blocks[i]; if(i) ok=w.append(",");
    ok=ok&&w.append("{\"id\":")&&w.number(c.id)&&w.append(",\"label\":")&&w.escaped(c.label,kMaxBlockLabel)&&
      w.append(",\"set\":true,\"readback\":")&&w.boolean(c.readback)&&w.append("}");
  }
  ok=ok&&w.append("]},\"preset\":{\"index\":")&&w.number(s.preset.index)&&w.append(",\"certainty\":")&&appendLiteral(w,certaintyName(s.preset.certainty))&&w.append(",\"name\":");
  const char *currentName=s.preset.index<kMaxPresets?s.presetNames[s.preset.index]:"";
  ok=ok&&w.escaped(currentName,kMaxPresetName)&&w.append("},\"visiblePresets\":[");
  const uint16_t first=static_cast<uint16_t>((s.localBank*4u));
  for(uint8_t slot=0;ok&&slot<4;slot++) {
    const uint16_t index=first+slot; if(slot) ok=w.append(","); char label[8]={};
    if(s.capabilities.presetSlotLayout==PresetSlotLayout::ZeroBasedP) snprintf(label,sizeof(label),"P%02u",index);
    else if(s.capabilities.presetSlotLayout==PresetSlotLayout::PocketPresetAndEffect) snprintf(label,sizeof(label),index<50?"P%02u":"F%02u",index<50?index+1:index-49);
    else snprintf(label,sizeof(label),"%u",index);
    const char *name=index<kMaxPresets?s.presetNames[index]:"";
    ok=ok&&w.append("{\"index\":")&&w.number(index)&&w.append(",\"label\":")&&w.escaped(label,sizeof(label))&&w.append(",\"name\":")&&w.escaped(name,kMaxPresetName)&&w.append("}");
  }
  ok=ok&&w.append("],\"tuner\":{\"enabled\":")&&w.boolean(s.tuner.value)&&w.append(",\"certainty\":")&&appendLiteral(w,certaintyName(s.tuner.certainty))&&w.append("},\"blocks\":[");
  for(uint8_t i=0;ok&&i<s.capabilities.blockCount;i++) {
    const auto &b=s.blocks[i]; if(i) ok=w.append(",");
    ok=ok&&w.append("{\"id\":")&&w.number(b.id)&&w.append(",\"enabled\":")&&w.boolean(b.enabled.value)&&w.append(",\"certainty\":")&&appendLiteral(w,certaintyName(b.enabled.certainty))&&w.append(",\"effect_name\":")&&w.escaped(b.modelName,kMaxBlockLabel)&&w.append(",\"model_certainty\":")&&appendLiteral(w,certaintyName(b.modelCertainty))&&w.append("}");
  }
  ok=ok&&w.append("]}");
  return ok && w.finish()<=kMaxJsonSnapshotBytes ? w.finish() : 0;
}

size_t encodeResult(const RequestResult &r, char *out, size_t capacity) {
  if (!out || capacity == 0) return 0;
  out[0] = '\0';
  Writer w(out, capacity);
  const bool ok=w.append("{\"v\":1,\"kind\":\"result\",\"request\":")&&w.number(r.requestId)&&
    w.append(",\"epoch\":")&&w.number(r.uiEpoch)&&w.append(",\"generation\":")&&w.number(r.generation)&&
    w.append(",\"admission\":")&&appendLiteral(w,resultName(r.admission))&&w.append(",\"operation\":")&&w.number(r.operation)&&
    w.append(",\"phase\":")&&appendLiteral(w,phaseName(r.phase))&&w.append(",\"result\":")&&appendLiteral(w,resultName(r.result))&&w.append("}");
  return ok && w.finish()<=kMaxJsonCommandBytes ? w.finish() : 0;
}

} // namespace stomp::ui
