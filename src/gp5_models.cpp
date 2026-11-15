#include "gp5_models.h"

namespace {

struct ModelName {
  uint32_t id;
  const char *name;
};

constexpr ModelName kDriveModels[] = {
    {0x00000003, "Overdrive"},       {0x02000003, "Distortion"},
    {0x06000003, "Metal"},           {0x2a000003, "Crunch"},
    {0x29000003, "Blues"},           {0x30000003, "Classic"},
    {0x2b000003, "Bass Overdrive"},  {0x22000003, "Fuzz"},
    {0x24000003, "Muff Fuzz"},       {0x40000003, "Bass Distortion"},
};

constexpr ModelName kDelayModels[] = {
    {0x0000000b, "Analog Delay"},    {0x0100000b, "Digital Delay"},
    {0x0500000b, "Slap Delay"},      {0x0d00000b, "Tape Echo"},
    {0x0200000b, "Mod Delay"},       {0x0b00000b, "Reverse Delay"},
    {0x1300000b, "Lo-Fi Delay"},     {0x0900000b, "Filter Delay"},
    {0x0600000b, "Sweep Delay"},     {0x0400000b, "Ping Pong Delay"},
};

constexpr ModelName kReverbModels[] = {
    {0x0b00000c, "Room"},       {0x0000000c, "Hall"},
    {0x0100000c, "Plate"},      {0x0200000c, "Church"},
    {0x1000000c, "Spring"},     {0x0f00000c, "Studio"},
    {0x0400000c, "Shimmer"},    {0x0600000c, "Reverse"},
    {0x0700000c, "Arena"},      {0x1500000c, "Mod Reverb"},
};

template <size_t Size>
const char *findModel(const ModelName (&models)[Size], uint32_t modelId) {
  for (const ModelName &model : models) {
    if (model.id == modelId) {
      return model.name;
    }
  }
  return nullptr;
}

} // namespace

const char *gp5EffectModelName(uint8_t blockIndex, uint32_t modelId) {
  switch (blockIndex) {
  case 2:
    return findModel(kDriveModels, modelId);
  case 7:
    return findModel(kDelayModels, modelId);
  case 8:
    return findModel(kReverbModels, modelId);
  default:
    return nullptr;
  }
}
