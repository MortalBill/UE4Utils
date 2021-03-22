// Copyright 2021 Cold Symmetry. All rights reserved.

#include "K2Node_SwitchClass.h"

#include "BlueprintNodeSpawner.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "EdGraphUtilities.h"
#include "Kismet/KismetMathLibrary.h"
#include "KismetCompiler.h"

#define LOCTEXT_NAMESPACE "K2Node_SwitchClass"

// A minor rewrite of FKCHandler_Switch. This compiles the node into operations that can be executed in-game.
// Can't inherit, since it's declared and defined in the cpp.
class FKCHandler_SwitchClass : public FNodeHandlingFunctor
{
protected:
	TMap<UEdGraphNode*, FBPTerminal*> BoolTermMap;

	// We will need to invert a boolean.
	UFunction* BooleanNotFunction;

public:
	FKCHandler_SwitchClass(FKismetCompilerContext& InCompilerContext)
		: FNodeHandlingFunctor(InCompilerContext)
	{
		BooleanNotFunction = FindUField<UFunction>(UKismetMathLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Not_PreBool));
	}

	virtual void RegisterNets(FKismetFunctionContext& Context, UEdGraphNode* Node) override
	{
		UK2Node_Switch* SwitchNode = Cast<UK2Node_Switch>(Node);

		FNodeHandlingFunctor::RegisterNets(Context, Node);

		// Create a term to determine if the compare was successful or not
		//@TODO: Ideally we just create one ever, not one per switch
		FBPTerminal* BoolTerm = Context.CreateLocalTerminal();
		BoolTerm->Type.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		BoolTerm->Source = Node;
		BoolTerm->Name = Context.NetNameMap->MakeValidName(Node, TEXT("CmpSuccess"));
		BoolTermMap.Add(Node, BoolTerm);
	}

	virtual void RegisterNet(FKismetFunctionContext& Context, UEdGraphPin* Net) override
	{
		FBPTerminal* Term = Context.CreateLocalTerminalFromPinAutoChooseScope(Net, Context.NetNameMap->MakeValidName(Net));
		Context.NetMap.Add(Net, Term);
	}
	
	// This function is a copy, up to the rewrite o the for PinIt loop.
	virtual void Compile(FKismetFunctionContext& Context, UEdGraphNode* Node) override
	{
		UK2Node_SwitchClass* SwitchNode = CastChecked<UK2Node_SwitchClass>(Node);

		FEdGraphPinType ExpectedExecPinType;
		ExpectedExecPinType.PinCategory = UEdGraphSchema_K2::PC_Exec;

		// Make sure that the input pin is connected and valid for this block
		UEdGraphPin* ExecTriggeringPin = Context.FindRequiredPinByName(SwitchNode, UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		if ((ExecTriggeringPin == NULL) || !Context.ValidatePinType(ExecTriggeringPin, ExpectedExecPinType))
		{
			CompilerContext.MessageLog.Error(*LOCTEXT("NoValidExecutionPinForSwitch_Error", "@@ must have a valid execution pin @@").ToString(), SwitchNode, ExecTriggeringPin);
			return;
		}

		// Make sure that the selection pin is connected and valid for this block
		UEdGraphPin* SelectionPin = SwitchNode->GetSelectionPin();
		if ((SelectionPin == NULL) || !Context.ValidatePinType(SelectionPin, SwitchNode->GetPinType()))
		{
			CompilerContext.MessageLog.Error(*LOCTEXT("NoValidSelectionPinForSwitch_Error", "@@ must have a valid execution pin @@").ToString(), SwitchNode, SelectionPin);
			return;
		}

		// Find the boolean intermediate result term, so we can track whether the compare was successful
		FBPTerminal* BoolTerm = BoolTermMap.FindRef(SwitchNode);

		// Generate the output impulse from this node
		UEdGraphPin* SwitchSelectionNet = FEdGraphUtilities::GetNetFromPin(SelectionPin);
		FBPTerminal* SwitchSelectionTerm = Context.NetMap.FindRef(SwitchSelectionNet);

		if ((BoolTerm != NULL) && (SwitchSelectionTerm != NULL))
		{
			UEdGraphNode* TargetNode = NULL;
			UEdGraphPin* FuncPin = SwitchNode->GetFunctionPin();
			FBPTerminal* FuncContext = Context.NetMap.FindRef(FuncPin);
			UEdGraphPin* DefaultPin = SwitchNode->GetDefaultPin();

			// We don't need to generate if checks if there are no connections to it if there is no default pin or if the default pin is not linked 
			// If there is a default pin that is linked then it would fall through to that default if we do not generate the cases
			const bool bCanSkipUnlinkedCase = (DefaultPin == nullptr || DefaultPin->LinkedTo.Num() == 0);

			// NOTE: Here begin the differences with FKCHandler_Switch.

			// Run thru all the output pins except for the default label
			for (auto PinIt = SwitchNode->Pins.CreateIterator(); PinIt; ++PinIt)
			{
				UEdGraphPin* ExecPin = *PinIt;

				if ((ExecPin->Direction == EGPD_Output)
					&& (ExecPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
					&& (ExecPin != DefaultPin)
					&& (!bCanSkipUnlinkedCase || ExecPin->LinkedTo.Num() > 0))
				{
					UEdGraphPin* CastOutputPin = nullptr;
					const FName& CastPinName = SwitchNode->ExecToCastPinMap[ExecPin->PinName];
					for (auto InnerPinIt = SwitchNode->Pins.CreateIterator(); InnerPinIt; ++InnerPinIt)
					{
						UEdGraphPin* ThisPin = *InnerPinIt;
						if (ThisPin->PinName == CastPinName)
						{
							CastOutputPin = ThisPin;
						}
					}

					check(CastOutputPin);

					FBPTerminal** CastResultTerm = Context.NetMap.Find(CastOutputPin);
					check(CastResultTerm && *CastResultTerm);

					// Create a term for the switch case value
					FBPTerminal* ClassTerm = new FBPTerminal();
					Context.Literals.Add(ClassTerm);
					ClassTerm->ObjectLiteral = ExecPin->DefaultObject;
					ClassTerm->Type = SwitchNode->GetInnerCaseType();
					ClassTerm->SourcePin = ExecPin;
					ClassTerm->bIsLiteral = true;

					// TODO: Handle KCST_DynamicCast vs. Interface/Meta like DynamicCastHandler.cpp
					// Cast to the appropriate class
					FBlueprintCompiledStatement& CastStatement = Context.AppendStatementForNode(Node);
					CastStatement.Type = KCST_DynamicCast;
					CastStatement.LHS = *CastResultTerm;
					CastStatement.RHS.Add(ClassTerm);
					CastStatement.RHS.Add(SwitchSelectionTerm);

					// Check result of cast statement
					FBlueprintCompiledStatement& CheckResultStatement = Context.AppendStatementForNode(Node);
					CheckResultStatement.Type = KCST_ObjectToBool;
					CheckResultStatement.LHS = BoolTerm;
					CheckResultStatement.RHS.Add(*CastResultTerm);

					// Invert the bool, for a GotoIfNot
					FBlueprintCompiledStatement& NotStatement = Context.AppendStatementForNode(SwitchNode);
					NotStatement.Type = KCST_CallFunction;
					NotStatement.FunctionToCall = BooleanNotFunction;
					NotStatement.FunctionContext = nullptr;
					NotStatement.bIsParentContext = false;
					NotStatement.LHS = BoolTerm;
					NotStatement.RHS.Add(BoolTerm);

					// Jump to output if cast succeeded
					FBlueprintCompiledStatement& IfFailTest_SucceedAtBeingEqualGoto = Context.AppendStatementForNode(SwitchNode);
					IfFailTest_SucceedAtBeingEqualGoto.Type = KCST_GotoIfNot;
					IfFailTest_SucceedAtBeingEqualGoto.LHS = BoolTerm;

					Context.GotoFixupRequestMap.Add(&IfFailTest_SucceedAtBeingEqualGoto, ExecPin);
				}
			}

			// Finally output default pin
			GenerateSimpleThenGoto(Context, *SwitchNode, DefaultPin);

			// TODO: Consider adding default output data pin
		}
		else
		{
			CompilerContext.MessageLog.Error(*LOCTEXT("ResolveTermPassed_Error", "Failed to resolve term passed into @@").ToString(), SelectionPin);
		}
	}
};

UK2Node_SwitchClass::UK2Node_SwitchClass(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bHasCastPins = true;
}

void UK2Node_SwitchClass::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	bool bIsDirty = false;
	FName PropertyName = (PropertyChangedEvent.Property != NULL) ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	if (PropertyName == TEXT("PinClasses"))
	{
		bIsDirty = true;
	}

	if (PropertyName == TEXT("bHasCastPins"))
	{
		bIsDirty = true;
		if (!bHasCastPins)
		{
			for (UEdGraphPin* PossibleCastPin : Pins)
			{
				if ((PossibleCastPin->Direction == EGPD_Output)
					&& (PossibleCastPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object))
				{
					const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
					K2Schema->BreakPinLinks(*PossibleCastPin, true);
				}
			}
		}
	}

	if (bIsDirty)
	{
		ReconstructNode();
		GetGraph()->NotifyGraphChanged();
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

FText UK2Node_SwitchClass::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return NSLOCTEXT("K2Node", "Switch_Class", "Switch on Class");
}

FText UK2Node_SwitchClass::GetTooltipText() const
{
	return NSLOCTEXT("K2Node", "SwitchClass_ToolTip", "Selects an output that matches the class of the input value");
}

// Copied unchanged from other Switch subclasses.
void UK2Node_SwitchClass::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	// actions get registered under specific object-keys; the idea is that 
	// actions might have to be updated (or deleted) if their object-key is  
	// mutated (or removed)... here we use the node's class (so if the node 
	// type disappears, then the action should go with it)
	UClass* ActionKey = GetClass();
	// to keep from needlessly instantiating a UBlueprintNodeSpawner, first   
	// check to make sure that the registrar is looking for actions of this type
	// (could be regenerating actions for a specific asset, and therefore the 
	// registrar would only accept actions corresponding to that asset)
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		check(NodeSpawner != nullptr);

		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

// Register our custom compile handler
class FNodeHandlingFunctor* UK2Node_SwitchClass::CreateNodeHandler(class FKismetCompilerContext& CompilerContext) const
{
	return new FKCHandler_SwitchClass(CompilerContext);
}

void UK2Node_SwitchClass::CreateFunctionPin()
{
	// Not used, since we built our own compile.
}

void UK2Node_SwitchClass::CreateSelectionPin()
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	UEdGraphPin* Pin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, UObject::StaticClass(), GetSelectionPinName());
	K2Schema->SetPinAutogeneratedDefaultValueBasedOnType(Pin);
}

