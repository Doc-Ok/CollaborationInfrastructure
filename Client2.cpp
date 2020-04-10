#include <stddef.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <string>
#include <stdexcept>
#include <Misc/SizedTypes.h>
#include <Misc/Vector.h>
#include <Misc/MessageLogger.h>

#include <Collaboration2/DataType.h>
#include <Collaboration2/Client.h>
#include <Collaboration2/Plugins/KoinoniaClient.h>

struct TestA
	{
	/* Elements: */
	public:
	bool flag;
	std::string text;
	Misc::Float64 matrix[4][4];
	
	/* Constructors and destructors: */
	TestA(bool sFlag,const char* sText,Misc::Float64 diag)
		:flag(sFlag),text(sText)
		{
		for(int i=0;i<4;++i)
			for(int j=0;j<4;++j)
				matrix[i][j]=i==j?diag:Misc::Float64(0);
		}
	};

struct TestB
	{
	/* Elements: */
	public:
	Misc::Vector<TestA> as;
	Misc::UInt32 size;
	
	/* Constructors and destructors: */
	TestB(void)
		{
		as.push_back(TestA(true,"What",1));
		as.push_back(TestA(false,"is",2));
		as.push_back(TestA(true,"going",3));
		as.push_back(TestA(false,"on",4));
		as.push_back(TestA(true,"here?",5));
		size=42;
		}
	};

struct TestC
	{
	/* Elements: */
	public:
	static const size_t numBs=20;
	TestB bs[numBs];
	};

struct Node
	{
	/* Elements: */
	public:
	Misc::UInt32 value;
	Node* left;
	Node* right;
	
	/* Constructors and destructors: */
	Node(Misc::UInt32 sValue)
		:value(sValue),
		 left(0),right(0)
		{
		}
	~Node(void)
		{
		delete left;
		delete right;
		}
	};

TestC* testObject=0;
Node* testTree=0;
KoinoniaProtocol::ObjectID testId=0;
KoinoniaProtocol::ObjectID treeId=0;

void shareData(KoinoniaClient* koinonia)
	{
	/* Initialize the test object: */
	testObject=new TestC;
	#if 0
	testTree=new Node(10);
	testTree->left=new Node(5);
	testTree->right=new Node(30);
	testTree->right->left=new Node(20);
	#endif
	
	/* Create a data type definition for the test object: */
	DataType testType;
	DataType::StructureElement testAElements[]=
		{
		{DataType::Bool,offsetof(TestA,flag)},
		{DataType::String,offsetof(TestA,text)},
		{testType.createFixedArray(4,testType.createFixedArray(4,DataType::Float64)),offsetof(TestA,matrix)}
		};
	DataType::TypeID testA=testType.createStructure(3,testAElements,sizeof(TestA));
	DataType::StructureElement testBElements[]=
		{
		{testType.createVector(testA),offsetof(TestB,as)},
		{DataType::UInt32,offsetof(TestB,size)}
		};
	DataType::TypeID testB=testType.createStructure(2,testBElements,sizeof(TestB));
	DataType::StructureElement testCElements[]=
		{
		{testType.createFixedArray(TestC::numBs,testB),offsetof(TestC,bs)}
		};
	DataType::TypeID testC=testType.createStructure(1,testCElements,sizeof(TestC));
	
	DataType::TypeID nodePointer=testType.createPointer();
	DataType::StructureElement nodeElements[]=
		{
		{DataType::UInt32,offsetof(Node,value)},
		{nodePointer,offsetof(Node,left)},
		{nodePointer,offsetof(Node,right)}
		};
	DataType::TypeID nodeT=testType.createStructure(3,nodeElements,sizeof(Node));
	testType.setPointerElementType(nodePointer,nodeT);
	
	/* Share the test object: */
	testId=koinonia->shareObject("TestObject",testType,testC,testObject,0,0);
	treeId=koinonia->shareObject("TestTree",testType,nodePointer,&testTree,0,0);
	}

int main(int argc,char* argv[])
	{
	/* Seed the random generator: */
	srand(time(0));
	
	/* Ignore SIGPIPE and leave handling of pipe errors to TCP sockets: */
	struct sigaction sigPipeAction;
	sigPipeAction.sa_handler=SIG_IGN;
	sigemptyset(&sigPipeAction.sa_mask);
	sigPipeAction.sa_flags=0x0;
	sigaction(SIGPIPE,&sigPipeAction,0);
	
	/* Create a client object: */
	Client client;
	
	/* Parse the command line: */
	std::string serverHostName=client.getDefaultServerHostName();
	int serverPort=client.getDefaultServerPort();
	std::string sessionPassword;
	for(int argi=1;argi<argc;++argi)
		{
		if(argv[argi][0]=='-')
			{
			if(strcasecmp(argv[argi]+1,"hostName")==0)
				{
				if(argi+1<argc)
					serverHostName=argv[argi+1];
				else
					Misc::formattedUserWarning("Client2: Ignoring dangling command line option %s",argv[argi]);
				
				++argi;
				}
			else if(strcasecmp(argv[argi]+1,"port")==0)
				{
				if(argi+1<argc)
					serverPort=atoi(argv[argi+1]);
				else
					Misc::formattedUserWarning("Client2: Ignoring dangling command line option %s",argv[argi]);
				
				++argi;
				}
			else if(strcasecmp(argv[argi]+1,"password")==0)
				{
				if(argi+1<argc)
					sessionPassword=argv[argi+1];
				else
					Misc::formattedUserWarning("Client2: Ignoring dangling command line option %s",argv[argi]);
				
				++argi;
				}
			else if(strcasecmp(argv[argi]+1,"name")==0)
				{
				if(argi+1<argc)
					client.requestNameChange(argv[argi+1]);
				else
					Misc::formattedUserWarning("Client2: Ignoring dangling command line option %s",argv[argi]);
				
				++argi;
				}
			else
				Misc::formattedUserWarning("Client2: Ignoring unrecognized command line option %s",argv[argi]);
			}
		else if(Client::isURI(argv[argi]))
			{
			if(!Client::parseURI(argv[argi],serverHostName,serverPort,sessionPassword))
				Misc::formattedUserWarning("Client2: Ignoring malformed server URI %s",argv[argi]);
			}
		else
			Misc::formattedUserWarning("Client2: Ignoring command line argument %s",argv[argi]);
		}
	
	/* Request some plug-in protocols: */
	client.requestPluginProtocol("Chat");
	client.requestPluginProtocol("Agora");
	
	/* Request the Koinonia protocol and share some data: */
	KoinoniaClient* koinonia=KoinoniaClient::requestClient(&client);
	if(koinonia!=0)
		shareData(koinonia);
	
	/* Stop the client on SIGINT and SIGTERM: */
	client.getDispatcher().stopOnSignals();
	
	/* Start the client protocol: */
	client.setPassword(sessionPassword);
	client.start(serverHostName,serverPort);
	
	/* Run the client: */
	client.run();
	
	/* Clean up and return: */
	delete testObject;
	delete testTree;
	return 0;
	}
