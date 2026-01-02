SRIX / ST25TB Tags – Technical Overview
Note: This document summarizes and reformulates information from public STMicroelectronics datasheets and related technical material for SRIX4K/SRIX512 and their ST25TB successors.
​

1. Overview
SRIX and ST25TB devices (e.g., SRIX512, SRIX4K, ST25TB512, ST25TB04K) are contactless RFID memory chips designed by STMicroelectronics for short‑range applications at 13.56 MHz. They are passive devices powered entirely by the reader’s RF field and provide a small, robust EEPROM along with features for anti-collision and basic security.
​

These chips implement the RF and protocol layers of ISO/IEC 14443 Type B, enabling compatibility with standard NFC/RFID readers that support this mode.
​

Typical use cases include:

Transport tickets and disposable passes

Access control badges

Prepaid counters (e.g., credits, usage counts)

Simple asset tracking and configuration storage
​

2. RF Interface and Protocol
2.1 Operating Conditions
Carrier frequency: 13.56 MHz

Data rate: 106 kbit/s (both directions)

Reader → Tag modulation: ASK (Amplitude Shift Keying), typically 10% modulation depth

Tag → Reader modulation: Load modulation with BPSK (Binary Phase Shift Keying) subcarrier at 847 kHz

Operating distance: Up to about 10 cm with appropriate antenna and reader power, compatible with ISO 14443 Type B requirements.
​

The tag rectifies energy from the RF field and uses it to power internal logic and EEPROM operations, without any internal battery.
​

2.2 Standards Compliance
SRIX and ST25TB devices support:

ISO/IEC 14443-2 Type B for the physical and RF layer

ISO/IEC 14443-3 Type B for initialization, anti-collision, and protocol.
​

3. Memory Organization
Most SRIX/ST25TB parts used in practice come in two main sizes:

512-bit devices (e.g., SRIX512 / ST25TB512)

4096-bit devices (e.g., SRIX4K / ST25TB04K)
​

The discussion below focuses on the 4 Kbit (4096-bit = 512 bytes) class, which is typical for SRIX4K/ST25TB04K.

3.1 High-Level Layout (4 Kbit devices)
The 4 Kbit parts expose a memory space of 128 blocks of 4 bytes each, for a total of 512 bytes.
​

A typical logical layout is:

Block Range	Purpose	Size	Notes
0x00–0x04	Resettable OTP area	5 blocks (20 B)	One‑time programmable bits, resettable in bulk via counter mechanism.
0x05–0x06	Binary counters	2 blocks (8 B)	32‑bit down‑counters with anti‑tearing protection.
0x07–0x7F	User EEPROM	121 blocks (484 B)	General-purpose read/write area. Sections may be permanently locked.
The device also contains system data such as a 64‑bit UID and lock registers, stored in dedicated internal locations that are not all mapped directly as normal blocks.
​

3.2 User EEPROM Area
Total size: 484 bytes (121 blocks × 4 bytes).

Address range: typically blocks 0x07 to 0x7F.

Each block is written as a 32‑bit word (4 consecutive bytes).

EEPROM writes are auto‑erase: the previous content of the block is erased before the new 32‑bit word is programmed.
​

Some blocks (commonly in the low part of the user range, e.g. 0x07–0x0F) can be configured as write‑protected by setting corresponding bits in a lock register. Once locked, they can no longer be modified.
​

4. Special Memory Areas
4.1 Resettable OTP Area (Blocks 0–4)
Blocks 0x00–0x04 form a resettable One‑Time Programmable (OTP) zone:
​

Individual bits can only be changed from 1 to 0 when written.

A special “reload” mechanism, controlled by one of the counters, can reset the entire OTP zone back to all 1 (i.e., 0xFFFFFFFF per block).

The number of allowed global reload operations is capped; the reload counter itself is stored in a protected location and decremented at each OTP reset cycle.
​

Typical uses: feature enable flags, ticket life-cycle markers, or other application states that generally only move in one direction, with the option of a limited number of factory/maintenance resets.

4.2 Binary Counters (Blocks 5–6)
One or two 32‑bit binary counters are provided, generally mapped at blocks 0x05 and 0x06:
​

They behave as down‑counters: application logic must only program a strictly lower numerical value than the current content.

Anti‑tearing logic ensures that if power is lost during an update, the counter remains at a consistent value (either the old or the new one, never a corrupted intermediate value).

One counter is typically used as a general-purpose application credit counter (e.g., remaining rides, token balance).

Another counter may be connected internally to the resettable OTP area as a reload‑limiter (for example, limiting the number of times OTP can be globally reset).
​

