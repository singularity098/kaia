#pragma once

#include "../Engine/SampleData.h"

namespace kaia
{

// True if the data looks like an Akai MPC2000/MPC2000XL (01 04) or MPC3000 (01 02) .SND file.
bool isMpcSnd (const void* data, size_t size);

// Decodes an MPC .SND sound (16-bit PCM with a small header) into `out`.
bool decodeMpcSnd (const void* data, size_t size, SampleData& out, juce::String& error);

}
