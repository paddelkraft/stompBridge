#include "pedal_plugin.h"

#include "gp5_plugin.h"
#include "pocket_master_plugin.h"

PedalPlugin *findPedalPlugin(const MidiDeviceIdentity &identity) {
  static Gp5Plugin gp5;
  static PocketMasterPlugin pocketMaster;
  if (gp5.matches(identity)) {
    return &gp5;
  }
  if (pocketMaster.matches(identity)) {
    return &pocketMaster;
  }
  return nullptr;
}

const char *pedalCertaintyName(PedalCertainty certainty) {
  switch (certainty) {
  case PedalCertainty::Assumed:
    return "assumed";
  case PedalCertainty::Confirmed:
    return "confirmed";
  case PedalCertainty::Unknown:
  default:
    return "unknown";
  }
}
