// Field offsets used by the mod. Every value here is verified against
// work/il2cpp-dump/dump.cs by tools/check_offsets.py.
//
// When the game updates, re-run Il2CppDumper and then `python tools/check_offsets.py`;
// it fails loudly if any offset moved.
#pragma once
#include <cstddef>

namespace off {

// ---- object layout ----
constexpr size_t kObjectHeader = 0x10;   // Il2CppObject: klass + monitor
constexpr size_t kArrayData = 0x20;      // Il2CppArray: header + bounds + max_length

// ---- CardController (TypeDefIndex 109) ----
constexpr size_t kCardControllerCardData = 0x90;   // <CardData>k__BackingField (Card)
constexpr size_t kCardControllerSection = 0x98;    // boardSection (BazaarBoard.ESections)

// ---- Card (TypeDefIndex 22413) ----
constexpr size_t kCardTemplateId = 0x18;  // <TemplateId>k__BackingField (Guid)
constexpr size_t kCardSize = 0x40;        // <Size>k__BackingField (ECardSize)
constexpr size_t kCardTier = 0x50;        // <Tier>k__BackingField (ETier)

// ---- CardTooltipData (TypeDefIndex 2813) ----
constexpr size_t kTooltipCardInstance = 0x10;  // _cardInstance (Card)
constexpr size_t kTooltipCardTemplate = 0x18;  // _cardTemplate (ITCard)

// ---- UnityEngine.Touch (TypeDefIndex 24035) ----
constexpr size_t kTouchPosition = 0x04;  // m_Position (Vector2)
constexpr size_t kTouchPhase = 0x24;     // m_Phase (TouchPhase)

}  // namespace off