This makes SRIX/ST25TB well suited to prepaid, decrement‑only scenarios (e.g., stored‑value tickets).

4.3 Unique Identifier (UID) and Locks
Each tag includes a 64‑bit UID (Unique Identifier) programmed at the factory:
​

The UID is read-only and globally unique.

It is typically used for tag identification, anti-cloning checks, or as a key/seed in higher‑level application protocols.

The chip also incorporates internal lock registers:

These may include bits that permanently lock specific EEPROM blocks (e.g., the first user blocks), making them read‑only for the rest of the device’s life.

Some lock bits are themselves OTP: once set, they cannot be cleared.
​

5. Command Set and State Machine
5.1 Basic Command Set (ISO 14443 Type B style)
A typical SRIX/ST25TB‑class device supports a compact command set including:
​

Inventory and Anti-Collision commands

Initiate() – Start the inventory process; tags respond with a random Chip_ID.

Pcall16() – Open a 16‑slot anti-collision sequence.

Slot_marker(n) – Select a specific collision slot (0–15) to identify tags.

Select(Chip_ID) – Select one tag by its Chip_ID for further operations.

Memory access commands

Read_block(addr) – Read a 32‑bit block at address addr.

Write_block(addr, data) – Write a 32‑bit word data to block addr.

UID and control commands

Get_UID() – Retrieve the 64‑bit unique identifier.

Completion() / Halt – Deactivate or gracefully end the session.

Reset_to_inventory() – Return the tag to inventory state.

The exact opcodes and frame formats follow ISO 14443 Type B framing combined with ST’s application-level command definitions.
​

5.2 Anti-Collision Principle
The anti-collision mechanism is based on a 16-slot scheme with a random Chip_ID generated by each tag:
​

After Initiate(), each tag in the RF field computes an 8‑bit Chip_ID.

Part of this value defines a slot number (0–15).

The reader walks through slots using Pcall16() and Slot_marker(n) commands.

In each slot, only tags whose Chip_ID maps to that slot are allowed to answer.

Once a tag is identified, the reader sends Select(Chip_ID) to put it into a selected state.

This approach allows multiple tags to coexist in the field and be addressed one at a time.

5.3 Tag States
The internal state machine of a SRIX/ST25TB tag typically includes:
​

Power-Off: No field present; tag unpowered.

Ready / Inventory: Field present; tag waiting for inventory commands.

Selected: Tag has been selected and will respond to memory commands.

Deselected / Deactivated: Tag is ignored until reset or until the reader sends specific commands to re-enter inventory.

The reader cycles tags between these states using the inventory and selection commands described above.

6. Reliability and Endurance
SRIX/ST25TB devices are designed as long‑life EEPROM storage for contactless applications:
​

Endurance:

Up to 1,000,000 write/erase cycles per EEPROM cell (typical datasheet value).

Data retention:

Up to 40 years at recommended operating conditions.

Programming time:

Typical 5 ms per 32‑bit block (varies by product and conditions).

These characteristics make them suitable for scenarios with frequent small updates (e.g., decrementing counters, updating a small record), as long as wear‑leveling and write distribution are considered in application design.

7. Application Patterns
SRIX/ST25TB tags are usually integrated into systems that combine RF interface, memory layout, and application‑level logic:

Ticketing / Transport

UID used as card ID.

Counters as remaining rides or time units.

OTP bits as lifecycle markers (used, expired, blacklisted, etc.).
​

Access Control

UID and a small credential record stored in user EEPROM.

Lock bits to freeze credential data after personalization.

Configuration and Asset Tags

User EEPROM holds serial numbers, configuration words, calibration data.

Lock bits ensure read-only behavior in the field after device provisioning.

Because the chip itself has relatively simple security primitives (OTP bits, counters, lock registers), higher‑level cryptographic authentication is typically implemented at system level, often using the UID as an input to external security logic.

8. Summary
SRIX and ST25TB tags are compact ISO 14443 Type B contactless EEPROMs with:

512 bytes total memory organized as 128 × 4‑byte blocks in the 4 Kbit variants, including special OTP, counter, and user areas.

Dedicated down‑counters with anti‑tearing for prepaid / credit‑based applications.

A 64‑bit factory‑programmed UID for unique identification.

Lock mechanisms to permanently protect selected blocks.

A simple and robust RF interface at 13.56 MHz following ISO 14443 Type B.
​

These properties make SRIX/ST25TB devices a good fit for low‑complexity, high‑volume contactless applications where deterministic behavior and robustness matter more than advanced cryptography.
