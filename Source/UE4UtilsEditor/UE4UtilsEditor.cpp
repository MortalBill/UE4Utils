// Copyright 2021 Cold Symmetry. All rights reserved.

#include "UE4UtilsEditor.h"

IMPLEMENT_GAME_MODULE(FUE4UtilsEditorModule, UE4UtilsEditor);
DEFINE_LOG_CATEGORY(UE4UtilsEditor)

#define LOCTEXT_NAMESPACE "UE4UtilsEditor"

void FUE4UtilsEditorModule::StartupModule()
{
	UE_LOG(UE4UtilsEditor, Log, TEXT("UE4UtilsEditor: Log Started"));
}

void FUE4UtilsEditorModule::ShutdownModule()
{
	UE_LOG(UE4UtilsEditor, Log, TEXT("UE4UtilsEditor: Log Ended"));
}

#undef LOCTEXT_NAMESPACE
