#pragma once

#include <Windows.h>

namespace FFIDMAN_API
{
	typedef void* (*GetIDManagerInstanceFunc)();
	typedef uint32_t (*GetNextIDLocalFunc)(void*, const char*);
	typedef void (*ReleaseIDLocalFunc)(void*, const char*, uint32_t);
	typedef void (*ReleaseAllLocalFunc)(void*, const char*);

	typedef uint32_t (*GetNextIDGlobalFunc)(void*, const char*);
	typedef void (*ReleaseIDGlobalFunc)(void*, const char*, uint32_t);
	typedef void (*ReleaseAllGlobalFunc)(void*, const char*);

	class Manager
	{
	private:
		HMODULE hModule{ LoadLibrary(TEXT("FakeFormNumberManager-NG.dll")) };
		void* ManagerInstancePtr{};

		// Retrieve function pointers
		GetIDManagerInstanceFunc GetIDManagerInstance = (GetIDManagerInstanceFunc)GetProcAddress(hModule, "GetIDManagerInstance");
		GetNextIDLocalFunc GetNextIDLocal = (GetNextIDLocalFunc)GetProcAddress(hModule, "GetNextIDLocal");
		ReleaseIDLocalFunc ReleaseIDLocal = (ReleaseIDLocalFunc)GetProcAddress(hModule, "ReleaseIDLocal");
		ReleaseAllLocalFunc ReleaseAllLocal = (ReleaseAllLocalFunc)GetProcAddress(hModule, "ReleaseAllLocal");

		GetNextIDGlobalFunc GetNextIDGlobal = (GetNextIDGlobalFunc)GetProcAddress(hModule, "GetNextIDGlobal");
		ReleaseIDGlobalFunc ReleaseIDGlobal = (ReleaseIDGlobalFunc)GetProcAddress(hModule, "ReleaseIDGlobal");
		ReleaseAllGlobalFunc ReleaseAllGlobal = (ReleaseAllGlobalFunc)GetProcAddress(hModule, "ReleaseAllGlobal");

		// Private constructor to prevent instantiation
		Manager() { ManagerInstancePtr = GetIDManagerInstance(); }

		// Delete copy constructor and assignment operator to prevent cloning
		Manager(const Manager&) = delete;
		Manager& operator=(const Manager&) = delete;

	public:
		auto GetNextFormIDLocal(const char* userIdent)
		{
			return static_cast<RE::FormID>(GetNextIDLocal(ManagerInstancePtr, userIdent));
		}
		void ReleaseFormIDLocal(RE::FormID id, const char* userIdent)
		{
			ReleaseIDLocal(ManagerInstancePtr, userIdent, static_cast<uint32_t>(id));
		}
		void ReleaseAllFormIDsLocal(const char* userIdent)
		{
			ReleaseAllLocal(ManagerInstancePtr, userIdent);
		}

		auto GetNextFormIDGlobal(const char* userIdent)
		{
			return static_cast<RE::FormID>(GetNextIDGlobal(ManagerInstancePtr, userIdent));
		}
		void ReleaseFormIDGlobal(RE::FormID id, const char* userIdent)
		{
			ReleaseIDGlobal(ManagerInstancePtr, userIdent, static_cast<uint32_t>(id));
		}
		void ReleaseAllFormIDsGlobal(const char* userIdent)
		{
			ReleaseAllGlobal(ManagerInstancePtr, userIdent);
		}

		static Manager* GetInstance()
		{
			static auto instance = new Manager();
			return instance;
		}

		inline bool IsLoaded() const { return hModule != nullptr && ManagerInstancePtr != nullptr; }
	};
}