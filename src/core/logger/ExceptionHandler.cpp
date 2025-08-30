#include "ExceptionHandler.hpp"

#include "StackTrace.hpp"
#include "game/backend/CrashSignatures.hpp"

#include <hde64.h>
#include <unordered_set>
#include <atomic>


namespace YimMenu
{
	inline auto HashStackTrace(std::vector<uint64_t> stack_trace)
	{
		auto data        = reinterpret_cast<const char*>(stack_trace.data());
		std::size_t size = stack_trace.size() * sizeof(uint64_t);

		return std::hash<std::string_view>()({data, size});
	}

	ExceptionHandler::ExceptionHandler()
	{
		LOG(INFO) << "ExceptionHandler initialized";
		m_OldErrorMode = SetErrorMode(0);
		m_Handler      = SetUnhandledExceptionFilter(&VectoredExceptionHandler);
	}

	ExceptionHandler::~ExceptionHandler()
	{
		SetErrorMode(m_OldErrorMode);
		SetUnhandledExceptionFilter(reinterpret_cast<decltype(&VectoredExceptionHandler)>(m_Handler));
	}

	inline thread_local static StackTrace trace;
	LONG VectoredExceptionHandler(EXCEPTION_POINTERS* exception_info)
	{
		const auto exception_code = exception_info->ExceptionRecord->ExceptionCode;
		if (exception_code == EXCEPTION_BREAKPOINT || exception_code == DBG_PRINTEXCEPTION_C || exception_code == DBG_PRINTEXCEPTION_WIDE_C)
			return EXCEPTION_CONTINUE_SEARCH;

		// crash protection using intelligent pattern detection
		if (exception_code == EXCEPTION_ACCESS_VIOLATION)
		{
			auto violation_address = static_cast<uintptr_t>(exception_info->ExceptionRecord->ExceptionInformation[1]);

			// check if this address matches known signatures or attack patterns
			if (CrashSignatures::IsKnownCrashPointerEnhanced(reinterpret_cast<void*>(violation_address)))
			{
				static std::atomic<int> known_crash_count = 0;
				LOG(WARNING) << "Blocked crash signature or attack pattern at address " << HEX(violation_address)
				            << " (intelligent detection - attempt #" << ++known_crash_count << ")";
				return EXCEPTION_EXECUTE_HANDLER; // force recovery for known crashes and attack patterns
			}

			// detect additional memory corruption patterns not in database
			if (violation_address < 0x1000 ||
			    (violation_address & 0xFFFF000000000000) == 0x7FF7000000000000 ||
			    (violation_address & 0xFFFFFF0000000000) == 0xFFFFFF0000000000) // Nemesis attack pattern
			{
				static std::atomic<int> corruption_count = 0;
				if (++corruption_count > 10)
				{
					LOG(FATAL) << "Too many memory corruption attempts detected, forcing return";
					return EXCEPTION_EXECUTE_HANDLER; // force recovery
				}

				// specific detection for Nemesis delayed crash attacks
				if ((violation_address & 0xFFFFFF0000000000) == 0xFFFFFF0000000000)
				{
					LOG(WARNING) << "Nemesis delayed crash attack detected at " << HEX(violation_address)
					            << " - blocking sophisticated memory corruption";
					return EXCEPTION_EXECUTE_HANDLER; // force recovery from Nemesis attack
				}

				// log new potential crash signature for database expansion
				LOG(WARNING) << "Potential new crash signature detected: " << HEX(violation_address)
				            << " - consider adding to database";
			}
		}

		static std::unordered_set<std::size_t> logged_exceptions;

		trace.NewStackTrace(exception_info);
		const auto trace_hash = HashStackTrace(trace.GetFramePointers());
		if (const auto it = logged_exceptions.find(trace_hash); it == logged_exceptions.end())
		{
			LOG(FATAL) << trace;
			Logger::FlushQueue();

			logged_exceptions.insert(trace_hash);
		}

		if (exception_info->ExceptionRecord->ExceptionInformation[0] == EXCEPTION_EXECUTE_FAULT)
		{
			auto return_address_ptr = (uint64_t*)exception_info->ContextRecord->Rsp;
			if (IsBadReadPtr(reinterpret_cast<void*>(return_address_ptr), 8))
			{
				LOG(FATAL) << "Cannot resume execution, crashing (failed to find valid return address)";
				return EXCEPTION_CONTINUE_SEARCH;
			}
			else
			{
				LOG(FATAL) << "Force returning from function";
				exception_info->ContextRecord->Rip = *return_address_ptr;
				exception_info->ContextRecord->Rsp += 8;
			}
		}
		else
		{
			hde64s opcode{};
			hde64_disasm(reinterpret_cast<void*>(exception_info->ContextRecord->Rip), &opcode);
			if (opcode.flags & F_ERROR)
			{
				LOG(FATAL) << "Cannot resume execution, crashing (failed to decode insn)";
				return EXCEPTION_CONTINUE_SEARCH;
			}

			if (opcode.opcode == 0xFF && opcode.modrm_reg == 4) // JMP (FF /4)
			{
				auto return_address_ptr = (uint64_t*)exception_info->ContextRecord->Rsp;
				if (IsBadReadPtr(reinterpret_cast<void*>(return_address_ptr), 8))
				{
					LOG(FATAL) << "Cannot resume execution, crashing";
					return EXCEPTION_CONTINUE_SEARCH;
				}
				else
				{
					exception_info->ContextRecord->Rip = *return_address_ptr;
					exception_info->ContextRecord->Rsp += 8;
				}
			}
			else
			{
				exception_info->ContextRecord->Rip += opcode.len;

				if (opcode.opcode == 0x8B && opcode.modrm_mod != 3)
				{
					uint8_t regId = opcode.rex_r | opcode.modrm_reg;
					switch (regId)
					{
					case 0: exception_info->ContextRecord->Rax = 0; break;
					case 1: exception_info->ContextRecord->Rcx = 0; break;
					case 2: exception_info->ContextRecord->Rdx = 0; break;
					case 3: exception_info->ContextRecord->Rbx = 0; break;
					case 4: exception_info->ContextRecord->Rsp = 0; break;
					case 5: exception_info->ContextRecord->Rbp = 0; break;
					case 6: exception_info->ContextRecord->Rsi = 0; break;
					case 7: exception_info->ContextRecord->Rdi = 0; break;
					case 8: exception_info->ContextRecord->R8 = 0; break;
					case 9: exception_info->ContextRecord->R9 = 0; break;
					case 10: exception_info->ContextRecord->R10 = 0; break;
					case 11: exception_info->ContextRecord->R11 = 0; break;
					case 12: exception_info->ContextRecord->R12 = 0; break;
					case 13: exception_info->ContextRecord->R13 = 0; break;
					case 14: exception_info->ContextRecord->R14 = 0; break;
					case 15: exception_info->ContextRecord->R15 = 0; break;
					}
				}
			}
		}

		return EXCEPTION_CONTINUE_EXECUTION;
	}
}

static YimMenu::ExceptionHandler _ExceptionHandler{};