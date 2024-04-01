#pragma once

#include <Windows.h>

namespace FFIDMAN_API
{
	typedef void* (*GetIDManagerInstanceFunc)();
	typedef uint32_t (*GetNextIDFunc)(void*);
	typedef void (*ReleaseIDFunc)(void*, uint32_t);

	class Manager
	{
	private:
		HMODULE hModule{ LoadLibrary(TEXT("FakeFormNumberManager-NG.dll")) };
		void* ManagerInstancePtr{};

		// Retrieve function pointers
		GetIDManagerInstanceFunc GetIDManagerInstance = (GetIDManagerInstanceFunc)GetProcAddress(hModule, "GetIDManagerInstance");
		GetNextIDFunc GetNextID = (GetNextIDFunc)GetProcAddress(hModule, "GetNextID");
		ReleaseIDFunc ReleaseID = (ReleaseIDFunc)GetProcAddress(hModule, "ReleaseID");

		// Private constructor to prevent instantiation
		Manager() { ManagerInstancePtr = GetIDManagerInstance(); }

		// Delete copy constructor and assignment operator to prevent cloning
		Manager(const Manager&) = delete;
		Manager& operator=(const Manager&) = delete;

	public:
		auto GetNextFormID()
		{
			return static_cast<RE::FormID>(GetNextID(ManagerInstancePtr));
		}
		void ReleaseFormID(RE::FormID id)
		{
			ReleaseID(ManagerInstancePtr, static_cast<uint32_t>(id));
		}

		static Manager* GetInstance()
		{
			static auto instance = new Manager();
			return instance;
		}

		inline bool IsLoaded() const { return hModule != nullptr && ManagerInstancePtr != nullptr; }
	};
}