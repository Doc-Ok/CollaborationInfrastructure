/***********************************************************************
VruiCoreTest - Vrui application to test the second-generation Vrui Core
collaboration protocol.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#include <stdlib.h>
#include <string.h>
#include <stdexcept>
#include <Misc/SizedTypes.h>
#include <Misc/MessageLogger.h>
#include <Threads/Thread.h>
#include <GLMotif/StyleSheet.h>
#include <GLMotif/PopupWindow.h>
#include <GLMotif/RowColumn.h>
#include <GLMotif/ToggleButton.h>
#include <GLMotif/TextField.h>
#include <GLMotif/DropdownBox.h>
#include <GLMotif/RadioBox.h>
#include <GLMotif/TextFieldSlider.h>
#include <Vrui/Vrui.h>

#include <Collaboration2/Plugins/KoinoniaClient.h>
#include <Collaboration2/CollaborativeVruiApplication.h>

class VruiCoreTest:public CollaborativeVruiApplication
	{
	/* Embedded classes: */
	private:
	struct SharedState // Some state shared between all clients in a shared session
		{
		/* Elements: */
		public:
		bool bool1;
		int int1;
		short int int2;
		double float1;
		std::string string1;
		};
	
	/* Elements: */
	private:
	KoinoniaClient* koinonia; // Koinonia plug-in protocol
	SharedState sharedState; // State shared between all clients
	KoinoniaProtocol::ObjectID sharedStateId; // Sharing ID of the shared state
	GLMotif::PopupWindow* dialog; // Dialog window to change shared state
	
	/* Private methods: */
	void shareState(void); // Shares shared state with all other clients
	static void sharedStateUpdateCallback(KoinoniaClient* client,KoinoniaProtocol::ObjectID id,void* object,void* userData);
	static void sharedStateUpdateCallback(Misc::CallbackData* cbData,void* userData);
	GLMotif::PopupWindow* createDialog(void);
	
	/* Constructors and destructors: */
	public:
	VruiCoreTest(int& argc,char**& argv);
	virtual ~VruiCoreTest(void);
	};

/*****************************
Methods of class VruiCoreTest:
*****************************/

void VruiCoreTest::shareState(void)
	{
	/* Create a data type definition for the shared state: */
	DataType sharedStateType;
	DataType::StructureElement sharedStateElements[]=
		{
		{DataType::Bool,offsetof(SharedState,bool1)},
		{DataType::SInt32,offsetof(SharedState,int1)},
		{DataType::SInt16,offsetof(SharedState,int2)},
		{DataType::Float64,offsetof(SharedState,float1)},
		{DataType::String,offsetof(SharedState,string1)}
		};
	DataType::TypeID sharedStateTypeId=sharedStateType.createStructure(5,sharedStateElements,sizeof(SharedState));
	
	/* Share the shared state: */
	sharedStateId=koinonia->shareObject("VruiCoreTest.sharedState",sharedStateType,sharedStateTypeId,&sharedState,&VruiCoreTest::sharedStateUpdateCallback,this);
	}

void VruiCoreTest::sharedStateUpdateCallback(KoinoniaClient* client,KoinoniaProtocol::ObjectID id,void* object,void* userData)
	{
	VruiCoreTest* thisPtr=static_cast<VruiCoreTest*>(userData);
	
	/* Update the UI: */
	thisPtr->dialog->updateVariables();
	}

void VruiCoreTest::sharedStateUpdateCallback(Misc::CallbackData* cbData,void* userData)
	{
	VruiCoreTest* thisPtr=static_cast<VruiCoreTest*>(userData);
	
	/* Update the shared state: */
	if(thisPtr->koinonia!=0)
		thisPtr->koinonia->replaceSharedObject(thisPtr->sharedStateId);
	}

