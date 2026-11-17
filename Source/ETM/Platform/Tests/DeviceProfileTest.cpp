// Checks that Quest 3 and 3S get exactly the Quest 2 rendering settings (bible P-31, tracker ADR-0003).
// Owned by T-0046. Fails if an engine upgrade or a config edit makes the headsets diverge.
// Run: UnrealEditor-Cmd ETM.uproject -ExecCmds="Automation RunTests ETM.Platform; Quit" -unattended -nullrdr
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"

namespace
{
	const EAutomationTestFlags ETMPlatformTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	/** A profile's console variables, including everything inherited from its parent profiles. Later entries win. */
	TMap<FString, FString> CollectCVars(UDeviceProfile* Profile)
	{
		TArray<UDeviceProfile*> Chain;
		for (UDeviceProfile* Current = Profile; Current && Chain.Num() < 16; )
		{
			Chain.Insert(Current, 0); // root first
			Current = Current->BaseProfileName.IsEmpty() ? nullptr
				: UDeviceProfileManager::Get().FindProfile(Current->BaseProfileName, /*bCreateProfileOnFail*/ false);
		}
		TMap<FString, FString> Result;
		for (const UDeviceProfile* Link : Chain)
		{
			for (const FString& Entry : Link->CVars)
			{
				FString Name, Value;
				if (Entry.Split(TEXT("="), &Name, &Value))
				{
					Result.Add(Name.TrimStartAndEnd().ToLower(), Value.TrimStartAndEnd());
				}
			}
		}
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMDeviceProfilesTest, "ETM.Platform.DeviceProfiles.Quest3MatchesQuest2", ETMPlatformTestFlags)
bool FETMDeviceProfilesTest::RunTest(const FString&)
{
	UDeviceProfile* Quest2 = UDeviceProfileManager::Get().FindProfile(TEXT("Oculus_Quest2"), false);
	TestNotNull(TEXT("Engine has a Quest 2 profile"), Quest2);
	if (!Quest2) return false;
	const TMap<FString, FString> Baseline = CollectCVars(Quest2);
	TestEqual(TEXT("Quest 2 keeps low shadow quality (the floor)"), Baseline.FindRef(TEXT("sg.shadowquality")), FString(TEXT("1")));

	for (const TCHAR* Name : { TEXT("Meta_Quest_3"), TEXT("Meta_Quest_3S") })
	{
		UDeviceProfile* Profile = UDeviceProfileManager::Get().FindProfile(Name, false);
		TestNotNull(FString::Printf(TEXT("Engine has a %s profile"), Name), Profile);
		if (!Profile) continue;
		TestEqual(FString::Printf(TEXT("%s is an Android profile"), Name), Profile->DeviceType, FString(TEXT("Android")));

		const TMap<FString, FString> CVars = CollectCVars(Profile);
		for (const TPair<FString, FString>& Setting : Baseline)
		{
			const FString* Value = CVars.Find(Setting.Key);
			TestTrue(FString::Printf(TEXT("%s sets %s=%s like Quest 2 (has %s)"), Name, *Setting.Key, *Setting.Value, Value ? **Value : TEXT("nothing")),
				Value && *Value == Setting.Value);
		}
		for (const TPair<FString, FString>& Setting : CVars)
		{
			const FString* Value = Baseline.Find(Setting.Key);
			TestTrue(FString::Printf(TEXT("%s adds nothing Quest 2 lacks (%s=%s)"), Name, *Setting.Key, *Setting.Value),
				Value && *Value == Setting.Value);
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
