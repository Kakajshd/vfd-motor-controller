#pragma once

#include <stdint.h>

struct VfdParamWrite
{
  uint16_t reg;
  uint16_t value;
  const char *code;
};

enum VfdProfile : uint8_t
{
  VFD_PROFILE_MANUAL = 0,
  VFD_PROFILE_AUTO = 1,
};

static const VfdParamWrite VFD_PROFILE_PARAMS_MANUAL[] = {
    //{0x0100, 50, "01.00"},
    {0x0200, 1, "02.00"},
    {0x0201, 2, "02.01"},
    //{0x0205, 1,  "02.05"},
    //{0x0209, 0,  "02.09"},
    //{0x0900, 1,  "09.00"},
    //{0x0901, 1,  "09.01"},
    //{0x0904, 0,  "09.04"},
};

static const VfdParamWrite VFD_PROFILE_PARAMS_AUTO[] = {
    //{0x0100, 150, "01.00"},
    {0x0200, 3, "02.00"},
    {0x0201, 1, "02.01"},
    //{0x0205, 2,   "02.05"},
    //{0x0209, 3,   "02.09"},
    //{0x0900, 1,   "09.00"},
    //{0x0901, 1,   "09.01"},
    //{0x0904, 6,   "09.04"},
};

static constexpr size_t VFD_PROFILE_PARAMS_MANUAL_COUNT = sizeof(VFD_PROFILE_PARAMS_MANUAL) / sizeof(VFD_PROFILE_PARAMS_MANUAL[0]);
static constexpr size_t VFD_PROFILE_PARAMS_AUTO_COUNT = sizeof(VFD_PROFILE_PARAMS_AUTO) / sizeof(VFD_PROFILE_PARAMS_AUTO[0]);