FEdGraphPinType UK2Node_SwitchClass::GetPinType() const 
{ 
	FEdGraphPinType PinType;
	PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
	PinType.PinSubCategoryObject = UObject::StaticClass();
	return PinType;
}

FEdGraphPinType UK2Node_SwitchClass::GetInnerCaseType() const
{
	FEdGraphPinType PinType;
	PinType.PinCategory = UEdGraphSchema_K2::PC_Class;
	return PinType;
}

FName UK2Node_SwitchClass::GetPinNameGivenIndex(int32 Index) const
{
	return FName(PinClasses[Index]->GetDisplayNameText().ToString());
}

void UK2Node_SwitchClass::CreateCasePins()
{
	ExecToCastPinMap.Empty();
	for (const TSubclassOf<class UObject>& PinClass : PinClasses)
	{
		// When we add a new pin, it'll be briefly null.
		if (PinClass)
		{
			CreateSingleCasePins(PinClass);
		}
	}
}

void UK2Node_SwitchClass::CreateSingleCasePins(const TSubclassOf<class UObject>& PinClass)
{
	UEdGraphPin* ExecPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, FName(PinClass->GetDisplayNameText().ToString()));
	ExecPin->DefaultObject = PinClass;

	UEdGraphPin* CastPin;

	const FString CastResultPinName = UEdGraphSchema_K2::PN_CastedValuePrefix + PinClass->GetDisplayNameText().ToString();
	if (PinClass->IsChildOf(UInterface::StaticClass()))
	{
		CastPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Interface, *PinClass, *CastResultPinName);
	}
	else
	{
		CastPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Object, *PinClass, *CastResultPinName);
	}

	// Mild hack: Instead of getting rid of the pins, we just break and hide them, so the compile is the same.
	CastPin->bHidden = !bHasCastPins;

	ExecToCastPinMap.Add(ExecPin->PinName, CastPin->PinName);
}

void UK2Node_SwitchClass::AddPinToSwitchNode()
{
	PinClasses.Emplace(UObject::StaticClass());
	CreateSingleCasePins(UObject::StaticClass());
}

void UK2Node_SwitchClass::RemovePin(UEdGraphPin* TargetPin)
{
	checkSlow(TargetPin);

	for (UClass* PinClass : PinClasses)
	{
		if (FName(PinClass->GetDisplayNameText().ToString()) == TargetPin->PinName)
		{
			PinClasses.Remove(PinClass);
			break;
		}
	}
}

#undef LOCTEXT_NAMESPACE
