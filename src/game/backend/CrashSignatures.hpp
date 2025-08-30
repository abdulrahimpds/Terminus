#pragma once
#include <unordered_set>
#include <cstdint>
#include <set>
#include <tuple>
#include <mutex>
#include <map>
#include <unordered_map>
#include <chrono>
#include "common.hpp"

namespace YimMenu::CrashSignatures
{
	// crash signature database - known corrupted memory addresses from crash logs
	// these addresses have been confirmed to cause crashes when passed to RDR2 natives
	static const std::unordered_set<uintptr_t> KnownCrashAddresses = {
		// from most_relevant-cout.log
		0x24FC7516440,  // lines 161, 216, 271, 326, 381, 436, 491
		0x24FC751644A,  // lines 217, 272, 327, 382, 437
		0xF089413058,   // line 51
		0xF089412FC8,   // line 106
		
		// from cout2.log
		0xFFFFFFFFFFFFFFFF, // lines 107, 162, 256, 311, 495, 550 - most common crash signature
		0x90042,        // line 52
		0x10DE38DEB8,   // line 218
		0x10DE38DE28,   // line 273
		0x144E21D28,    // line 328
		0x144E21C98,    // line 383
		
		// from cout3.log
		0x1A600008AFC,  // line 376
		0x1A600008A6C,  // line 422
		
		// from cout4.log
		0x40,           // line 52
		0x42,           // lines 107, 163, 273, 328, 383
		0x43,           // line 218
		0x72B8,         // line 415
		0x108C,         // line 446
		0x23E8,         // line 478
		0x9C,           // line 510
		0x2440,         // lines 543, 644, 678
		0x2430,         // lines 576, 610
		0x2BD8,         // line 712
		0x2C10,         // line 746
		0x70,           // lines 778, 810

		// new signatures from recent crash logs (2024-01-XX session)
		0x0,            // null pointer access - very common crash pattern
		0x10,           // small offset access - common in multiple crashes
		0x7FF7000000CD, // corrupted memory access - successfully prevented by exception handler

		// new signatures from crash that bypassed exception handler
		0x8000098,      // large offset crash - caused actual game crash
		0x8000008,      // large offset crash - caused actual game crash
		0x8000000,      // base address pattern - prevent similar attacks

		// new signatures from HandleCloneSync crash (2025-08-03)
		0xE09D562228,   // corrupted pointer in HandleCloneSync - attempted to read from invalid memory
		0xE09D562198,   // corrupted pointer in HandleCloneSync - attempted to read from invalid memory

		// new signature from TpAllToWaypoint crash (2025-08-03)
		0x46,           // small offset crash - attempted to read from 0x46 (null pointer + offset)

		// from cout6.log - comprehensive crash signature collection
		0x20,           // small offset access pattern
		0xC78E2FF270,   // corrupted high memory address
		0xC0,           // small offset access
		0x30,           // small offset access - very common
		0x24,           // small offset access
		0xA6,           // small offset access
		0xA8,           // small offset access
		0xAC,           // small offset access
		0x4,            // very small offset access
		0x8,            // very small offset access
		0x18,           // small offset access
		0x38,           // small offset access
		0x3C,           // small offset access
		0x120,          // medium offset access
		0x2C,           // small offset access
		0x28,           // small offset access
		0x34,           // small offset access
		0x44,           // small offset access
		0x14,           // small offset access
		0x1C,           // small offset access
		0x48,           // small offset access
		0x50,           // small offset access
		0x60,           // small offset access
		0xB8,           // medium offset access
		0xE0,           // medium offset access
		0xCCAEEFD6D0,   // corrupted high memory address
		0xC44B2822DC,   // corrupted high memory address
		0xC6AF0DB600,   // corrupted high memory address
		0xCCB50DD9D8,   // corrupted high memory address

		0x100815F12F4, // from crash log 2025-08-03 - attempted to read from invalid memory causing crash
		0x100815F1264, // from crash log 2025-08-03 - attempted to read from invalid memory causing crash

		0x248,  // from crash log 2025-08-03 – attempted to read from 0x248 causing game termination
		0x1b8,  // from crash log 2025-08-03 – attempted to read from 0x1b8 causing game termination

		0x1A0B8AE88C8, // from crash log 2025-08-03 - attempted to read from 0x1A0B8AE88C8 causing game termination
		0x1A0B8AE8838, // from crash log 2025-08-03 - attempted to read from 0x1A0B8AE8838 causing game termination
		0x2949,        // from crash log 2025-08-03 - attempted to execute at 0x2949 causing game termination

		// new signatures from HandleCloneSync.cpp and ReceiveNetMessage.cpp crashes
		0xE09D562228,  // from HandleCloneSync.cpp L:30 crash - attempted to read from invalid memory
		0xE09D562198,  // from ReceiveNetMessage.cpp L:272 crash - attempted to read from invalid memory

		// new signature from BringPlayer spam-click crash (2025-08-03)
		0xC08,         // from BringPlayer crash - attempted to read from 0xC08 (null pointer + offset) in TeleportPlayerToCoords

		// new signatures from LogSyncNode crash (2025-08-03) - friend's crash log
		0x1A0F20C9018, // from LogSyncNode crash - attempted to read from invalid memory in network sync
		0x1A0F20C8F88, // from LogSyncNode crash - attempted to read from invalid memory in network sync (second crash)

		// new signatures from Nemesis attack crash (2025-08-03) - sophisticated modder attack
		0x97,          // from Nemesis attack - attempted to read from 0x97 (null pointer + offset) in HandleCloneSync
		0x7,           // from Nemesis attack - attempted to read from 0x7 (null pointer + offset) in HandleCloneSync
		0x1A02C8503B8, // from Nemesis attack - attempted to read from 0x1A02C8503B8 (corrupted pointer) in HandleCloneSync
		0x1A02C850328, // from Nemesis attack - attempted to read from 0x1A02C850328 (corrupted pointer) in HandleCloneSync
		0x1A02C7DFCD8, // from Nemesis attack - attempted to read from 0x1A02C7DFCD8 (corrupted pointer) in HandleCloneSync

		// common spectate crash signatures (patterns for hidden/invisible modders)
		0xFFFFFFFF,    // invalid entity handle - common in spectate crashes
		0xDEADBEEF,    // corrupted entity pointer - common debug pattern used by modders
		0xCCCCCCCC,    // uninitialized memory pattern - common in entity corruption
		0xFEEEFEEE,    // heap corruption pattern - common in spectate entity crashes
		0xABABABAB,    // heap free pattern - common when spectating deleted entities

		// new signature from Nemesis delayed crash attack (2025-08-03)
		0xFFFFFF0000000039, // from Nemesis attack - attempted to write to crafted invalid address after task fuzzer blocks
		0xFFFFFF0000000000, // base pattern for Nemesis memory corruption attacks
		0xFFFFFF00,         // partial pattern for similar attacks

		// new signatures from final-cout.log sequential crash attack (2025-08-03)
		0x1A0D8B35A78, // from HandleCloneSync crash - corrupted pointer attack from Dwight-Drury
		0x1A0D8B359E8, // from HandleCloneSync crash - sequential corrupted pointer (144 bytes offset)

		// add new crash signatures here as they are discovered
		// format: 0x1234567890, // from crash log file - description
	};
	
	// check if a memory address is known to cause crashes
	inline bool IsKnownCrashAddress(uintptr_t address)
	{
		return KnownCrashAddresses.find(address) != KnownCrashAddresses.end();
	}
	
	// check if a pointer is known to cause crashes
	inline bool IsKnownCrashPointer(void* ptr)
	{
		if (!ptr) return false; // null pointers are handled elsewhere
		return IsKnownCrashAddress(reinterpret_cast<uintptr_t>(ptr));
	}
	
	// check if an entity handle corresponds to a known crash address
	inline bool IsKnownCrashHandle(int handle)
	{
		if (handle <= 0) return false; // invalid handles are handled elsewhere
		return IsKnownCrashAddress(static_cast<uintptr_t>(handle));
	}

	// intelligent pattern detection - helper functions for attack pattern analysis
	inline bool IsCommonCorruptionPattern(uintptr_t address)
	{
		// detect common memory corruption patterns
		if (address == 0xFFFFFFFF || address == 0xDEADBEEF ||
		    address == 0xCCCCCCCC || address == 0xFEEEFEEE ||
		    address == 0xABABABAB)
		{
			return true;
		}

		// detect repeating byte patterns (common in corruption)
		uint8_t byte = address & 0xFF;
		if (((address >> 8) & 0xFF) == byte &&
		    ((address >> 16) & 0xFF) == byte &&
		    ((address >> 24) & 0xFF) == byte)
		{
			return true;
		}

		return false;
	}

