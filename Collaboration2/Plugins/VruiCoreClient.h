/***********************************************************************
VruiCoreClient - Client for the core Vrui collaboration protocol, which
represents remote users' physical VR environments, and maintains their
head and input device positions/orientations and navigation
transformations.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#ifndef PLUGINS_VRUICORECLIENT_INCLUDED
#define PLUGINS_VRUICORECLIENT_INCLUDED

#include <string>
#include <vector>
#include <Misc/HashTable.h>
#include <Misc/Pipe.h>
#include <Misc/ConfigurationFile.h>
#include <Threads/EventDispatcher.h>
#include <GLMotif/Pager.h>
#include <GLMotif/ToggleButton.h>
#include <GLMotif/TextField.h>
#include <GLMotif/ListBox.h>
#include <SceneGraph/GraphNode.h>
#include <SceneGraph/GroupNode.h>
#include <SceneGraph/TransformNode.h>
#include <SceneGraph/ONTransformNode.h>
#include <SceneGraph/DOGTransformNode.h>
#include <SceneGraph/BillboardNode.h>
#include <SceneGraph/FancyFontStyleNode.h>
#include <Vrui/Vrui.h>
#include <Vrui/InputDevice.h>
#include <Vrui/Viewer.h>
#include <Vrui/InputDeviceManager.h>
#include <Vrui/InputGraphManager.h>

#include <Collaboration2/MessageReader.h>
#include <Collaboration2/Client.h>
#include <Collaboration2/VruiPluginClient.h>
#include <Collaboration2/Plugins/VruiCoreProtocol.h>

/* Forward declarations: */
class GLContextData;
namespace GLMotif {
class PopupWindow;
class Label;
}
namespace SceneGraph {
class ONTransformNode;
class BillboardNode;
class MaterialNode;
class FancyTextNode;
}
namespace Vrui {
class ToolFactory;
}
class NonBlockSocket;
class MessageContinuation;