GLMotif::PopupWindow* VruiCoreTest::createDialog(void)
	{
	GLMotif::PopupWindow* dialog=new GLMotif::PopupWindow("Dialog",Vrui::getWidgetManager(),"Shared State");
	dialog->setCloseButton(false);
	dialog->setResizableFlags(true,false);
	
	GLMotif::RowColumn* panel=new GLMotif::RowColumn("Panel",dialog,false);
	panel->setOrientation(GLMotif::RowColumn::VERTICAL);
	panel->setPacking(GLMotif::RowColumn::PACK_TIGHT);
	panel->setNumMinorWidgets(1);
	
	GLMotif::ToggleButton* bool1Toggle=new GLMotif::ToggleButton("Bool1Toggle",panel,"Bool 1");
	bool1Toggle->track(sharedState.bool1);
	bool1Toggle->updateVariables();
	bool1Toggle->getValueChangedCallbacks().add(&VruiCoreTest::sharedStateUpdateCallback,this);
	
	GLMotif::RadioBox* int1Box=new GLMotif::RadioBox("Int1Box",panel,false);
	int1Box->setOrientation(GLMotif::RowColumn::HORIZONTAL);
	int1Box->setPacking(GLMotif::RowColumn::PACK_TIGHT);
	int1Box->setNumMinorWidgets(1);
	int1Box->addToggle("Item 1");
	int1Box->addToggle("Item 2");
	int1Box->addToggle("Item 3");
	int1Box->track(sharedState.int1);
	int1Box->updateVariables();
	int1Box->getValueChangedCallbacks().add(&VruiCoreTest::sharedStateUpdateCallback,this);
	int1Box->manageChild();
	
	GLMotif::DropdownBox* int2Box=new GLMotif::DropdownBox("Int2Box",panel,false);
	int2Box->addItem("Item 1");
	int2Box->addItem("Item 2");
	int2Box->addItem("Item 3");
	int2Box->addItem("Item 4");
	int2Box->addItem("Item 5");
	int2Box->track(sharedState.int2);
	int2Box->updateVariables();
	int2Box->getValueChangedCallbacks().add(&VruiCoreTest::sharedStateUpdateCallback,this);
	int2Box->manageChild();
	
	GLMotif::TextFieldSlider* double1Slider=new GLMotif::TextFieldSlider("Double1Slider",panel,10,Vrui::getUiStyleSheet()->fontHeight*10.0f);
	double1Slider->setSliderMapping(GLMotif::TextFieldSlider::LINEAR);
	double1Slider->setValueType(GLMotif::TextFieldSlider::FLOAT);
	double1Slider->setValueRange(0.0,10.0,0.0);
	double1Slider->track(sharedState.float1);
	double1Slider->updateVariables();
	double1Slider->getValueChangedCallbacks().add(&VruiCoreTest::sharedStateUpdateCallback,this);
	
	GLMotif::TextField* string1Field=new GLMotif::TextField("String1Field",panel,20);
	string1Field->track(sharedState.string1);
	string1Field->updateVariables();
	string1Field->setEditable(true);
	string1Field->getValueChangedCallbacks().add(&VruiCoreTest::sharedStateUpdateCallback,this);
	
	panel->manageChild();
	
	return dialog;
	}

VruiCoreTest::VruiCoreTest(int& argc,char**& argv)
	:CollaborativeVruiApplication(argc,argv),
	 koinonia(0),
	 dialog(0)
	{
	/* Initialize the shared state: */
	sharedState.bool1=true;
	sharedState.int1=1;
	sharedState.int2=2;
	sharedState.float1=3.141592654;
	sharedState.string1="Hello, World!";
	
	/* Create the shared state update dialog: */
	dialog=createDialog();
	Vrui::popupPrimaryWidget(dialog);
	
	/* Request the Koinonia protocol and share the shared state: */
	koinonia=KoinoniaClient::requestClient(&client);
	if(koinonia!=0)
		shareState();
	
	/* Start the collaboration back end: */
	startClient();
	}

VruiCoreTest::~VruiCoreTest(void)
	{
	delete dialog;
	}

/****************
Main entry point:
****************/

VRUI_APPLICATION_RUN(VruiCoreTest)