	inline bool IsHighAddressCorruption(uintptr_t address)
	{
		// more precise high-address corruption detection
		// based on Windows x64 memory layout research (user space limit: 0x7FFFFFFFFFFF)

		// detect specific attack patterns in heap corruption range (like 0x1A0... series from crash logs)
		if (address >= 0x1A0000000000 && address <= 0x1AFFFFFFFFFFFF)
		{
			// additional validation: check if address looks like corrupted heap pointer
			// expert recommendation: heap pointers should be reasonably aligned
			if ((address & 0x7) != 0) // not 8-byte aligned - likely corruption
			{
				return true;
			}

			// check for suspicious bit patterns in high addresses (common in attacks)
			uint64_t highBits = (address >> 32) & 0xFFFFFFFF;
			if (highBits == 0x1A0D8B35 || // exact pattern from final-cout.log
			    highBits == 0x1A0F20C9 || // pattern from previous crashes
			    highBits == 0x1A02C850)   // pattern from Nemesis attacks
			{
				return true;
			}
		}

		// detect addresses above Windows x64 user space limit
		// Windows x64 user space: 0x0000000000000000 to 0x00007FFFFFFFFFFF (128TB)
		if (address > 0x00007FFFFFFFFFFF)
		{
			// exclude known valid kernel/system addresses to reduce false positives
			if (address != 0x7FFFFFFFFFFFFFFC && // valid system address
			    address != 0xFFFFFFFFFFFFFFFF)   // valid sentinel value
			{
				return true;
			}
		}

		return false;
	}

	inline bool IsLowAddressOffset(uintptr_t address)
	{
		// detect low-address offsets (null pointer + small offset attacks)
		// based on AddressSanitizer and security research best practices

		if (address > 0 && address < 0x10000) // first 64KB - standard protection range
		{
			// expert recommendation: add additional validation for common attack patterns
			// check for specific offsets commonly used in attacks
			if (address == 0xC08 ||  // from BringPlayer crash
			    address == 0x97 ||   // from Nemesis attack
			    address == 0x7 ||    // from Nemesis attack
			    address == 0x46 ||   // from AntiLasso crash
			    address < 0x1000)    // first 4KB - most critical range
			{
				return true;
			}

			// expert recommendation: check for suspicious alignment patterns
			// legitimate small addresses are usually well-aligned
			if ((address & 0x3) == 0 && address < 0x8000) // 4-byte aligned in lower 32KB
			{
				return true; // likely structure member offset attack
			}
		}

		return false;
	}

	// additional validation functions for comprehensive protection
	inline bool IsValidPointerAlignment(uintptr_t address)
	{
		// expert recommendation: most valid pointers on x64 are 8-byte aligned
		// unaligned pointers in critical ranges are often corruption
		if (address < 0x10000 || address > 0x100000000000) // critical ranges
		{
			return (address & 0x7) == 0; // must be 8-byte aligned
		}
		return true; // less strict for normal address ranges
	}

	inline bool HasSuspiciousBitPattern(uintptr_t address)
	{
		// expert recommendation: detect bit patterns common in memory corruption
		// check for repeating nibbles (common in corruption)
		uint8_t nibble = address & 0xF;
		uint64_t pattern = nibble | (nibble << 4) | (nibble << 8) | (nibble << 12);
		if ((address & 0xFFFF) == pattern)
		{
			return true;
		}

		// check for alternating bit patterns (common in attacks)
		if ((address & 0xFFFFFFFF) == 0xAAAAAAAA ||
		    (address & 0xFFFFFFFF) == 0x55555555)
		{
			return true;
		}

		return false;
	}

	// main intelligent pattern detection algorithm (expert-enhanced)
	inline bool IsLikelyAttackPattern(void* ptr)
	{
		if (!ptr) return true; // null pointers are always suspicious

		uintptr_t address = reinterpret_cast<uintptr_t>(ptr);

		// expert recommendation: early validation for obviously invalid addresses
		if (HasSuspiciousBitPattern(address))
		{
			LOG(WARNING) << "Detected suspicious bit pattern: 0x" << std::hex << address;
			return true;
		}

		// first check exact matches (fastest path)
		if (IsKnownCrashAddress(address))
		{
			return true;
		}

		// pattern 1: sequential attack detection (like final-cout.log crashes)
		// limit search to prevent performance impact
		for (const auto& signature : KnownCrashAddresses)
		{
			uintptr_t diff = (address > signature) ? (address - signature) : (signature - address);
			if (diff > 0 && diff <= 1024) // within 1KB range
			{
				LOG(WARNING) << "Detected sequential attack pattern: 0x" << std::hex << address
				            << " (offset " << diff << " from known signature 0x" << signature << ")";
				return true;
			}
		}

		// pattern 2: common corruption patterns
		if (IsCommonCorruptionPattern(address))
		{
			LOG(WARNING) << "Detected common corruption pattern: 0x" << std::hex << address;
			return true;
		}

		// pattern 3: high-address corruption (like 0x1A0... patterns)
		if (IsHighAddressCorruption(address))
		{
			LOG(WARNING) << "Detected high-address corruption pattern: 0x" << std::hex << address;
			return true;
		}

		// pattern 4: low-address null pointer offsets (like 0xC08, 0x97, 0x7)
		if (IsLowAddressOffset(address))
		{
			LOG(WARNING) << "Detected low-address offset pattern: 0x" << std::hex << address;
			return true;
		}

		// expert recommendation: final alignment check for critical addresses
		if (!IsValidPointerAlignment(address))
		{
			LOG(WARNING) << "Detected invalid pointer alignment: 0x" << std::hex << address;
			return true;
		}

		return false;
	}

	// performance-optimized enhanced crash pointer detection
	inline bool IsKnownCrashPointerEnhanced(void* ptr)
	{
		// expert recommendation: fast path for null pointers
		if (!ptr) return true;

		uintptr_t address = reinterpret_cast<uintptr_t>(ptr);

		// expert recommendation: ultra-fast checks first (single comparisons)
		// check for obviously invalid addresses that don't need complex analysis
		if (address < 0x1000 ||                    // first 4KB (most common attacks)
		    address == 0xFFFFFFFFFFFFFFFF ||       // invalid sentinel
		    address == 0xDEADBEEF ||               // common corruption marker
		    address == 0xCCCCCCCC)                 // uninitialized memory pattern
		{
			return true;
		}

		// expert recommendation: exact database match (hash lookup - very fast)
		if (IsKnownCrashPointer(ptr))
		{
			return true;
		}

		// expert recommendation: only run expensive pattern analysis for suspicious ranges
		// this prevents performance impact on normal pointers
		if (address < 0x10000 ||                           // low addresses
		    address > 0x100000000000 ||                    // high addresses
		    (address >= 0x1A0000000000 && address <= 0x1AFFFFFFFFFFFF)) // attack range
		{
			return IsLikelyAttackPattern(ptr);
		}

		return false;
	}

	// context-aware validation for specific use cases
	inline bool IsKnownCrashPointerForNetworking(void* ptr)
	{
		// specialized validation for network-related pointers (more strict)
		if (!ptr) return true;

		uintptr_t address = reinterpret_cast<uintptr_t>(ptr);

		// network pointers should never be in low memory (common attack vector)
		if (address < 0x10000)
		{
			return true;
		}

		// use standard enhanced detection for other cases
		return IsKnownCrashPointerEnhanced(ptr);
	}

	// validation for entity/game object pointers
	inline bool IsKnownCrashPointerForEntities(void* ptr)
	{
		// specialized validation for game entity pointers
		if (!ptr) return true;

		uintptr_t address = reinterpret_cast<uintptr_t>(ptr);

		// entity pointers should be well-aligned (8-byte minimum)
		if ((address & 0x7) != 0)
		{
			return true;
		}

		// use standard enhanced detection
		return IsKnownCrashPointerEnhanced(ptr);
	}

	// Triple-based fuzzer protection using semantic fields only
	// The vector 'validTasks' is your existing list of {treeIndex, taskIndex, taskType, taskTreeType}
	static std::unordered_set<std::uint64_t> g_ValidTaskTriples;
	static std::once_flag g_TripleInitFlag;
	static std::set<std::tuple<int, int, int, int>> g_SeenTasks; // Your collected 1,435 4-tuples

	// Fix pre-population bug by moving static to file scope
	static bool g_TasksPrePopulated = false;

