#pragma once

#include <Windows.h>

namespace FFIDMAN_API
{
	typedef void* (*GetIDManagerInstanceFunc)();
	typedef uint32_t (*GetNextIDFunc)(void*, const char*);
	typedef void (*ReleaseIDFunc)(void*, const char*, uint32_t);
	typedef void (*ReleaseAllFunc)(void*, const char*);

	class Manager
	{
	private:
		HMODULE hModule{ LoadLibrary(TEXT("FakeFormNumberManager-NG.dll")) };
		void* ManagerInstancePtr{};

		// Retrieve function pointers
		GetIDManagerInstanceFunc GetIDManagerInstance = (GetIDManagerInstanceFunc)GetProcAddress(hModule, "GetIDManagerInstance");
		GetNextIDFunc GetNextID = (GetNextIDFunc)GetProcAddress(hModule, "GetNextID");
		ReleaseIDFunc ReleaseID = (ReleaseIDFunc)GetProcAddress(hModule, "ReleaseID");
		ReleaseAllFunc ReleaseAll = (ReleaseAllFunc)GetProcAddress(hModule, "ReleaseAll");

		// Private constructor to prevent instantiation
		Manager() { ManagerInstancePtr = GetIDManagerInstance(); }

		// Delete copy constructor and assignment operator to prevent cloning
		Manager(const Manager&) = delete;
		Manager& operator=(const Manager&) = delete;

	public:
		auto GetNextFormID(const char* userIdent)
		{
			return static_cast<RE::FormID>(GetNextID(ManagerInstancePtr, userIdent));
		}
		void ReleaseFormID(RE::FormID id, const char* userIdent)
		{
			ReleaseID(ManagerInstancePtr, userIdent, static_cast<uint32_t>(id));
		}
		void ReleaseAllFormIDs(const char* userIdent)
		{
			ReleaseAll(ManagerInstancePtr, userIdent);
		}

		static Manager* GetInstance()
		{
			static auto instance = new Manager();
			return instance;
		}

		inline bool IsLoaded() const { return hModule != nullptr && ManagerInstancePtr != nullptr; }
	};
}