class VruiCoreClient:public VruiPluginClient,public VruiCoreProtocol
	{
	/* Embedded classes: */
	private:
	class SharedEnvironment; // Forward declaration
	
	public:
	class RemoteClient:public PluginClient::RemoteClient
		{
		friend class VruiCoreClient;
		
		/* Embedded classes: */
		public:
		struct DeviceState:public ClientInputDeviceState // Structure for device states with scene graph representation
			{
			friend class VruiCoreClient;
			friend class RemoteClient;
			
			/* Elements: */
			private:
			SceneGraph::TransformNodePointer root; // Root node of device's scene graph
			ONTransform glyphTransform; // Transformation from device to its scene graph
			};
		
		typedef std::vector<DeviceState> DeviceList; // Type for lists of input devices
		private:
		typedef Misc::HashTable<unsigned int,size_t> DeviceIndexMap; // Type for hash tables mapping input device IDs to indices in the device list
		
		/* Elements: */
		unsigned int id; // Client's ID, replicated from back-end structures
		std::string name; // Client's name, replicated from back-end structures
		SharedEnvironment* sharedEnvironment; // A shared physical environment of which this client is part, or 0
		ClientEnvironment environment; // Client's physical environment definition
		Point floorCenter; // Projection of the client's display center onto its floor plane along the up direction
		Rotation baseRotation; // Rotation from client's physical space to a frame where z is the up direction, the (y, z) plane contains the forward direction, and x points to the right
		ClientViewerConfig viewerConfig; // Client's viewer state configuration
		ClientViewerState viewerState; // Client's viewer state
		RemoteClient* navLockedClient; // Another client to whom this client's navigation transformation is locked, or null if unlocked
		NavTransform navTransform; // Client's current navigation transformation, or relative transformation if locked to another client
		DeviceList devices; // List of client's input devices
		DeviceIndexMap deviceIndexMap; // Map from input device IDs to indices in the device list
		unsigned int hideMainViewerCount; // Counter to hide main viewer glyph; is only drawn when count is zero
		unsigned int hideNameTagCount; // Counter to hide name tag; is only drawn when count is zero
		unsigned int hideDevicesCount; // Counter to hide device glyphs; are only drawn when count is zero
		SceneGraph::DOGTransformNodePointer physicalRoot; // Pointer to root of scene graph linked to client's physical space
		SceneGraph::ONTransformNode* headRoot; // Pointer to root of scene graph linked to client's head transformation
		SceneGraph::TransformNodePointer eyes[2]; // Pointers to scene graphs to render the client's left and right eyes
		SceneGraph::BillboardNodePointer nameTag; // Pointer to scene graph to render the client's name tag
		SceneGraph::TransformNode* nameTagTransform; // Pointer to scene graph to render the client's name tag
		SceneGraph::MaterialNode* nameTagMaterial; // Material properties for the client's name tag
		SceneGraph::FancyTextNode* nameTagText; // Pointer to a text node rendering the client's name above their head
		
		/* Private methods: */
		void updateEnvironmentFromVrui(void); // Updates the client's environment definition by reading Vrui's current environment
		void updateEnvironment(MessageReader& message); // Updates the client's environment definition from an environment structure pending in the given message
		void updateViewerConfigFromVrui(void); // Updates the client's viewer configuration by reading Vrui's current main viewer configuration
		void updateViewerConfig(MessageReader& message); // Updates the client's viewer configuration from a viewer configuration structure pending in the given message
		void updateViewerState(MessageReader& message); // Updates the client's viewer state from a viewer state structure pending in the given message
		
		/* Constructors and destructors: */
		RemoteClient(unsigned int sId); // Creates an uninitialized remote client state with the given ID
		RemoteClient(VruiCoreClient* client,MessageReader& message); // Creates a remote client state by reading the given message
		
		/* Methods: */
		public:
		unsigned int getId(void) const // Returns the remote client's ID
			{
			return id;
			}
		const std::string& getName(void) const // Returns the remote client's name
			{
			return name;
			}
		const ONTransform& getHeadTransform(void) const // Returns the main viewer's position and orientation
			{
			return viewerState.headTransform;
			}
		void setNavTransform(const NavTransform& newNavTransform) // Sets the client's current absolute or relative navigation transformation
			{
			navTransform=newNavTransform;
			}
		NavTransform getNavTransform(void) const // Returns the client's navigation transformation
			{
			NavTransform result=navTransform;
			
			if(navLockedClient!=0)
				{
				/* Follow the client's lock chain to calculate its full navigation transformation: */
				const RemoteClient* lockClient=navLockedClient;
				do
					{
					result*=lockClient->navTransform;
					lockClient=lockClient->navLockedClient;
					}
				while(lockClient!=0);
				
				result.renormalize();
				}
			
			return result;
			}
		const DeviceList& getDevices(void) const // Returns the client's input device list
			{
			return devices;
			}
		void hideMainViewer(void); // Requests hiding the main viewer glyph; must be paired with showMainViewer call
		void showMainViewer(void); // Requests showing the main viewer glyph; must be paired with hideMainViewer call
		void hideNameTag(void); // Requests hiding the name tag; must be paired with showNameTag call
		void showNameTag(void); // Requests showing the name tag; must be paired with hideNameTag call
		void hideDevices(void); // Requests hiding the device glyphs; must be paired with showDevices call
		void showDevices(void); // Requests showing the device glyphs; must be paired with hideDevices call
		SceneGraph::GroupNode& getPhysicalRoot(void) const // Returns root of client's physical-space scene graph
			{
			return *physicalRoot;
			}
		SceneGraph::GroupNode& getHeadRoot(void) const // Returns root of client's head-space scene graph
			{
			return *headRoot;
			}
		};
	
	private:
	class SharedEnvironment // Class representing a shared physical environment where all participating clients have the same navigation transformation
		{
		friend class VruiCoreClient;
		
		/* Elements: */
		private:
		unsigned int id; // The shared environment's ID
		std::vector<RemoteClient*> clients; // List of clients that are part of this shared physical environment
		RemoteClient* navigatingClient; // The client that is currently performing a navigation sequence, or 0
		RemoteClient* lockingClient; // The client that initiated the lock currently held by all clients that are part of this shared physical environment, or 0
		
		/* Constructors and destructors: */
		SharedEnvironment(unsigned int sId)
			:id(sId),navigatingClient(0),lockingClient(0)
			{
			}
		};
	
	typedef Misc::HashTable<unsigned int,SharedEnvironment> SharedEnvironmentMap; // Type for hash tables mapping shared environment IDs to shared environments
	typedef std::vector<RemoteClient*> RemoteClientList; // Type for lists of remote client state structures
	typedef Misc::HashTable<unsigned int,RemoteClient*> RemoteClientMap; // Type for hash tables mapping remote client IDs to remote client structures
	typedef Misc::HashTable<Vrui::InputDevice*,size_t> InputDeviceIndexMap; // Type for hash tables mapping Vrui input device pointers to indices in the client's input device list
	
	enum NavSequenceState // Enumerated type for states of navigation sequences
		{
		Idle=0,
		PendingToolActive, // Waiting for start reply; navigation tool is active
		PendingToolInactive, // Waiting for start reply; navigation tool is inactive
		PendingSingleton, // Waiting for start reply on one-off navigation transformation update
		PendingSingletonUpdated, // Waiting for start reply on one-of navigation transformation update; there has been another one-off update
		PendingSingletonToolActive, // Waiting for start reply on one-off navigation transformation update; navigation tool has become active
		PendingSingletonUpdatedToolActive, // Waiting for start reply on one-off navigation transformation update; navigation tool has become active, and there has been another update request
		Granted, // Start request has been granted and navigation tool is still active
		Denied // Start request has been denied and navigation tool is still active
		};
	
	/* Elements: */
	private:
	bool initialized; // Flag if the plug-in client has already been initialized
	std::vector<VruiPluginClient*> dependentProtocols; // List of protocols depending on the Vrui Core protocol's services
	Misc::ConfigurationFileSection vruiCoreConfig; // Configuration file section holding Vrui Core settings, and containing sub-sections for dependent protocols
	
	/* Front-end state: */
	RemoteClientList remoteClients; // List of remote clients used by the front-end, in alphabetical order by name (to match control dialog list box)
	RemoteClientMap remoteClientMap; // Map from remote client IDs to remote client structures
	RemoteClient self; // A partial representation of this client as a pseudo-remote client to simplify handling navigation lock chains etc.
	SharedEnvironmentMap sharedEnvironmentMap; // Map of shared environments
	
	/* Front-end state for navigation exclusion management in shared physical environments: */
	bool navigationTransformationLocked; // Flag indicating whether the Vrui Core client is holding a lock on Vrui's navigation transformation
	unsigned int noNavigationUpdateCallback; // Flag that an update to Vrui's navigation transformation must be ignored by the update callback if value >0
	NavSequenceState navSequenceState; // Current state of local navigation sequence
	NavTransform savedNavTransform; // Navigation transformation saved when nav sequence start request was sent
	NavTransform singletonNavTransform; // Navigation transformation originally requested by a one-off update
	NavTransform requestedNavTransform; // Most recent requested navigation transformation while nav sequence start reply is pending
	
	InputDeviceID lastDeviceId; // Last ID assigned to an input device of this client
	std::vector<Vrui::InputDevice*> inputDevices; // List of Vrui input devices represented by Vrui Core
	InputDeviceIndexMap inputDeviceIndexMap; // Map from Vrui input device pointers to indices in the self-representation's device list
	int followMode; // Mode in which the local client follows the nav-locked client: 0: free, 1: aligned, 2: facing 1:1
	Vrui::InputDevice* mainViewerHeadDevice; // Pointer to input device currently tracking the main viewer, or null if main viewer is not head-tracked
	bool drawRemoteMainViewers; // Flag whether to draw representations of remote users' main viewers
	bool drawRemoteNameTags; // Flag whether to draw remote users' name tags
	bool drawRemoteDevices; // Flag whether to draw representations of remote users' input devices
	GLMotif::Button* showCollaborationDialogButton; // Button in Vrui's system menu to show the collaboration dialog
	bool firstShown; // Flag indicating the first time the collaboration dialog is shown
	GLMotif::PopupWindow* collaborationDialog; // Dialog window to control the Vrui Core protocol and client protocols
	GLMotif::ListBox* remoteClientList; // List box showing the names of all remote Vrui Core clients
	GLMotif::Pager* settingsPager; // Multi-page notebook for plug-in protocols' settings
	GLMotif::TextField* clientNameTextField;
	GLMotif::Label* sharedEnvironmentStatusLabel;
	GLMotif::ToggleButton* clientFollowsYouToggle;
	GLMotif::ToggleButton* followClientToggle;
	GLMotif::ToggleButton* alignWithClientToggle;
	GLMotif::ToggleButton* faceClientToggle;
	Vrui::ToolFactory* collaborationToolBase; // Abstract tool factory to collect collaboration-related tools
	SceneGraph::GroupNodePointer physicalRoot; // Pointer to the root of the local client's physical-space scene graph
	SceneGraph::DOGTransformNode* navigationalRoot; // Pointer to root of shared navigational-space scene graph
	SceneGraph::GraphNodePointer eye; // Scene graph to render a creepy eye
	SceneGraph::FancyFontStyleNodePointer font; // Font style node to render name tags above remote clients' heads
	SceneGraph::GraphNodePointer device; // Default scene graph to render a remote device
	Vector deviceOffset; // Offset vector from device scene graph's hot spot to its origin
	
	/* Private methods: */
	RemoteClient* getRemoteClient(MessageReader& message) // Returns the remote client state of the client whose ID is next up in the given message
		{
		/* Read the client's ID from the message and look it up in the client map: */
		return remoteClientMap.getEntry(message.read<ClientID>()).getDest();
		}
	void setNavTransform(const NavTransform& newNavTransform) // Sets Vrui's navigation transformation to the given transformation without notifying the server
		{
		++noNavigationUpdateCallback;
		Vrui::setNavigationTransformation(Vrui::NavTransform(newNavTransform));
		--noNavigationUpdateCallback;
		}
	void sendNavTransform(const NavTransform& newNavTransform); // Sends the given navigation transformation to the server as an update request
	
	/* Methods receiving messages from the server: */
	void nameChangeReplyCallback(Client::NameChangeReplyCallbackData* cbData); // Called from the back end when the server sends a reply to this client's name change request
	void nameChangeNotificationCallback(Client::NameChangeNotificationCallbackData* cbData); // Called from the back end when a remote client changed its name
	
	/* Methods receiving status messages from the back-end: */
	void startNotificationCallback(unsigned int messageId,MessageReader& message); // Notification that the Vrui Core client is connected to a server and ready to forward server messages
	void registerProtocolNotificationCallback(unsigned int messageId,MessageReader& message); // Notification that a new dependent protocol registered itself with the Vrui Core protocol
	void nameChangeReplyNotificationCallback(unsigned int messageId,MessageReader& message); // Notification that this client changed its name
	void clientConnectNotificationCallback(unsigned int messageId,MessageReader& message); // Notification that a new remote Vrui Core client connected
	void clientNameChangeNotificationCallback(unsigned int messageId,MessageReader& message); // Notification that a remote Vrui Core client changed its name
	void clientDisconnectNotificationCallback(unsigned int messageId,MessageReader& message); // Notification that a remote Vrui Core client disconnected
	
	/* Methods receiving forwarded messages in the front end: */
	void connectReplyCallback(unsigned int messageId,MessageReader& message);
	void connectNotificationCallback(unsigned int messageId,MessageReader& message);
	void environmentUpdateNotificationCallback(unsigned int messageId,MessageReader& message);
	void viewerConfigUpdateNotificationCallback(unsigned int messageId,MessageReader& message);
	void viewerUpdateNotificationCallback(unsigned int messageId,MessageReader& message);
	void startNavSequenceReplyCallback(unsigned int messageId,MessageReader& message);
	void startNavSequenceNotificationCallback(unsigned int messageId,MessageReader& message);
	void navTransformUpdateNotificationCallback(unsigned int messageId,MessageReader& message);
	void stopNavSequenceNotificationCallback(unsigned int messageId,MessageReader& message);
	void lockNavTransformReplyCallback(unsigned int messageId,MessageReader& message);
	void lockNavTransformNotificationCallback(unsigned int messageId,MessageReader& message);
	void unlockNavTransformNotificationCallback(unsigned int messageId,MessageReader& message);
	void createInputDeviceNotificationCallback(unsigned int messageId,MessageReader& message);
	void updateInputDeviceRayNotificationCallback(unsigned int messageId,MessageReader& message);
	void updateInputDeviceNotificationCallback(unsigned int messageId,MessageReader& message);
	void disableInputDeviceNotificationCallback(unsigned int messageId,MessageReader& message);
	void enableInputDeviceNotificationCallback(unsigned int messageId,MessageReader& message);
	void destroyInputDeviceNotificationCallback(unsigned int messageId,MessageReader& message);
	
	/* Methods receiving notifications when some parts of Vrui state changes: */
	void environmentDefinitionChangedCallback(Vrui::EnvironmentDefinitionChangedCallbackData* cbData); // Callback called when Vrui's physical environment definition changes
	void mainViewerConfigurationChangedCallback(Vrui::Viewer::ConfigChangedCallbackData* cbData); // Callback called when the configuration of Vrui's main viewer changes
	void mainViewerTrackingCallback(Vrui::InputDevice::CallbackData* cbData); // Callback called when the main viewer's head tracking device changes its transformation
	void navigationTransformationChangedCallback(Vrui::NavigationTransformationChangedCallbackData* cbData); // Callback called when Vrui's navigation transformation changes
	void navigationToolActivationCallback(Vrui::NavigationToolActivationCallbackData* cbData); // Callback called when a navigation tool is activated or deactivated
	unsigned int addInputDevice(Vrui::InputDevice* inputDevice,bool force); // Creates a Vrui Core representation for the given input device; forces representation of virtual devices when flag is true; returns new device ID
	void removeInputDevice(Vrui::InputDevice* inputDevice); // Removes the Vrui Core representation for the given input device
	void inputDeviceCreationCallback(Vrui::InputDeviceManager::InputDeviceCreationCallbackData* cbData); // Callback called when a new input device is created
	void inputDeviceDestructionCallback(Vrui::InputDeviceManager::InputDeviceDestructionCallbackData* cbData); // Callback called when an input device is destroyed
	void inputDeviceStateChangeCallback(Vrui::InputGraphManager::InputDeviceStateChangeCallbackData* cbData); // Callback called when a represented input device is enabled or disabled
	void inputDeviceRayDirectionCallback(Vrui::InputDevice::CallbackData* cbData); // Callback called when a represented input device changes its pointing ray direction
	void inputDeviceTrackingCallback(Vrui::InputDevice::CallbackData* cbData); // Callback called when a represented input device changes its transformation
	
	/* User interface methods: */
	static void changeNameCommandCallback(const char* argumentBegin,const char* argumentEnd,void* userData);
	void showCollaborationDialogCallback(Misc::CallbackData* cbData);
	void unlockNavTransform(bool sendMessage =true);
	void requestNavTransformLock(RemoteClient* lockClient,const NavTransform& lockTransform);
	void updateVruiCoreSettingsPage(void);
	void remoteClientListValueChangedCallback(GLMotif::ListBox::ValueChangedCallbackData* cbData);
	void clientNameValueChangedCallback(GLMotif::TextField::ValueChangedCallbackData* cbData);
	void drawRemoteMainViewersValueChangedCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData);
	void drawRemoteNameTagsValueChangedCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData);
	void drawRemoteDevicesValueChangedCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData);
	void followClientValueChangedCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData);
	void alignWithClientValueChangedCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData);
	void faceClientValueChangedCallback(GLMotif::ToggleButton::ValueChangedCallbackData* cbData);
	void createCollaborationDialog(const std::string& sharedEnvironmentName); // Creates the collaboration control and configuration dialog window
	
	/* Initialization methods: */
	virtual void initialize(void); // Initializes a Vrui Core client plug-in after it has been installed
	
	/* Constructors and destructors: */
	public:
	VruiCoreClient(Client* sClient);
	virtual ~VruiCoreClient(void);
	
	/* Methods from class PluginClient: */
	virtual const char* getName(void) const;
	virtual unsigned int getVersion(void) const;
	virtual unsigned int getNumClientMessages(void) const;
	virtual unsigned int getNumServerMessages(void) const;
	virtual void setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase);
	virtual void start(void);
	virtual void clientConnected(unsigned int clientId);
	virtual void clientDisconnected(unsigned int clientId);
	
	/* Methods from class VruiPluginClient: */
	virtual void frame(void);
	virtual void display(GLContextData& contextData) const;
	virtual void sound(ALContextData& contextData) const;
	virtual void shutdown(void);
	
	/* New methods: */
	static VruiCoreClient* requestClient(Client* client) // Returns a Vrui Core protocol client
		{
		/* Request the Vrui Core protocol client and cast it to the correct type: */
		VruiCoreClient* result=static_cast<VruiCoreClient*>(client->requestPluginProtocol(VRUICORE_PROTOCOLNAME,VRUICORE_PROTOCOLVERSION));
		
		/* Initialize and return the protocol client: */
		result->initialize();
		return result;
		}
	static VruiCoreClient* getClient(Client* client) // Returns a Vrui Core protocol client that has previously been registered with the given collaboration client
		{
		/* Find the Vrui Core protocol client and cast it to the correct type: */
		VruiCoreClient* result=static_cast<VruiCoreClient*>(client->findPluginProtocol(VRUICORE_PROTOCOLNAME,VRUICORE_PROTOCOLVERSION));
		
		/* Initialize and return the protocol client: */
		if(result!=0)
			result->initialize();
		return result;
		}
	Misc::ConfigurationFileSection getProtocolConfig(const char* protocolName) const // Returns a configuration section for a dependent protocol of the given name
		{
		return vruiCoreConfig.getSection(protocolName);
		}
	
	/* Back-end interface: */
	virtual void registerDependentProtocol(VruiPluginClient* newProtocol); // Registers a new protocol that depends on the Vrui Core protocol's services
	
	/* Front-end interface: */
	const RemoteClient* getRemoteClient(unsigned int clientId) const // Returns the Vrui Core state of the remote client with the given ID
		{
		return remoteClientMap.getEntry(clientId).getDest();
		}
	
	/* UI interface: */
	GLMotif::Pager* getSettingsPager(void) // Returns the pager widget where dependent protocols can create their settings UI
		{
		return settingsPager;
		}
	GLMotif::ListBox* getRemoteClientList(void) // Returns the list box widget containing the names of currently connected remote clients
		{
		return remoteClientList;
		}
	RemoteClient* getSelectedRemoteClient(void) // Returns the remote client currently selected in the client list box, or null
		{
		if(remoteClientList->getSelectedItem()>=0)
			return remoteClients[remoteClientList->getSelectedItem()];
		else
			return 0;
		}
	virtual unsigned int getInputDeviceId(Vrui::InputDevice* device); // Returns the input device ID assigned to the given input device
	virtual Vrui::ToolFactory* getCollaborationToolBase(void); // Returns an abstract tool factory to use as base class for collaboration-related tools
	SceneGraph::GroupNode& getPhysicalRoot(void) // Returns the root of the local client's physical-space scene graph
		{
		return *physicalRoot;
		}
	SceneGraph::GroupNode& getNavigationalRoot(void) // Returns the root of the shared navigational-space scene graph
		{
		return *navigationalRoot;
		}
	};

#endif
