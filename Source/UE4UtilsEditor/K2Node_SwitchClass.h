// Copyright 2021 Cold Symmetry. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node_Switch.h"
#include "K2Node_SwitchClass.generated.h"

/**
 * Switch based on whether the passed in object is an instance of the class on each output pin.
 * Equivalent to a chain of Cast nodes, with the Cast Failed pin connecting to the next Cast.
 * The "As ObjectType" output pins will only have a value if the corresponding execute pin is taken.
 */
UCLASS()
class UE4UTILSEDITOR_API UK2Node_SwitchClass : public UK2Node_Switch
{
	GENERATED_BODY()
	
public:
	UK2Node_SwitchClass(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, Category = PinOptions)
	TArray<TSubclassOf<class UObject>> PinClasses;

	/** If true switch has pins to output a cast of the input object to the selected class. */
	UPROPERTY(EditAnywhere, Category = PinOptions)
	uint32 bHasCastPins : 1;

	UPROPERTY()
	TMap<FName, FName> ExecToCastPinMap;

	// UObject interface
	void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	// End of UObject interface

	// UEdGraphNode interface
	virtual FText GetTooltipText() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual bool ShouldShowNodeProperties() const override { return true; }
	// End of UEdGraphNode interface

	// UK2Node interface
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual class FNodeHandlingFunctor* CreateNodeHandler(class FKismetCompilerContext& CompilerContext) const override;
	// End of UK2Node interface

	// UK2Node_Switch Interface
	virtual void AddPinToSwitchNode() override;
	virtual FEdGraphPinType GetPinType() const override;
	virtual FEdGraphPinType GetInnerCaseType() const override;
	virtual FName GetPinNameGivenIndex(int32 Index) const override;
	// End of UK2Node_Switch Interface

protected:
	virtual void CreateFunctionPin() override;
	virtual void CreateSelectionPin() override;
	virtual void CreateCasePins() override;
	void CreateSingleCasePins(const TSubclassOf<class UObject>& PinClass);
	virtual void RemovePin(UEdGraphPin* TargetPin) override;
};