	// LEARNING MODE: Pre-populate with existing task combinations
	inline void PrePopulateExistingTasks()
	{
		if (g_TasksPrePopulated) return;

		// Your collected dataset: 1,435 valid task combinations from clean sessions
		static const std::vector<std::tuple<int,int,int,int>> validTasks = {
			{0, 0, 32, 0}, {0, 0, 150, 0}, {0, 0, 150, 1}, {0, 0, 150, 5}, {0, 0, 152, 0},
			{0, 0, 154, 1}, {0, 0, 154, 5}, {0, 0, 154, 6}, {0, 0, 168, 0}, {0, 0, 168, 1},
			{0, 0, 169, 1}, {0, 0, 169, 5}, {0, 0, 320, 0}, {0, 0, 322, 0}, {0, 0, 451, 0},
			{0, 0, 451, 1}, {0, 0, 451, 2}, {0, 0, 451, 5}, {0, 0, 474, 0}, {0, 0, 594, 0},
			{0, 0, 594, 1}, {0, 0, 594, 2}, {0, 0, 594, 3}, {0, 0, 594, 4}, {0, 0, 594, 5},
			{0, 0, 594, 6}, {0, 0, 594, 7}, {0, 1, 32, 1}, {0, 1, 32, 2}, {0, 1, 32, 3},
			{0, 1, 32, 4}, {0, 1, 32, 5}, {0, 1, 32, 6}, {0, 1, 32, 7}, {0, 1, 32, 10},
			{0, 1, 48, 1}, {0, 1, 48, 255}, {0, 1, 68, 1}, {0, 1, 69, 1}, {0, 1, 76, 1},
			{0, 1, 154, 1}, {0, 1, 154, 2}, {0, 1, 154, 6}, {0, 1, 346, 1}, {0, 1, 355, 1},
			{0, 1, 355, 2}, {0, 1, 368, 1}, {0, 1, 369, 1}, {0, 1, 371, 1}, {0, 1, 383, 1},
			{0, 1, 418, 1}, {0, 1, 579, 2}, {0, 1, 579, 3}, {0, 1, 579, 4}, {0, 1, 581, 7},
			{0, 1, 582, 5}, {0, 1, 582, 7}, {0, 1, 583, 1}, {0, 1, 583, 2}, {0, 1, 583, 3},
			{0, 1, 583, 7}, {0, 1, 583, 9}, {0, 1, 587, 7}, {0, 1, 594, 1}, {0, 1, 594, 6},
			{0, 1, 601, 2}, {0, 1, 603, 2}, {0, 1, 603, 5}, {0, 1, 603, 9}, {0, 1, 604, 1},
			{0, 1, 605, 1}, {0, 1, 609, 1}, {0, 1, 610, 1}, {0, 2, 32, 2}, {0, 2, 32, 7},
			{0, 2, 48, 3}, {0, 2, 48, 4}, {0, 2, 48, 5}, {0, 2, 48, 6}, {0, 2, 48, 7},
			{0, 2, 48, 8}, {0, 2, 48, 11}, {0, 2, 48, 255}, {0, 2, 355, 2}, {0, 2, 428, 2},
			{0, 2, 428, 3}, {0, 2, 428, 6}, {0, 2, 428, 8}, {0, 2, 576, 2}, {0, 2, 576, 7},
			{0, 3, 48, 255}, {0, 3, 428, 2}, {0, 3, 428, 9}, {1, 0, 3, 0}, {1, 0, 4, 0},
			{1, 0, 31, 2}, {1, 0, 74, 0}, {1, 0, 76, 0}, {1, 0, 79, 0}, {1, 0, 138, 1},
			{1, 0, 161, 0}, {1, 0, 161, 1}, {1, 0, 191, 2}, {1, 0, 374, 0}, {1, 0, 449, 0},
			{1, 0, 470, 0}, {1, 0, 594, 0}, {1, 0, 594, 1}, {1, 0, 594, 4}, {1, 1, 32, 1},
			{1, 1, 32, 2}, {1, 1, 48, 2}, {1, 1, 48, 255}, {1, 1, 53, 2}, {1, 1, 76, 1},
			{1, 1, 188, 1}, {1, 1, 583, 1}, {1, 1, 587, 1}, {1, 1, 587, 5}, {1, 1, 604, 1},
			{1, 2, 48, 255}, {2, 0, 11, 1}, {2, 0, 14, 1}, {2, 0, 39, 5}, {2, 0, 48, 0},
			{2, 0, 48, 1}, {2, 0, 48, 2}, {2, 0, 113, 1}, {2, 0, 113, 2}, {2, 0, 114, 1},
			{2, 0, 115, 0}, {2, 0, 115, 1}, {2, 0, 121, 0}, {2, 0, 121, 1}, {2, 0, 123, 0},
			{2, 0, 123, 1}, {2, 0, 138, 1}, {2, 0, 147, 1}, {2, 0, 168, 1}, {2, 0, 257, 1},
			{2, 0, 257, 2}, {2, 0, 474, 0}, {2, 0, 474, 1}, {2, 0, 532, 0}, {2, 0, 532, 1},
			{2, 0, 532, 2}, {2, 0, 532, 3}, {2, 0, 616, 0}, {2, 0, 618, 1}, {2, 1, 4, 2},
			{2, 1, 32, 2}, {2, 1, 48, 2}, {2, 1, 48, 3}, {2, 1, 113, 2}, {2, 1, 115, 1},
			{2, 1, 115, 2}, {2, 1, 138, 2}, {2, 1, 161, 2}, {2, 1, 193, 2}, {2, 1, 221, 3},
			{2, 1, 257, 1}, {2, 1, 257, 2}, {2, 1, 257, 3}, {2, 1, 523, 2}, {2, 1, 532, 1},
			{2, 1, 532, 2}, {2, 1, 533, 1}, {2, 1, 533, 2}, {2, 1, 533, 4}, {2, 1, 539, 2},
			{2, 1, 563, 2}, {2, 1, 582, 2}, {2, 1, 594, 1}, {2, 1, 594, 2}, {2, 1, 594, 6},
			{2, 2, 15, 3}, {2, 2, 23, 3}, {2, 2, 32, 2}, {2, 2, 48, 1}, {2, 2, 48, 2},
			{2, 2, 48, 3}, {2, 2, 48, 4}, {2, 2, 138, 3}, {2, 2, 147, 3}, {2, 2, 147, 4},
			{2, 2, 176, 3}, {2, 2, 176, 4}, {2, 2, 178, 3}, {2, 2, 193, 3}, {2, 2, 257, 2},
			{2, 2, 257, 3}, {2, 2, 257, 5}, {2, 2, 281, 3}, {2, 2, 492, 3}, {2, 2, 492, 5},
			{2, 2, 507, 3}, {2, 2, 523, 3}, {2, 2, 533, 3}, {2, 2, 545, 3}, {2, 2, 547, 3},
			{2, 2, 576, 3}, {2, 2, 579, 3}, {2, 2, 580, 2}, {2, 2, 581, 2}, {2, 2, 582, 2},
			{2, 2, 582, 3}, {2, 2, 583, 3}, {2, 2, 587, 2}, {2, 2, 587, 3}, {2, 2, 588, 7},
			{2, 2, 604, 2}, {2, 2, 604, 3}, {2, 2, 605, 3}, {2, 3, 4, 4}, {2, 3, 23, 4},
			{2, 3, 48, 2}, {2, 3, 48, 3}, {2, 3, 48, 4}, {2, 3, 48, 5}, {2, 3, 48, 6},
			{2, 3, 48, 255}, {2, 3, 76, 3}, {2, 3, 76, 4}, {2, 3, 138, 4}, {2, 3, 147, 4},
			{2, 3, 152, 4}, {2, 3, 176, 4}, {2, 3, 176, 5}, {2, 3, 178, 3}, {2, 3, 193, 4},
			{2, 3, 221, 4}, {2, 3, 257, 4}, {2, 3, 260, 4}, {2, 3, 265, 4}, {2, 3, 428, 4},
			{2, 3, 428, 5}, {2, 3, 437, 4}, {2, 3, 524, 4}, {2, 3, 539, 4}, {2, 4, 4, 4},
			{2, 4, 23, 5}, {2, 4, 48, 4}, {2, 4, 48, 5}, {2, 4, 76, 6}, {2, 4, 154, 5},
			{2, 4, 176, 5}, {2, 4, 281, 5}, {2, 4, 428, 5}, {2, 4, 526, 5}, {2, 4, 539, 5},
			{2, 4, 544, 5}, {2, 4, 613, 5}, {2, 4, 613, 6}, {2, 5, 23, 6}, {2, 5, 48, 4},
			{2, 5, 48, 6}, {2, 5, 238, 4}, {2, 5, 428, 6}, {2, 5, 544, 7}, {2, 5, 572, 6},
			{2, 5, 613, 6}, {2, 6, 23, 7}, {2, 6, 48, 8}, {2, 6, 48, 11}, {2, 6, 238, 8},
			{2, 6, 428, 7}, {2, 6, 544, 7}, {2, 7, 23, 9}, {2, 7, 238, 8}, {2, 7, 428, 8},
			{2, 7, 544, 7}, {2, 8, 23, 9}, {2, 8, 238, 8}, {2, 8, 238, 9}, {2, 8, 428, 10},
			{2, 8, 613, 9}, {2, 9, 23, 9}, {2, 9, 23, 10}, {2, 9, 428, 10}, {2, 10, 428, 10},
			{2, 10, 428, 11}, {2, 11, 430, 12},
			{3, 0, 3, 1}, {3, 0, 4, 1}, {3, 0, 5, 1}, {3, 0, 31, 1}, {3, 0, 46, 1},
			{3, 0, 48, 0}, {3, 0, 48, 1}, {3, 0, 48, 255}, {3, 0, 76, 0}, {3, 0, 121, 1},
			{3, 0, 121, 2}, {3, 0, 138, 0}, {3, 0, 138, 1}, {3, 0, 142, 1}, {3, 0, 178, 0},
			{3, 0, 178, 255}, {3, 0, 185, 0}, {3, 0, 193, 0}, {3, 0, 257, 0}, {3, 0, 257, 1},
			{3, 0, 266, 0}, {3, 0, 268, 1}, {3, 0, 435, 0}, {3, 0, 435, 1}, {3, 0, 435, 2},
			{3, 0, 435, 5}, {3, 0, 435, 6}, {3, 0, 470, 0}, {3, 0, 532, 1}, {3, 1, 3, 2},
			{3, 1, 4, 1}, {3, 1, 4, 2}, {3, 1, 30, 2}, {3, 1, 32, 2}, {3, 1, 48, 1},
			{3, 1, 48, 2}, {3, 1, 48, 255}, {3, 1, 53, 1}, {3, 1, 53, 2}, {3, 1, 76, 1},
			{3, 1, 76, 2}, {3, 1, 76, 3}, {3, 1, 138, 2}, {3, 1, 176, 1}, {3, 1, 185, 1},
			{3, 1, 185, 2}, {3, 1, 238, 1}, {3, 1, 257, 2}, {3, 1, 261, 2}, {3, 1, 281, 255},
			{3, 1, 309, 2}, {3, 1, 532, 1}, {3, 1, 532, 2}, {3, 1, 533, 2}, {3, 2, 23, 2},
			{3, 2, 23, 3}, {3, 2, 48, 2}, {3, 2, 48, 3}, {3, 2, 48, 255}, {3, 2, 53, 3},
			{3, 2, 76, 2}, {3, 2, 76, 3}, {3, 2, 257, 3}, {3, 2, 281, 3}, {3, 2, 523, 2},
			{3, 2, 523, 3}, {3, 3, 23, 3}, {3, 3, 23, 4}, {3, 3, 48, 4}, {3, 3, 76, 1},
			{3, 3, 152, 4}, {3, 3, 265, 4}, {3, 3, 428, 3}, {3, 3, 428, 4}, {3, 3, 524, 4},
			{3, 3, 545, 3}, {3, 3, 547, 3}, {3, 3, 547, 4}, {3, 3, 572, 4}, {3, 4, 4, 4},
			{3, 4, 48, 5}, {3, 4, 48, 6}, {3, 4, 76, 5}, {3, 4, 154, 5}, {3, 4, 412, 5},
			{3, 4, 428, 4}, {3, 4, 428, 5}, {3, 4, 430, 4}, {3, 4, 437, 5}, {3, 4, 526, 5},
			{3, 4, 539, 4}, {3, 4, 539, 5}, {3, 4, 544, 4}, {3, 4, 544, 5}, {3, 4, 547, 5},
			{3, 4, 549, 5}, {3, 5, 23, 5}, {3, 5, 23, 6}, {3, 5, 48, 6}, {3, 5, 76, 5},
			{3, 5, 238, 5}, {3, 5, 238, 6}, {3, 5, 428, 5}, {3, 5, 433, 6}, {3, 5, 437, 5},
			{3, 5, 437, 6}, {3, 5, 438, 6}, {3, 5, 539, 6}, {3, 5, 572, 6}, {3, 5, 613, 5},
			{3, 5, 613, 6}, {3, 6, 23, 6}, {3, 6, 23, 7}, {3, 6, 48, 7}, {3, 6, 48, 8},
			{3, 6, 48, 11}, {3, 6, 428, 6}, {3, 6, 428, 7}, {3, 6, 428, 8}, {3, 6, 437, 7},
			{3, 6, 539, 7}, {3, 6, 544, 7}, {3, 6, 613, 6}, {3, 7, 23, 8}, {3, 7, 48, 7},
			{3, 7, 238, 8}, {3, 7, 238, 9}, {3, 7, 428, 7}, {3, 7, 428, 8}, {3, 7, 430, 8},
			{3, 7, 539, 8}, {3, 7, 544, 7}, {3, 7, 613, 8}, {3, 8, 23, 9}, {3, 8, 23, 10},
			{3, 8, 238, 8}, {3, 8, 238, 9}, {3, 8, 428, 9}, {3, 8, 430, 9}, {3, 8, 613, 9},
			{3, 9, 23, 9}, {3, 9, 23, 10}, {3, 9, 428, 10}, {3, 9, 428, 11}, {3, 10, 428, 10},
			{3, 10, 428, 11}, {3, 10, 430, 11}, {3, 10, 613, 11}, {3, 11, 430, 12},
			{4, 0, 1, 0}, {4, 0, 22, 0}, {4, 0, 23, 0}, {4, 0, 31, 0}, {4, 0, 31, 1},
			{4, 0, 31, 2}, {4, 0, 31, 3}, {4, 0, 31, 7}, {4, 0, 48, 255}, {4, 0, 77, 0},
			{4, 0, 79, 0}, {4, 0, 80, 0}, {4, 0, 138, 0}, {4, 0, 142, 0}, {4, 0, 142, 1},
			{4, 0, 142, 2}, {4, 0, 142, 3}, {4, 0, 152, 0}, {4, 0, 161, 0}, {4, 0, 176, 0},
			{4, 0, 176, 2}, {4, 0, 177, 0}, {4, 0, 184, 255}, {4, 0, 185, 0}, {4, 0, 270, 0},
			{4, 0, 412, 0}, {4, 0, 445, 255}, {4, 0, 449, 0}, {4, 0, 452, 0}, {4, 0, 455, 0},
			{4, 0, 457, 0}, {4, 0, 467, 0}, {4, 0, 487, 255}, {4, 1, 4, 1}, {4, 1, 10, 1},
			{4, 1, 48, 1}, {4, 1, 48, 255}, {4, 1, 53, 1}, {4, 1, 76, 0}, {4, 1, 76, 1},
			{4, 1, 77, 1}, {4, 1, 83, 1}, {4, 1, 138, 1}, {4, 1, 149, 1}, {4, 1, 154, 1},
			{4, 1, 176, 0}, {4, 1, 176, 1}, {4, 1, 185, 1}, {4, 1, 191, 1}, {4, 1, 215, 1},
			{4, 1, 238, 1}, {4, 1, 261, 1}, {4, 1, 266, 1}, {4, 1, 281, 255}, {4, 1, 427, 1},
			{4, 1, 428, 1}, {4, 1, 429, 1}, {4, 1, 433, 1}, {4, 1, 434, 1}, {4, 1, 438, 1},
			{4, 1, 444, 0}, {4, 1, 445, 255}, {4, 1, 454, 1}, {4, 1, 502, 0}, {4, 1, 502, 1},
			{4, 1, 622, 1}, {4, 1, 624, 1}, {4, 2, 23, 2}, {4, 2, 48, 1}, {4, 2, 48, 2},
			{4, 2, 48, 255}, {4, 2, 53, 2}, {4, 2, 76, 2}, {4, 2, 85, 2}, {4, 2, 154, 2},
			{4, 2, 227, 2}, {4, 2, 265, 2}, {4, 2, 277, 2}, {4, 2, 278, 2}, {4, 2, 281, 2},
			{4, 2, 355, 2}, {4, 2, 430, 2}, {4, 2, 444, 0}, {4, 2, 454, 1}, {4, 2, 503, 2},
			{4, 2, 504, 1}, {4, 2, 504, 2}, {4, 2, 631, 2}, {4, 3, 4, 3}, {4, 3, 48, 2},
			{4, 3, 48, 3}, {4, 3, 48, 255}, {4, 3, 103, 3}, {4, 3, 138, 3}, {4, 3, 277, 3},
			{4, 3, 285, 3}, {4, 3, 428, 3}, {4, 3, 631, 2}, {4, 4, 48, 4}, {4, 4, 48, 255},
			{4, 4, 76, 4}, {4, 4, 281, 4}, {4, 4, 430, 4}, {4, 5, 48, 5},
			// NEW combinations from validtask2.log
			{0, 0, 121, 1}, {0, 0, 150, 4}, {0, 0, 154, 4}, {0, 0, 168, 4}, {0, 0, 169, 3},
			{0, 0, 451, 4}, {0, 1, 2, 1}, {0, 1, 154, 5}, {0, 1, 346, 2}, {0, 1, 372, 1},
			{0, 1, 580, 5}, {0, 1, 587, 6}, {0, 1, 603, 4}, {0, 1, 604, 6}, {0, 2, 355, 1},
			{0, 3, 428, 7}, {1, 1, 581, 5}, {1, 1, 583, 2}, {1, 1, 604, 5}, {2, 0, 14, 5},
			{2, 0, 114, 2}, {2, 0, 123, 2}, {2, 0, 123, 5}, {2, 0, 532, 5}, {2, 1, 76, 2},
			{2, 1, 138, 6}, {2, 1, 178, 2}, {2, 1, 523, 3}, {2, 1, 533, 3}, {2, 1, 533, 5},
			{2, 1, 533, 6}, {2, 1, 563, 3}, {2, 1, 563, 6}, {2, 2, 4, 3}, {2, 2, 23, 4},
			{2, 2, 48, 5}, {2, 2, 48, 7}, {2, 2, 138, 7}, {2, 2, 257, 4}, {2, 2, 257, 6},
			{2, 2, 281, 7}, {2, 2, 492, 4}, {2, 2, 507, 7}, {2, 2, 524, 4}, {2, 2, 580, 3},
			{2, 3, 48, 8}, {2, 3, 185, 4}, {2, 3, 428, 3}, {2, 3, 511, 8}, {2, 3, 526, 5},
			{2, 3, 547, 4}, {2, 3, 572, 4}, {2, 4, 152, 9}, {2, 4, 412, 5}, {2, 4, 572, 6},
			{2, 5, 48, 8}, {2, 5, 48, 10}, {2, 5, 149, 10}, {2, 5, 437, 6}, {2, 6, 154, 11},
			{2, 6, 437, 7}, {2, 6, 539, 7}, {2, 7, 23, 8}, {2, 7, 238, 9}, {2, 7, 539, 8},
			{2, 8, 23, 10}, {2, 8, 428, 9}, {2, 9, 428, 11}, {3, 0, 3, 0}, {3, 0, 31, 0},
			{3, 0, 46, 255}, {3, 0, 76, 1}, {3, 0, 123, 1}, {3, 0, 139, 1}, {3, 1, 4, 0},
			{3, 1, 31, 2}, {3, 1, 138, 0}, {3, 1, 147, 2}, {3, 1, 281, 2}, {3, 1, 532, 3},
			{3, 1, 563, 2}, {3, 2, 53, 1}, {3, 2, 138, 3}, {3, 2, 507, 3}, {3, 2, 523, 4},
			{3, 3, 76, 4}, {3, 3, 511, 4}, {3, 3, 513, 4}, {3, 3, 572, 6}, {3, 4, 48, 0},
			{3, 4, 152, 5}, {3, 5, 149, 6}, {3, 5, 227, 6}, {3, 5, 544, 6}, {3, 6, 48, 2},
			{3, 6, 154, 7}, {3, 6, 238, 7}, {3, 7, 48, 2}, {3, 7, 430, 9}, {3, 7, 544, 8},
			{3, 8, 48, 7}, {3, 8, 430, 0}, {3, 9, 76, 0}, {3, 9, 430, 3}, {3, 9, 430, 10},
			{3, 9, 613, 0}, {4, 0, 22, 1}, {4, 0, 31, 9}, {4, 0, 31, 10}, {4, 0, 567, 0},
			{4, 1, 4, 0}, {4, 1, 238, 0}, {4, 1, 270, 1}, {4, 2, 4, 2}, {4, 2, 23, 1},
			{4, 2, 76, 1}, {4, 2, 185, 2}, {4, 2, 266, 2}, {4, 2, 279, 2}, {4, 2, 503, 1},
			{4, 3, 138, 2}, {4, 3, 428, 2}, {4, 4, 1, 4}, {4, 4, 430, 3}, {4, 5, 285, 5},
			// NEW combinations from validtask3.log
			{0, 0, 169, 4}, {0, 1, 32, 8}, {0, 1, 70, 1}, {0, 1, 71, 1}, {0, 1, 579, 5},
			{0, 1, 582, 2}, {0, 1, 582, 4}, {0, 1, 584, 1}, {0, 1, 587, 5}, {0, 2, 48, 9},
			{0, 2, 428, 5}, {0, 2, 580, 2}, {0, 3, 428, 4}, {0, 3, 428, 5}, {0, 3, 428, 6},
			{1, 0, 31, 6}, {1, 0, 48, 6}, {1, 0, 76, 1}, {1, 0, 594, 3}, {1, 0, 594, 5},
			{1, 0, 594, 6}, {1, 1, 32, 4}, {1, 1, 32, 6}, {1, 1, 579, 6}, {1, 1, 587, 7},
			{1, 1, 592, 1}, {1, 1, 604, 4}, {1, 2, 48, 6}, {1, 2, 48, 7}, {1, 2, 428, 2},
			{1, 2, 428, 7}, {1, 3, 428, 1}, {2, 0, 46, 2}, {2, 0, 257, 0}, {2, 0, 268, 1},
			{2, 0, 451, 0}, {2, 0, 534, 1}, {2, 1, 35, 2}, {2, 1, 48, 1}, {2, 1, 475, 2},
			{2, 1, 532, 3}, {2, 2, 34, 3}, {2, 2, 152, 3}, {2, 2, 154, 4}, {2, 2, 492, 2},
			{2, 2, 507, 4}, {2, 2, 533, 4}, {2, 2, 572, 3}, {2, 2, 576, 2}, {2, 2, 588, 2},
			{2, 2, 603, 2}, {2, 2, 605, 2}, {2, 3, 154, 4}, {2, 3, 257, 5}, {2, 3, 427, 4},
			{2, 3, 436, 4}, {2, 3, 437, 5}, {2, 3, 511, 4}, {2, 3, 511, 5}, {2, 3, 513, 4},
			{2, 3, 537, 4}, {2, 4, 48, 6}, {2, 4, 152, 5}, {2, 4, 152, 6}, {2, 4, 257, 5},
			{2, 4, 437, 4}, {2, 4, 437, 5}, {2, 4, 539, 6}, {2, 5, 23, 7}, {2, 5, 149, 6},
			{2, 5, 154, 6}, {2, 5, 539, 6}, {2, 6, 154, 7}, {2, 6, 427, 7}, {2, 6, 428, 8},
			{2, 6, 430, 7}, {2, 6, 613, 7}, {2, 7, 427, 8}, {2, 7, 430, 8}, {2, 8, 76, 9},
			{2, 8, 430, 9}, {3, 0, 4, 0}, {3, 0, 142, 0}, {3, 0, 267, 1}, {3, 0, 282, 0},
			{3, 0, 282, 1}, {3, 0, 309, 1}, {3, 0, 412, 1}, {3, 0, 435, 7}, {3, 0, 532, 2},
			{3, 0, 532, 4}, {3, 1, 48, 0}, {3, 1, 265, 2}, {3, 1, 433, 2}, {3, 1, 437, 2},
			{3, 1, 438, 2}, {3, 1, 502, 1}, {3, 1, 502, 2}, {3, 1, 523, 2}, {3, 1, 523, 3},
			{3, 1, 532, 6}, {3, 1, 532, 7}, {3, 1, 631, 1}, {3, 2, 48, 4}, {3, 2, 152, 3},
			{3, 2, 152, 4}, {3, 2, 257, 2}, {3, 2, 428, 3}, {3, 2, 428, 4}, {3, 2, 436, 2},
			{3, 2, 436, 3}, {3, 2, 504, 3}, {3, 2, 523, 5}, {3, 2, 523, 6}, {3, 2, 523, 8},
			{3, 2, 533, 3}, {3, 2, 539, 3}, {3, 3, 48, 3}, {3, 3, 152, 5}, {3, 3, 154, 4},
			{3, 3, 257, 4}, {3, 3, 412, 4}, {3, 3, 437, 3}, {3, 3, 437, 4}, {3, 3, 507, 4},
			{3, 3, 549, 4}, {3, 3, 572, 3}, {3, 3, 572, 5}, {3, 3, 572, 7}, {3, 3, 572, 9},
			{3, 4, 48, 10}, {3, 4, 412, 4}, {3, 4, 433, 5}, {3, 4, 437, 4}, {3, 4, 437, 6},
			{3, 4, 438, 5}, {3, 4, 505, 5}, {3, 4, 547, 255}, {3, 5, 48, 5}, {3, 5, 428, 7},
			{3, 5, 430, 6}, {3, 5, 433, 5}, {3, 5, 437, 4}, {3, 5, 438, 5}, {3, 5, 511, 6},
			{3, 5, 539, 5}, {3, 5, 539, 7}, {3, 6, 23, 8}, {3, 6, 48, 6}, {3, 6, 152, 7},
			{3, 6, 437, 6}, {3, 7, 48, 6}, {3, 7, 48, 8}, {3, 7, 428, 9}, {3, 8, 76, 9},
			{3, 8, 430, 5}, {3, 8, 430, 10}, {3, 9, 430, 5}, {3, 9, 613, 10}, {3, 10, 76, 11},
			{4, 0, 10, 0}, {4, 0, 31, 5}, {4, 0, 142, 4}, {4, 0, 142, 5}, {4, 0, 142, 255},
			{4, 0, 176, 5}, {4, 0, 176, 8}, {4, 0, 177, 1}, {4, 1, 142, 1}, {4, 1, 177, 1},
			{4, 2, 138, 2}, {4, 2, 176, 2}, {4, 2, 257, 2}, {4, 2, 286, 2}, {4, 3, 53, 3},
			// NEW combinations from validtask4.log
			{0, 0, 154, 3}, {0, 0, 169, 0}, {0, 1, 428, 1}, {0, 1, 579, 1}, {0, 1, 582, 1},
			{0, 1, 582, 6}, {0, 1, 583, 5}, {0, 2, 48, 2}, {0, 2, 428, 1}, {0, 2, 428, 7},
			{0, 3, 428, 1}, {1, 0, 4, 1}, {1, 0, 32, 1}, {1, 0, 46, 1}, {1, 0, 216, 1},
			{1, 1, 4, 2}, {1, 1, 188, 2}, {1, 1, 221, 2}, {1, 1, 257, 2}, {1, 1, 602, 1},
			{1, 2, 48, 3}, {1, 2, 154, 3}, {1, 2, 221, 3}, {1, 3, 48, 4}, {2, 0, 48, 4},
			{2, 0, 48, 5}, {2, 0, 121, 4}, {2, 0, 121, 5}, {2, 0, 532, 6}, {2, 0, 616, 1},
			{2, 1, 114, 2}, {2, 1, 533, 7}, {2, 1, 594, 5}, {2, 1, 594, 7}, {2, 2, 32, 3},
			{2, 2, 76, 4}, {2, 2, 507, 8}, {2, 2, 576, 6}, {2, 2, 576, 7}, {2, 2, 579, 2},
			{2, 2, 582, 6}, {2, 2, 582, 8}, {2, 2, 583, 2}, {2, 2, 583, 6}, {2, 2, 605, 7},
			{2, 3, 149, 4}, {2, 3, 428, 7}, {2, 3, 428, 9}, {2, 3, 497, 9}, {2, 4, 152, 10},
			{2, 5, 48, 11}, {3, 0, 268, 255}, {3, 1, 268, 255}, {3, 1, 282, 0}, {3, 4, 513, 5},
			{4, 0, 213, 0}, {4, 0, 450, 0}, {4, 0, 488, 0}, {4, 1, 3, 1}, {4, 1, 132, 1},
			{4, 1, 187, 1}, {4, 1, 453, 255}, {4, 2, 428, 2}, {4, 2, 455, 0}, {4, 2, 532, 2},
			{4, 3, 430, 3}, {4, 3, 454, 1}, {4, 3, 523, 3}, {4, 4, 23, 4}, {4, 4, 152, 4},
			{4, 4, 524, 4}, {4, 4, 544, 4}, {4, 4, 547, 4}, {4, 4, 572, 4}, {4, 4, 631, 2},
			{4, 5, 4, 5}, {4, 5, 48, 6}, {4, 5, 428, 5}, {4, 5, 437, 5}, {4, 5, 526, 5},
			{4, 5, 539, 5}, {4, 5, 547, 5}, {4, 6, 23, 6}, {4, 6, 48, 6}, {4, 6, 227, 6},
			{4, 6, 428, 6}, {4, 6, 437, 5}, {4, 6, 437, 6}, {4, 6, 539, 6}, {4, 6, 544, 6},
			{4, 6, 544, 7}, {4, 6, 572, 6}, {4, 6, 613, 6}, {4, 7, 23, 7}, {4, 7, 48, 7},
			{4, 7, 48, 8}, {4, 7, 238, 7}, {4, 7, 428, 7}, {4, 7, 539, 7}, {4, 7, 544, 7},
			{4, 7, 613, 7}, {4, 8, 23, 8}, {4, 8, 238, 8}, {4, 8, 238, 9}, {4, 8, 428, 8},
			{4, 8, 544, 7}, {4, 8, 544, 8}, {4, 8, 613, 8}, {4, 9, 23, 9}, {4, 9, 23, 10},
			{4, 9, 238, 9}, {4, 9, 428, 9}, {4, 9, 613, 9}, {4, 10, 23, 10}, {4, 10, 428, 10},
			{4, 10, 428, 11}, {4, 10, 613, 10}, {4, 11, 428, 11}, {4, 11, 430, 11}, {4, 11, 430, 12},
			{4, 12, 430, 12}, {4, 2, 31, 2}, {0, 2, 31, 2},
			// New combinations from validtask5.log
			{0, 0, 36, 0}, {0, 1, 580, 2}, {1, 2, 428, 1}, {1, 2, 428, 3}, {2, 2, 547, 4}, 
			{2, 2, 572, 1}, {2, 3, 412, 4}, {2, 3, 539, 5}, {2, 4, 23, 6}, {2, 5, 428, 7},
			{2, 6, 613, 8}, {2, 9, 613, 10}, {3, 0, 221, 0}, {3, 1, 138, 1}, {4, 2, 238, 2},
			{4, 3, 23, 3}, {4, 3, 76, 3}, {4, 3, 427, 3}, {4, 4, 622, 4}, {4, 1, 133, 1},
			// New combinations from validtask6.log
			{2, 0, 121, 6}, {4, 2, 191, 2}, {3, 0, 502, 1}, {4, 2, 506, 2}, {4, 3, 152, 3},
			{4, 4, 149, 4}, {4, 5, 154, 5}, {3, 1, 503, 2}, {3, 3, 281, 4}, {4, 1, 625, 1},
			{3, 0, 626, 0}, {3, 4, 285, 5}, {2, 0, 474, 2}, {2, 1, 475, 3}, {4, 3, 437, 3},
			{4, 4, 539, 4}, {4, 5, 23, 5}, {4, 8, 76, 8}, {4, 8, 430, 8}, {4, 5, 412, 5},
			{4, 7, 437, 7}, {4, 8, 539, 8}, {4, 10, 430, 10}, {2, 0, 208, 0}, {2, 0, 476, 1},
			{0, 1, 604, 2}, {0, 1, 603, 3}, {2, 3, 257, 3}, {0, 2, 428, 4}, {0, 3, 428, 3},
			{4, 0, 176, 1}, {3, 2, 503, 3}, {3, 3, 138, 4}, {0, 0, 451, 3}, {3, 3, 185, 4},
			{3, 0, 80, 1}, {3, 1, 83, 2}, {3, 2, 532, 3}, {3, 3, 523, 4}, {3, 4, 23, 5},
			{3, 5, 428, 6}, {3, 6, 613, 7}, {4, 0, 107, 0}, {4, 0, 107, 1}, {4, 3, 281, 3},
			{4, 0, 80, 1}, {4, 5, 544, 5}, {4, 3, 266, 3}, {3, 0, 178, 1}, {4, 1, 12, 0},
			{3, 1, 12, 1}, {1, 0, 12, 1}, {4, 4, 76, 3}, {1, 1, 32, 5}, {3, 8, 430, 8},
			{3, 1, 281, 1}, {2, 2, 257, 7}, {2, 3, 265, 8}, {0, 1, 48, 2}, {3, 4, 613, 5},
			{2, 0, 474, 3}, {2, 1, 532, 4}, {2, 2, 523, 5}, {2, 3, 152, 6}, {2, 4, 48, 7},
			{2, 5, 437, 8}, {2, 6, 539, 9}, {2, 7, 23, 10}, {2, 8, 428, 11}, {2, 4, 154, 7},
			{2, 3, 572, 6}, {2, 4, 412, 7}, {2, 6, 437, 9}, {2, 7, 539, 10}, {2, 8, 23, 11},
			{2, 9, 428, 12}, {2, 10, 613, 13}, {0, 0, 594, 8}, {0, 1, 582, 9}, {0, 2, 428, 10},
			{0, 1, 32, 9}, {0, 2, 48, 10}, {0, 3, 428, 11}, {2, 10, 430, 13}, {3, 2, 572, 3},
			{2, 10, 613, 11}, {3, 0, 538, 1}, {3, 2, 547, 3}, {3, 3, 539, 4}, {3, 0, 262, 1},
			{3, 0, 623, 1}, {3, 4, 149, 5}, {3, 5, 154, 6}, {3, 1, 532, 5}, {0, 1, 584, 5},
			{1, 1, 576, 1}, {3, 1, 31, 0}, {2, 0, 46, 1}, {3, 0, 594, 2}, {0, 0, 168, 3},
			{4, 2, 261, 2}, {2, 3, 102, 4}, {2, 0, 121, 2}, {2, 1, 594, 3}, {2, 2, 582, 4},
			{2, 6, 76, 8}, {0, 0, 35, 4}, {0, 1, 34, 5}, {2, 2, 572, 4}, {2, 3, 412, 5},
			{2, 5, 437, 7}, {2, 6, 539, 8}, {0, 1, 594, 4}, {0, 2, 576, 5}, {0, 2, 32, 5},
			{3, 1, 257, 3}, {4, 11, 613, 11}, {0, 1, 582, 3}, {1, 0, 46, 6}, {1, 1, 221, 7},
			{1, 2, 154, 8}, {2, 2, 193, 2}, {2, 3, 176, 3}, {1, 0, 31, 1}, {2, 2, 571, 1},
			{2, 2, 547, 1}, {0, 1, 584, 7}, {4, 3, 257, 3}, {4, 3, 76, 1}, {4, 6, 433, 6},
			{4, 6, 438, 6}, {4, 4, 571, 4}, {3, 3, 76, 3}, {3, 8, 48, 6}, {3, 3, 544, 4},
			{3, 4, 238, 5}, {3, 3, 238, 4}, {2, 5, 48, 7}, {3, 3, 547, 0}, {3, 4, 572, 5},
			{3, 5, 48, 7}, {3, 6, 539, 8}, {3, 7, 23, 9}, {3, 8, 428, 10}, {2, 0, 185, 1},
			{2, 0, 31, 1}, {1, 0, 453, 255}, {3, 0, 449, 0}, {3, 1, 142, 2}, {3, 0, 312, 1},
			{3, 1, 453, 2}, {3, 2, 455, 3}, {3, 3, 454, 4}, {3, 4, 631, 5}, {3, 1, 257, 0},
			{3, 2, 147, 1}, {3, 3, 48, 255},
			// New combinations from validtask7.log
			{2, 3, 221, 5}, {0, 1, 587, 1}, {4, 0, 567, 1}, {4, 5, 48, 255}, {2, 0, 532, 4},
			{2, 3, 48, 7}, {4, 3, 429, 3}, {1, 0, 221, 2}, {1, 1, 154, 3}, {0, 1, 584, 3},
			{4, 2, 449, 1}, {3, 1, 76, 0}, {4, 1, 10, 0}, {2, 2, 3, 3}, {3, 0, 435, 8},
			{3, 2, 3, 3}, {3, 2, 3, 255}, {3, 0, 543, 1}, {3, 1, 572, 2}, {3, 2, 412, 3},
			{3, 3, 433, 4}, {3, 3, 438, 4}, {3, 0, 147, 1}, {3, 7, 76, 8}, {3, 0, 268, 2},
			{4, 1, 128, 1}, {0, 1, 149, 1}, {0, 2, 154, 2}, {1, 1, 604, 6}, {2, 0, 268, 2},
			{4, 2, 428, 1}, {2, 6, 76, 7}, {2, 9, 430, 10}, {2, 1, 221, 2}, {3, 1, 262, 2},
			{3, 2, 48, 0}, {2, 2, 102, 3}, {2, 2, 102, 4}, {2, 2, 549, 3}, {2, 5, 437, 5},
			{1, 0, 594, 2}, {1, 1, 604, 3}, {3, 3, 1, 4}, {3, 6, 1, 7}, {3, 5, 1, 10},
			{1, 1, 32, 3}, {1, 3, 428, 2}, {1, 0, 12, 0}, {0, 1, 579, 6}, {3, 0, 185, 1},
			{3, 6, 1, 11}, {3, 7, 437, 6}, {3, 8, 539, 8}, {3, 6, 437, 5}, {3, 7, 539, 7},
			{3, 8, 23, 8}, {3, 9, 428, 9}, {3, 1, 532, 8}, {3, 2, 523, 9}, {0, 1, 603, 8},
			{0, 3, 428, 8}, {0, 0, 35, 3}, {0, 1, 34, 4}, {3, 0, 193, 1}, {3, 1, 176, 2},
			{3, 3, 545, 4}, {3, 1, 436, 1}, {3, 2, 544, 2}, {3, 3, 238, 3}, {3, 4, 23, 4},
			{0, 1, 601, 3}, {0, 1, 587, 4}, {3, 2, 76, 5}, {3, 3, 285, 4}, {0, 1, 154, 7},
			// New combinations from validtask8.log
			{3, 0, 436, 1}, {1, 0, 32, 0}, {4, 0, 442, 255}, {3, 4, 257, 5}, {4, 1, 428, 0},
			{2, 2, 178, 2}, {2, 3, 4, 3}, {0, 1, 594, 2}, {0, 2, 576, 3}, {0, 2, 32, 3},
			{0, 1, 48, 5}, {0, 1, 583, 4}, {0, 1, 605, 5}, {0, 1, 594, 5}, {0, 2, 576, 6},
			{0, 2, 32, 6}, {1, 0, 31, 4}, {1, 0, 76, 3}, {2, 1, 523, 4}, {2, 2, 547, 5},
			{2, 3, 539, 6}, {2, 4, 23, 7}, {2, 5, 428, 8}, {2, 3, 437, 6}, {2, 4, 539, 7},
			{2, 5, 23, 8}, {2, 6, 428, 9}, {2, 7, 613, 10}, {2, 1, 114, 3}, {3, 2, 187, 3},
			{4, 1, 77, 0}, {2, 4, 221, 5}, {2, 2, 48, 0}, {2, 2, 185, 3}, {4, 1, 462, 0},
			{2, 2, 587, 6}, {2, 2, 587, 8}, {4, 12, 613, 12}, {4, 9, 76, 9}, {4, 9, 430, 9},
			{2, 1, 563, 1}, {4, 4, 564, 4}, {4, 5, 572, 5}, {4, 6, 412, 6}, {4, 8, 437, 8},
			{4, 9, 539, 9}, {1, 0, 31, 5}, {4, 7, 428, 8}, {4, 8, 430, 9}, {4, 9, 48, 7},
			{4, 8, 48, 7}, {4, 5, 76, 5}, {4, 1, 154, 0}, {2, 2, 582, 7}, {2, 3, 428, 8},
			{2, 1, 35, 7}, {2, 2, 34, 8}, {4, 7, 48, 11}, {4, 9, 238, 8}, {4, 10, 23, 9},
			{4, 11, 428, 10}, {0, 2, 34, 1}, {2, 2, 23, 5}, {2, 3, 428, 6}, {2, 4, 613, 7},
			{2, 0, 121, 7}, {2, 1, 594, 9}, {2, 2, 582, 10}, {2, 3, 428, 11}, {0, 2, 35, 6},
			{0, 3, 34, 7}, {0, 1, 581, 1}, {0, 0, 35, 6}, {0, 1, 34, 7}, {4, 4, 185, 4},
			{2, 1, 32, 5}, {4, 7, 238, 8}, {4, 8, 23, 9}, {4, 9, 428, 10}, {3, 2, 524, 3},
			{3, 3, 526, 4}, {3, 4, 227, 5}, {3, 6, 544, 6}, {3, 2, 89, 3}, {3, 5, 544, 5},
			{2, 0, 616, 3}, {2, 1, 257, 4}, {0, 1, 584, 2}, {3, 3, 48, 5}, {3, 3, 4, 4},
			{1, 1, 589, 1},
			// New combinations from validtask9.log
			{1, 0, 48, 1}, {3, 6, 238, 8}, {3, 1, 147, 1}, {3, 5, 48, 10}, {3, 7, 238, 7},
			{0, 1, 604, 3}, {0, 1, 32, 0}, {0, 2, 48, 1}, {3, 9, 430, 7}, {3, 5, 48, 11},
			{3, 10, 430, 7}, {3, 3, 526, 0}, {3, 4, 48, 4}, {4, 0, 31, 6}, {3, 4, 572, 4},
			{3, 6, 544, 5}, {2, 1, 4, 1}, {2, 1, 32, 1}, {2, 2, 524, 3}, {2, 3, 526, 4},
			{2, 4, 572, 5}, {2, 6, 544, 6}, {2, 3, 526, 1}, {2, 5, 544, 6}, {4, 10, 76, 10},
			{4, 4, 285, 4}, {3, 5, 238, 7}, {4, 0, 26, 0}, {3, 0, 428, 1}, {3, 7, 427, 8},
			{3, 8, 427, 9}, {3, 3, 633, 4}, {3, 4, 638, 5}, {3, 0, 309, 0}, {3, 0, 428, 0},
			{3, 9, 76, 10}, {3, 2, 523, 7}, {3, 6, 427, 7}, {3, 4, 427, 5},
			// New combinations from validtask10.log
			{3, 0, 107, 1}, {3, 3, 53, 4}, {3, 1, 474, 2}, {3, 2, 475, 3}, {3, 1, 108, 2},
			{3, 6, 430, 8}, {2, 5, 539, 7}, {2, 6, 23, 8}, {2, 7, 428, 9}, {3, 1, 113, 2},
			{2, 3, 547, 5}, {2, 1, 523, 5}, {2, 2, 23, 6}, {2, 4, 613, 8}, {2, 2, 572, 6},
			{2, 4, 437, 7}, {2, 5, 539, 9}, {2, 6, 23, 10}, {2, 7, 428, 11}, {2, 7, 430, 9},
			{2, 4, 437, 6}, {3, 5, 412, 6}, {3, 6, 438, 7}, {4, 0, 80, 3}, {3, 8, 48, 8},
			{3, 9, 48, 8}, {3, 1, 191, 2}, {1, 0, 4, 2}, {1, 1, 188, 3}, {1, 0, 121, 2},
			{1, 1, 604, 7},
            // New combinations from validtask11.log
			{3, 0, 121, 0}, {2, 4, 227, 6}, {2, 6, 544, 8}, {2, 4, 76, 5}, {2, 5, 238, 6},
			{4, 1, 621, 1}, {0, 2, 35, 7}, {0, 3, 34, 8}, {0, 3, 32, 8}, {0, 4, 48, 9},
			{3, 0, 5, 0}, {0, 1, 582, 8}, {0, 2, 428, 9}, {0, 1, 576, 5}, {3, 4, 185, 5},
			{3, 5, 544, 7}, {3, 2, 503, 2}, {0, 1, 605, 3}, {2, 2, 48, 255}, {3, 0, 532, 0},
			{3, 1, 523, 1}, {3, 4, 613, 4}, {3, 2, 545, 2}, {3, 3, 544, 3}, {3, 4, 238, 4},
			{3, 3, 193, 3}, {3, 3, 560, 4}, {3, 4, 562, 5}, {3, 4, 193, 4}, {3, 6, 238, 6},
			{3, 7, 23, 7}, {3, 8, 428, 8}, {3, 2, 178, 2}, {3, 3, 4, 3}, {4, 0, 31, 4},
			{2, 4, 433, 5}, {4, 0, 80, 7}, {3, 7, 437, 8}, {3, 8, 539, 9}, {3, 4, 547, 4},
			{4, 0, 80, 6}, {2, 0, 113, 4},
            // New combinations from validtask12.log
            {2, 4, 438, 5}, {2, 6, 48, 6}, {2, 6, 430, 8}, {2, 7, 48, 6}, {2, 6, 238, 7},
            {0, 1, 605, 2}, {2, 2, 76, 3}, {2, 2, 154, 3}, {4, 4, 428, 4},
            // New combinations from validtask13.log
			{2, 2, 428, 2}, {2, 3, 564, 4}, {2, 6, 437, 6}, {2, 1, 4, 3}, {1, 1, 589, 2},
			{0, 0, 576, 2}, {0, 0, 32, 2}, {0, 1, 48, 3}, {2, 2, 437, 3}, {4, 4, 517, 4},
			{4, 0, 8, 0}, {4, 6, 238, 6}, {4, 2, 427, 2}
		};

		for (const auto& task : validTasks) {
			g_SeenTasks.insert(task);
		}

		g_TasksPrePopulated = true;
		LOG(INFO) << "Pre-populated " << validTasks.size() << " existing 4-tuple task combinations";
	}

	// Build a set of valid triples at startup
	inline void InitializeTaskTripleWhitelist() {
		std::call_once(g_TripleInitFlag, [] {
			// First ensure we have the base data
			PrePopulateExistingTasks();

			// Convert 4-tuples to 3-tuples (removing unstable taskIndex)
			// "When I deduplicated your 1,435 quadruples on these three fields there were 1,078 unique triples"
			for (const auto& task : g_SeenTasks) {
				int treeIdx = std::get<0>(task);      // treeIndex
				// int taskIdx = std::get<1>(task);   // taskIndex - ignore unstable array position
				int taskType = std::get<2>(task);     // taskType
				int taskTreeType = std::get<3>(task); // taskTreeType

				std::uint64_t key = (static_cast<std::uint64_t>(treeIdx)  << 32) |
				                    (static_cast<std::uint64_t>(taskType)  << 16) |
				                     static_cast<std::uint64_t>(taskTreeType);
				g_ValidTaskTriples.insert(key);
			}

			LOG(INFO) << "Initialized task triple whitelist with " << g_ValidTaskTriples.size()
			          << " unique (treeIndex, taskType, taskTreeType) combinations from " << g_SeenTasks.size() << " original 4-tuples";
		});
	}

	// Fast triple validation using semantic fields only
	inline bool IsValidTaskTriple(int treeIdx, int taskType, int taskTreeType) {
		InitializeTaskTripleWhitelist();
		std::uint64_t key = (static_cast<std::uint64_t>(treeIdx)  << 32) |
		                    (static_cast<std::uint64_t>(taskType)  << 16) |
		                     static_cast<std::uint64_t>(taskTreeType);
		return g_ValidTaskTriples.find(key) != g_ValidTaskTriples.end();
	}

	// Rate limiting for fuzzer attack logging
	static std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_LastFuzzerLogTime;
	static std::unordered_map<std::string, int> g_FuzzerAttackCount;
	static std::mutex g_FuzzerLogMutex;

	// "log once, and possibly block the player"
	inline void LogFuzzerAttackOnce(const std::string& playerName, const std::string& attackDetails)
	{
		std::lock_guard<std::mutex> lock(g_FuzzerLogMutex);

		// Only log once per player to prevent spam from loop attacks
		std::string playerKey = "fuzzer_" + playerName;
		if (g_LastFuzzerLogTime.find(playerKey) == g_LastFuzzerLogTime.end())
		{
			LOG(WARNING) << "FUZZER ATTACK detected from " << playerName << " - " << attackDetails;
			g_LastFuzzerLogTime[playerKey] = std::chrono::steady_clock::now();
		}

		// Count attacks for analysis but don't spam logs
		g_FuzzerAttackCount[playerKey]++;
	}

	// REMOVED LEARNING MODE - Pure production whitelist validation
	// "you retain the usefulness of your 3‑day dataset while eliminating the brittleness"
	inline bool IsValidTaskTreeData(int treeIndex, int taskIndex, int taskType, int taskTreeType)
	{
		// Use only semantic fields, ignore unstable array positions
		return IsValidTaskTriple(treeIndex, taskType, taskTreeType);
	}

	// REMOVED LEARNING MODE - "you retain the usefulness of your 3‑day dataset"
	// The learning phase is complete. Pure production whitelist validation only.
}
