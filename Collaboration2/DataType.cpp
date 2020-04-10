/***********************************************************************
DataType - Class to define data types and data objects for automatic
transmission over binary pipes.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#include <Collaboration2/DataType.h>
#include <Collaboration2/DataType.icpp>

#include <string>
#include <stdexcept>
#include <Misc/Utility.h>

#include <Collaboration2/MessageReader.h>
#include <Collaboration2/MessageWriter.h>
#include <Collaboration2/MessageEditor.h>
#include <Collaboration2/MessageContinuation.h>
#include <Collaboration2/NonBlockSocket.h>

/*****************************************************
Declaration of class DataType::ReadObjectContinuation:
*****************************************************/

class DataType::ReadObjectContinuation:public MessageContinuation
	{
	friend class DataType;
	
	/* Embedded classes: */
	private:
	enum State // Enumerated type for reader states
		{
		ReadFixedAtomic,
		ReadVarIntFirst,
		ReadVarInt,
		ReadStringLength,
		ReadString,
		ReadPointerValid,
		ReadPointer,
		ReadFixedArray,
		ReadVectorSize,
		ReadVector,
		ReadStructure
		};
	
	struct Frame // Structure representing a stack frame of reading a sub-object
		{
		/* Elements: */
		public:
		State state; // State of sub-object reader
		TypeID type; // Current sub-object's type
		void* object; // Pointer to current sub-object's memory representation
		union
			{
			struct
				{
				Misc::UInt32 value; // Value of the VarInt object currently being read
				unsigned int remaining; // Number of VarInt bytes left to read
				} varInt; // State to read a VarInt
			struct
				{
				Misc::UInt32 remaining; // Number of characters remaining to be read
				} string; // State to read a String
			struct
				{
				Misc::UInt32 remaining; // Number of fixed array or vector elements remaining to be read
				char* elementPtr; // Pointer to the current fixed array or vector element
				} array; // State to read a fixed array or a vector
			struct
				{
				size_t elementIndex; // Index of the structure element
				} structure; // State to read a structure
			};
		};
	
	/* Elements: */
	const DataType& dataType; // The data type defining the object being read
	Frame frames[128]; // Sub-object reading state stack
	Frame* top; // Pointer to currently active sub-object reading stack frame
	size_t numBytesNeeded; // Number of bytes needed in the socket buffer before being able to continue
	
	/* Elements: */
	
	/* Constructors and destructors: */
	ReadObjectContinuation(const DataType& sDataType)
		:dataType(sDataType),
		 top(frames-1)
		{
		}
	
	/* Methods: */
	bool incomplete(void) const
		{
		return top>=frames;
		}
	void startSubObject(TypeID type,void* object)
		{
		/* Start a new stack frame: */
		++top;
		if(top==frames+128)
			throw std::runtime_error("DataType::continueReading: Data type nested too deeply");
		
		/* Initialize the stack frame: */
		top->type=type;
		top->object=object;
		if(type<NumAtomicTypes)
			{
			if(type<VarInt)
				{
				/* Read a fixed-size atomic value: */
				top->state=ReadFixedAtomic;
				numBytesNeeded=DataType::atomicTypeMinSizes[type];
				}
			else if(type==VarInt)
				{
				/* Read a variable-sized integer: */
				top->state=ReadVarIntFirst;
				numBytesNeeded=sizeof(Misc::UInt8);
				}
			else
				{
				/* Read a string: */
				top->state=ReadStringLength;
				
				/* Read the string length as a VarInt: */
				startSubObject(VarInt,&top->string.remaining);
				}
			}
		else
			{
			/* Access the compound type: */
			const CompoundType& ct=dataType.compoundTypes[type-NumAtomicTypes];
			switch(ct.type)
				{
				case CompoundType::Pointer:
					
					/* Read a pointer's valid flag: */
					top->state=ReadPointerValid;
					numBytesNeeded=sizeof(WireBool);
					
					break;
				
				case CompoundType::FixedArray:
					
					/* Read a fixed-size array: */
					top->state=ReadFixedArray;
					top->array.remaining=Misc::UInt32(ct.fixedArray.numElements);
					top->array.elementPtr=static_cast<char*>(top->object);
					
					/* Start reading the first array element: */
					startSubObject(ct.fixedArray.elementType,object);
					
					break;
				
				case CompoundType::Vector:
					
					/* Read a vector: */
					top->state=ReadVectorSize;
					
					/* Read the vector size as a VarInt: */
					startSubObject(VarInt,&top->array.remaining);
					
					break;
				
				case CompoundType::Structure:
					{
					/* Read a structure: */
					top->state=ReadStructure;
					top->structure.elementIndex=0;
					
					/* Start reading the first structure element: */
					const StructureElement& se=ct.structure.elements[0];
					startSubObject(se.type,static_cast<char*>(object)+se.memOffset);
					
					break;
					}
				
				default:
					/* Can't happen, just to make compiler happy: */
					;
				}
			}
		}
	void finishSubObject(void)
		{
		/* Go back to the previous stack frame and bail out if the stack is now empty: */
		--top;
		if(top==frames-1)
			return;
		
		/* Continue reading the parent of the just-finished sub-object: */
		bool keepReading=true;
		switch(top->state)
			{
			case ReadStringLength:
				{
				/* Initialize the string object: */
				std::string& string=*static_cast<std::string*>(top->object);
				string.clear();
				
				/* Check if the string is non-empty: */
				keepReading=top->string.remaining>0;
				if(keepReading)
					{
					/* Start reading the string's characters: */
					string.reserve(top->string.remaining);
					top->state=ReadString;
					numBytesNeeded=sizeof(Char);
					}
				
				break;
				}
			
			case ReadPointer:
				
				/* Done reading: */
				keepReading=false;
				
				break;
			
			case ReadFixedArray:
			
				/* Check if there are more array elements: */
				keepReading=--top->array.remaining>0;
				if(keepReading)
					{
					/* Start reading the next array element: */
					const CompoundType& ct=dataType.compoundTypes[top->type-NumAtomicTypes];
					top->array.elementPtr+=dataType.getMemSize(ct.fixedArray.elementType);
					startSubObject(ct.fixedArray.elementType,top->array.elementPtr);
					}
				
				break;
			
			case ReadVectorSize:
				{
				/* Re-initialize the vector: */
				Misc::VectorBase& vector=*static_cast<Misc::VectorBase*>(top->object);
				const CompoundType& ct=dataType.compoundTypes[top->type-NumAtomicTypes];
				size_t elementSize=dataType.getMemSize(ct.vector.elementType);
				if(top->array.remaining>vector.capacity())
					{
					/* Deinitialize all current vector elements: */
					char* elements=static_cast<char*>(vector.getElements());
					char* elementsEnd=elements+vector.size()*elementSize;
					for(char* elementPtr=elements;elementPtr!=elementsEnd;elementPtr+=elementSize)
						dataType.deinitObject(ct.vector.elementType,elementPtr);
					
					/* Reallocate the vector: */
					vector.reallocate(top->array.remaining,elementSize);
					vector.setSize(0);
					}
				if(top->array.remaining>vector.size())
					{
					/* Initialize all new vector elements: */
					char* elements=static_cast<char*>(vector.getElements())+vector.size()*elementSize;
					char* elementsEnd=elements+(top->array.remaining-vector.size())*elementSize;
					for(char* elementPtr=elements;elementPtr!=elementsEnd;elementPtr+=elementSize)
						dataType.initObject(ct.vector.elementType,elementPtr);
					}
				else if(top->array.remaining<vector.size())
					{
					/* Deinitialize all new vector elements: */
					char* elements=static_cast<char*>(vector.getElements())+top->array.remaining*elementSize;
					char* elementsEnd=elements+(vector.size()-top->array.remaining)*elementSize;
					for(char* elementPtr=elements;elementPtr!=elementsEnd;elementPtr+=elementSize)
						dataType.deinitObject(ct.vector.elementType,elementPtr);
					}
				vector.setSize(top->array.remaining);
				
				/* Check if the vector is non-empty: */
				keepReading=top->array.remaining>0;
				if(keepReading)
					{
					/* Start reading the first vector element: */
					top->state=ReadVector;
					top->array.elementPtr=static_cast<char*>(vector.getElements());
					startSubObject(ct.vector.elementType,top->array.elementPtr);
					}
				
				break;
				}
			
			case ReadVector:
				
				/* Check if there are more vector elements: */
				keepReading=--top->array.remaining>0;
				if(keepReading)
					{
					/* Start reading the next vector element: */
					const CompoundType& ct=dataType.compoundTypes[top->type-NumAtomicTypes];
					top->array.elementPtr+=dataType.getMemSize(ct.vector.elementType);
					startSubObject(ct.vector.elementType,top->array.elementPtr);
					}
				
				break;
			
			case ReadStructure:
				{
				/* Go to the next structure element and check if there are more: */
				const CompoundType& ct=dataType.compoundTypes[top->type-NumAtomicTypes];
				keepReading=++top->structure.elementIndex<ct.structure.numElements;
				if(keepReading)
					{
					/* Start reading the next structure element: */
					const StructureElement& se=ct.structure.elements[top->structure.elementIndex];
					startSubObject(se.type,static_cast<char*>(top->object)+se.memOffset);
					}
				
				break;
				}
				
			default:
				/* Can't happen, just to make compiler happy: */
				;
			}
		
		/* Finish the compound object itself if it is complete: */
		if(!keepReading)
			finishSubObject();
		}
	};

/*********************************
Static elements of class DataType:
*********************************/

const size_t DataType::atomicTypeMinSizes[DataType::NumAtomicTypes]=
	{
	sizeof(Misc::UInt8),
	sizeof(Misc::UInt8),
	sizeof(Misc::SInt8),sizeof(Misc::SInt16),sizeof(Misc::SInt32),sizeof(Misc::SInt64),
	sizeof(Misc::UInt8),sizeof(Misc::UInt16),sizeof(Misc::UInt32),sizeof(Misc::UInt64),
	sizeof(Misc::Float32),sizeof(Misc::Float64),
	sizeof(Misc::UInt8), // Wire representation of VarInt is 1-5 bytes
	sizeof(Misc::UInt8), // Wire representation of std::string is a VarInt length field followed by the string's characters
	};

const size_t DataType::atomicTypeAlignments[DataType::NumAtomicTypes]=
	{
	sizeof(bool),
	sizeof(char),
	sizeof(Misc::SInt8),sizeof(Misc::SInt16),sizeof(Misc::SInt32),sizeof(Misc::SInt64),
	sizeof(Misc::UInt8),sizeof(Misc::UInt16),sizeof(Misc::UInt32),sizeof(Misc::UInt64),
	sizeof(Misc::Float32),sizeof(Misc::Float64),
	sizeof(Misc::UInt32), // Memory representation of VarInt is 4 bytes
	sizeof(std::string::value_type*) // Assuming std::string's alignment is that of its pointer type
	};

const size_t DataType::atomicTypeMemSizes[DataType::NumAtomicTypes]=
	{
	sizeof(bool),
	sizeof(char),
	sizeof(Misc::SInt8),sizeof(Misc::SInt16),sizeof(Misc::SInt32),sizeof(Misc::SInt64),
	sizeof(Misc::UInt8),sizeof(Misc::UInt16),sizeof(Misc::UInt32),sizeof(Misc::UInt64),
	sizeof(Misc::Float32),sizeof(Misc::Float64),
	sizeof(Misc::UInt32), // Memory representation of VarInt is 4 bytes
	sizeof(std::string)
	};

/*************************
Methods of class DataType:
*************************/

void DataType::initObject(DataType::TypeID type,void* object) const
	{
	/* Check if the given type is atomic: */
	if(type<NumAtomicTypes)
		{
		/* The only atomic type that needs initialization is String: */
		if(type==String)
			{
			/* Create a new std::string object at the object's location: */
			new(object) std::string;
			}
		}
	else
		{
		/* Access the compound type: */
		const CompoundType& ct=compoundTypes[type-NumAtomicTypes];
		switch(ct.type)
			{
			case CompoundType::Pointer:
				
				/* Initialize the pointer to null: */
				*static_cast<void**>(object)=0;
				
				break;
			
			case CompoundType::FixedArray:
				{
				/* Initialize all elements of the fixed array: */
				size_t elementSize=getMemSize(ct.fixedArray.elementType);
				char* elementEnd=static_cast<char*>(object)+ct.fixedArray.numElements*elementSize;
				for(char* elementPtr=static_cast<char*>(object);elementPtr!=elementEnd;elementPtr+=elementSize)
					initObject(ct.fixedArray.elementType,elementPtr);
				
				break;
				}
			
			case CompoundType::Vector:
				/* Initialize the vector: */
				static_cast<Misc::VectorBase*>(object)->init();
				
				break;
			
			case CompoundType::Structure:
				{
				/* Initialize all elements of the structure: */
				char* elementPtr=static_cast<char*>(object);
				const StructureElement* seEnd=ct.structure.elements+ct.structure.numElements;
				for(const StructureElement* sePtr=ct.structure.elements;sePtr!=seEnd;++sePtr)
					initObject(sePtr->type,elementPtr+sePtr->memOffset);
				
				break;
				}
			
			default:
				/* Can't happen, just to make compiler happy: */
				;
			}
		}
	}

void DataType::deinitObject(DataType::TypeID type,void* object) const
	{
	/* Check if the given type is atomic: */
	if(type<NumAtomicTypes)
		{
		/* The only atomic type that needs de-initialization is String: */
		if(type==String)
			{
			/* Destroy the std::string object at the object's location: */
			static_cast<std::string*>(object)->std::string::~string();
			}
		}
	else
		{
		/* Access the compound type: */
		const CompoundType& ct=compoundTypes[type-NumAtomicTypes];
		switch(ct.type)
			{
			case CompoundType::Pointer:
				{
				/* Check if the pointer is valid: */
				void* target=*static_cast<void**>(object);
				if(target!=0)
					{
					/* Delete the pointed-to object: */
					destroyObject(ct.pointer.elementType,target);
					}
				
				break;
				}
			
			case CompoundType::FixedArray:
				{
				/* De-initialize all elements of the fixed array: */
				size_t elementSize=getMemSize(ct.fixedArray.elementType);
				char* elementEnd=static_cast<char*>(object)+ct.fixedArray.numElements*elementSize;
				for(char* elementPtr=static_cast<char*>(object);elementPtr!=elementEnd;elementPtr+=elementSize)
					deinitObject(ct.fixedArray.elementType,elementPtr);
				
				break;
				}
			
			case CompoundType::Vector:
				{
				Misc::VectorBase& vec=*static_cast<Misc::VectorBase*>(object);
				
				/* De-initialize all elements of the vector: */
				size_t elementSize=getMemSize(ct.vector.elementType);
				char* elementEnd=static_cast<char*>(vec.getElements())+vec.size()*elementSize;
				for(char* elementPtr=static_cast<char*>(vec.getElements());elementPtr!=elementEnd;elementPtr+=elementSize)
					deinitObject(ct.vector.elementType,elementPtr);
				
				/* De-initialize the vector: */
				vec.~VectorBase();
				
				break;
				}
			
			case CompoundType::Structure:
				{
				/* De-initialize all elements of the structure: */
				char* elementPtr=static_cast<char*>(object);
				const StructureElement* seEnd=ct.structure.elements+ct.structure.numElements;
				for(const StructureElement* sePtr=ct.structure.elements;sePtr!=seEnd;++sePtr)
					deinitObject(sePtr->type,elementPtr+sePtr->memOffset);
				
				break;
				}
			
			default:
				/* Can't happen, just to make compiler happy: */
				;
			}
		}
	}

DataType& DataType::operator=(const DataType& source)
	{
	/* Check for aliasing: */
	if(this!=&source)
		{
		/* Replace the compound type vector with a new one: */
		std::vector<CompoundType> oldCompoundTypes;
		std::swap(oldCompoundTypes,compoundTypes);
		compoundTypes.reserve(source.compoundTypes.size());
		
		/* Copy all compound type definitions: */
		for(std::vector<CompoundType>::const_iterator sctIt=source.compoundTypes.begin();sctIt!=source.compoundTypes.end();++sctIt)
			{
			/* Copy the source's compound type: */
			CompoundType newType;
			newType.type=sctIt->type;
			newType.fixedSize=sctIt->fixedSize;
			switch(newType.type)
				{
				case CompoundType::Pointer:
					newType.pointer.elementType=sctIt->pointer.elementType;
					break;
				
				case CompoundType::FixedArray:
					newType.fixedArray.numElements=sctIt->fixedArray.numElements;
					newType.fixedArray.elementType=sctIt->fixedArray.elementType;
					break;
				
				case CompoundType::Vector:
					newType.vector.elementType=sctIt->vector.elementType;
					break;
				
				case CompoundType::Structure:
					newType.structure.numElements=sctIt->structure.numElements;
					newType.structure.elements=new StructureElement[newType.structure.numElements];
					for(size_t i=0;i<newType.structure.numElements;++i)
						newType.structure.elements[i]=sctIt->structure.elements[i];
					break;
				}
			newType.minSize=sctIt->minSize;
			newType.alignment=sctIt->alignment;
			newType.memSize=sctIt->memSize;
			compoundTypes.push_back(newType);
			}
		
		/* Destroy all previous compound data type definitions: */
		for(std::vector<CompoundType>::iterator ctIt=oldCompoundTypes.begin();ctIt!=oldCompoundTypes.end();++ctIt)
			if(ctIt->type==CompoundType::Structure)
				delete[] ctIt->structure.elements;
		}
	
	return *this;
	}

DataType::~DataType(void)
	{
	/* Destroy all compound data type definitions: */
	for(std::vector<CompoundType>::iterator ctIt=compoundTypes.begin();ctIt!=compoundTypes.end();++ctIt)
		if(ctIt->type==CompoundType::Structure)
			delete[] ctIt->structure.elements;
	}

bool DataType::operator==(const DataType& other) const
	{
	/* Check if the two data type definitions have the same number of compound types: */
	if(compoundTypes.size()!=other.compoundTypes.size())
		return false;
	
	/* Compare all compound types for equivalence: */
	std::vector<CompoundType>::const_iterator ctIt=compoundTypes.begin();
	std::vector<CompoundType>::const_iterator octIt=other.compoundTypes.begin();
	for(;ctIt!=compoundTypes.end();++ctIt,++octIt)
		{
		/* Check if the two compound types have the same type: */
		if(ctIt->type!=octIt->type)
			return false;
		
		switch(ctIt->type)
			{
			case CompoundType::Pointer:
				
				/* Compare the pointer element type: */
				if(ctIt->pointer.elementType!=octIt->pointer.elementType)
					return false;
				
				break;
			
			case CompoundType::FixedArray:
				
				/* Compare the number of array elements and the array element type: */
				if(ctIt->fixedArray.numElements!=octIt->fixedArray.numElements||ctIt->fixedArray.elementType!=octIt->fixedArray.elementType)
					return false;
				
				break;
			
			case CompoundType::Vector:
				
				/* Compare the vector element type: */
				if(ctIt->vector.elementType!=octIt->vector.elementType)
					return false;
				
				break;
			
			case CompoundType::Structure:
				
				/* Compare the number of structure elements: */
				if(ctIt->structure.numElements!=octIt->structure.numElements)
					return false;
				
				/* Compare the structure element types: */
				for(size_t i=0;i<ctIt->structure.numElements;++i)
					if(ctIt->structure.elements[i].type!=octIt->structure.elements[i].type)
						return false;
				
				break;
			}
		}
	
	return true;
	}

DataType::TypeID DataType::createPointer(void)
	{
	/* Ensure that there are not too many data types: */
	if(NumAtomicTypes+compoundTypes.size()>maxTypeId)
		throw std::runtime_error("DataType::createPointer: Too many type definitions");
	
	/* Create the new compound type: */
	TypeID result=TypeID(NumAtomicTypes+compoundTypes.size());
	CompoundType newType;
	newType.type=CompoundType::Pointer;
	newType.fixedSize=false;
	newType.pointer.elementType=result; // Since pointers can't point to themselves, this means "undefined"
	newType.minSize=sizeof(WireBool);
	newType.alignment=sizeof(void*);
	newType.memSize=sizeof(void*);
	compoundTypes.push_back(newType);
	
	return result;
	}

void DataType::setPointerElementType(DataType::TypeID pointerType,DataType::TypeID elementType)
	{
	/* Ensure that the pointer type is defined, is a pointer, has an undefined element type, and that the element type is defined: */
	if(pointerType>=NumAtomicTypes+compoundTypes.size())
		throw std::runtime_error("DataType::setPointerElementType: Undefined pointer type");
	CompoundType& ct=compoundTypes[pointerType-NumAtomicTypes];
	if(ct.type!=CompoundType::Pointer)
		throw std::runtime_error("DataType::setPointerElementType: Pointer type is not a pointer");
	if(ct.pointer.elementType!=pointerType)
		throw std::runtime_error("DataType::setPointerElementType: Pointer type's element type is already defined");
	if(elementType==pointerType)
		throw std::runtime_error("DataType::setPointerElementType: Invalid element type");
	if(elementType>=NumAtomicTypes+compoundTypes.size())
		throw std::runtime_error("DataType::setPointerElementType: Undefined element type");
	
	/* Set the pointer type's element type: */
	ct.pointer.elementType=elementType;
	}

DataType::TypeID DataType::createPointer(DataType::TypeID elementType)
	{
	/* Ensure that there are not too many data types: */
	if(NumAtomicTypes+compoundTypes.size()>maxTypeId)
		throw std::runtime_error("DataType::createPointer: Too many type definitions");
		
	/* Ensure that all base types are already defined: */
	if(elementType>=NumAtomicTypes+compoundTypes.size())
		throw std::runtime_error("DataType::createPointer: Undefined element type");
	
	/* Create the new compound type: */
	TypeID result=TypeID(NumAtomicTypes+compoundTypes.size());
	CompoundType newType;
	newType.type=CompoundType::Pointer;
	newType.fixedSize=false;
	newType.pointer.elementType=elementType;
	newType.minSize=sizeof(WireBool);
	newType.alignment=sizeof(void*);
	newType.memSize=sizeof(void*);
	compoundTypes.push_back(newType);
	
	return result;
	}

DataType::TypeID DataType::createFixedArray(size_t numElements,DataType::TypeID elementType)
	{
	/* Ensure that there are not too many data types: */
	if(NumAtomicTypes+compoundTypes.size()>maxTypeId)
		throw std::runtime_error("DataType::createFixedArray: Too many type definitions");
	
	/* Ensure that there is at least one array element: */
	if(numElements==0U)
		throw std::runtime_error("DataType::createFixedArray: Zero-element arrays not allowed");
	
	/* Ensure that the number of array elements is not too large: */
	if(numElements>1U<<16)
		throw std::runtime_error("DataType::createFixedArray: Too many array elements");
		
	/* Ensure that all base types are already defined: */
	if(elementType>=NumAtomicTypes+compoundTypes.size())
		throw std::runtime_error("DataType::createFixedArray: Undefined element type");
	
	/* Create the new compound type: */
	TypeID result=TypeID(NumAtomicTypes+compoundTypes.size());
	CompoundType newType;
	newType.type=CompoundType::FixedArray;
	newType.fixedSize=hasFixedSize(elementType);
	newType.fixedArray.numElements=numElements;
	newType.fixedArray.elementType=elementType;
	newType.minSize=numElements*getMinSize(elementType);
	newType.alignment=getAlignment(elementType);
	newType.memSize=numElements*getMemSize(elementType);
	compoundTypes.push_back(newType);
	
	return result;
	}

DataType::TypeID DataType::createVector(DataType::TypeID elementType)
	{
	/* Ensure that there are not too many data types: */
	if(NumAtomicTypes+compoundTypes.size()>maxTypeId)
		throw std::runtime_error("DataType::createVector: Too many type definitions");
	
	/* Ensure that all base types are already defined: */
	if(elementType>=NumAtomicTypes+compoundTypes.size())
		throw std::runtime_error("DataType::createVector: Undefined element type");
	
	/* Create the new compound type: */
	TypeID result=TypeID(NumAtomicTypes+compoundTypes.size());
	CompoundType newType;
	newType.type=CompoundType::Vector;
	newType.fixedSize=false;
	newType.vector.elementType=elementType;
	newType.minSize=atomicTypeMinSizes[VarInt];
	newType.alignment=sizeof(void*);
	newType.memSize=sizeof(Misc::VectorBase);
	compoundTypes.push_back(newType);
	
	return result;
	}

DataType::TypeID DataType::createStructure(size_t numElements,const DataType::StructureElement elements[],size_t memSize)
	{
	/* Ensure that there are not too many data types: */
	if(NumAtomicTypes+compoundTypes.size()>maxTypeId)
		throw std::runtime_error("DataType::createStructure: Too many type definitions");
	
	/* Ensure that the number of structure elements is not too large: */
	if(numElements>1U<<8)
		throw std::runtime_error("DataType::createStructure: Too many structure elements");
	
	/* Ensure that all base types are already defined: */
	for(size_t i=0;i<numElements;++i)
		if(elements[i].type>=NumAtomicTypes+compoundTypes.size())
			throw std::runtime_error("DataType::createStructure: Undefined element type");
	
	/* Create the new compound type: */
	TypeID result=TypeID(NumAtomicTypes+compoundTypes.size());
	CompoundType newType;
	newType.type=CompoundType::Structure;
	newType.fixedSize=true;
	newType.structure.numElements=numElements;
	newType.structure.elements=new StructureElement[numElements];
	newType.minSize=0;
	newType.alignment=1;
	for(size_t i=0;i<numElements;++i)
		{
		newType.fixedSize=newType.fixedSize&&hasFixedSize(elements[i].type);
		newType.structure.elements[i]=elements[i];
		newType.minSize+=getMinSize(elements[i].type);
		if(newType.alignment<getAlignment(elements[i].type))
			newType.alignment=getAlignment(elements[i].type);
		}
	newType.memSize=memSize;
	compoundTypes.push_back(newType);
	
	return result;
	}

DataType::TypeID DataType::createStructure(const std::vector<DataType::StructureElement> elements,size_t memSize)
	{
	/* Ensure that there are not too many data types: */
	if(NumAtomicTypes+compoundTypes.size()>maxTypeId)
		throw std::runtime_error("DataType::createStructure: Too many type definitions");
	
	/* Ensure that the number of structure elements is not too large: */
	if(elements.size()>1U<<8)
		throw std::runtime_error("DataType::createStructure: Too many structure elements");
	
	/* Ensure that all base types are already defined: */
	for(std::vector<StructureElement>::const_iterator eIt=elements.begin();eIt!=elements.end();++eIt)
		if(eIt->type>=NumAtomicTypes+compoundTypes.size())
			throw std::runtime_error("DataType::createStructure: Undefined element type");
	
	/* Create the new compound type: */
	TypeID result=TypeID(NumAtomicTypes+compoundTypes.size());
	CompoundType newType;
	newType.type=CompoundType::Structure;
	newType.fixedSize=true;
	newType.structure.numElements=elements.size();
	newType.structure.elements=new StructureElement[elements.size()];
	newType.minSize=0;
	newType.alignment=1;
	StructureElement* sePtr=newType.structure.elements;
	for(std::vector<StructureElement>::const_iterator eIt=elements.begin();eIt!=elements.end();++eIt,++sePtr)
		{
		newType.fixedSize=newType.fixedSize&&hasFixedSize(eIt->type);
		*sePtr=*eIt;
		newType.minSize+=getMinSize(eIt->type);
		if(newType.alignment<getAlignment(eIt->type))
			newType.alignment=getAlignment(eIt->type);
		}
	newType.memSize=memSize;
	compoundTypes.push_back(newType);
	
	return result;
	}

DataType::TypeID DataType::createStructure(size_t numElements,const DataType::TypeID elementTypes[])
	{
	/* Ensure that there are not too many data types: */
	if(NumAtomicTypes+compoundTypes.size()>maxTypeId)
		throw std::runtime_error("DataType::createStructure: Too many type definitions");
	
	/* Ensure that there is at least one structure element: */
	if(numElements==0U)
		throw std::runtime_error("DataType::createStructure: Zero-element structures not allowed");
	
	/* Ensure that the number of structure elements is not too large: */
	if(numElements>1U<<8)
		throw std::runtime_error("DataType::createStructure: Too many structure elements");
	
	/* Ensure that all base types are already defined: */
	for(size_t i=0;i<numElements;++i)
		if(elementTypes[i]>=NumAtomicTypes+compoundTypes.size())
			throw std::runtime_error("DataType::createStructure: Undefined element type");
	
	/* Create the new compound type: */
	TypeID result=TypeID(NumAtomicTypes+compoundTypes.size());
	CompoundType newType;
	newType.type=CompoundType::Structure;
	newType.fixedSize=true;
	newType.structure.numElements=numElements;
	newType.structure.elements=new StructureElement[numElements];
	
	/* Initialize the structure's sizes: */
	newType.minSize=0;
	newType.alignment=1;
	newType.memSize=0;
	
	/* Initialize the structure's elements: */
	for(size_t i=0;i<numElements;++i)
		{
		/* Update the structure's fixed size flag: */
		newType.fixedSize=newType.fixedSize&&hasFixedSize(elementTypes[i]);
		
		/* Update the structure's minimum size: */
		newType.minSize+=getMinSize(elementTypes[i]);
		
		/* Update the structure's alignment: */
		size_t elementAlignment=getAlignment(elementTypes[i]);
		if(newType.alignment<elementAlignment)
			newType.alignment=elementAlignment;
			
		/* Align the next element: */
		newType.memSize+=(elementAlignment-newType.memSize)%elementAlignment;
		newType.structure.elements[i]=StructureElement(elementTypes[i],newType.memSize);
		
		/* Update the structure's memory size: */
		newType.memSize+=getMemSize(elementTypes[i]);
		}
	
	/* Pad the structure's memory size to its own alignment: */
	newType.memSize+=(newType.alignment-newType.memSize)%newType.alignment;
	
	compoundTypes.push_back(newType);
	
	return result;
	}

DataType::TypeID DataType::createStructure(size_t numElements)
	{
	/* Ensure that there are not too many data types: */
	if(NumAtomicTypes+compoundTypes.size()>maxTypeId)
		throw std::runtime_error("DataType::createStructure: Too many type definitions");
	
	/* Ensure that there is at least one structure element: */
	if(numElements==0U)
		throw std::runtime_error("DataType::createStructure: Zero-element structures not allowed");
	
	/* Ensure that the number of structure elements is not too large: */
	if(numElements>1U<<8)
		throw std::runtime_error("DataType::createStructure: Too many structure elements");
	
	/* Create the new compound type: */
	TypeID result=TypeID(NumAtomicTypes+compoundTypes.size());
	CompoundType newType;
	newType.type=CompoundType::Structure;
	newType.fixedSize=true;
	newType.structure.numElements=numElements;
	newType.structure.elements=new StructureElement[numElements];
	
	/* Initialize the structure's sizes: */
	newType.minSize=0;
	newType.alignment=1;
	newType.memSize=0;
	
	compoundTypes.push_back(newType);
	
	return result;
	}

void DataType::setStructureElement(DataType::TypeID structureType,size_t elementIndex,DataType::TypeID elementType,ptrdiff_t elementMemOffset)
	{
	/* Ensure that the structure type is defined and is a structure, that the element index is valid, and that the element type is defined: */
	if(structureType>=NumAtomicTypes+compoundTypes.size())
		throw std::runtime_error("DataType::setStructureElement: Undefined structure type");
	CompoundType& ct=compoundTypes[structureType-NumAtomicTypes];
	if(ct.type!=CompoundType::Structure)
		throw std::runtime_error("DataType::setStructureElement: Structure type is not a structure");
	if(elementIndex>=ct.structure.numElements)
		throw std::runtime_error("DataType::setStructureElement: Element index is out of bounds");
	if(elementType>=structureType)
		throw std::runtime_error("DataType::setStructureElement: Undefined element type");
	
	/* Update the structure's fixed size flag: */
	ct.fixedSize=ct.fixedSize&&hasFixedSize(elementType);
	
	/* Set the structure element's type and memory offset: */
	ct.structure.elements[elementIndex].type=elementType;
	ct.structure.elements[elementIndex].memOffset=elementMemOffset;
	
	/* Update the structure's sizes: */
	ct.minSize+=getMinSize(elementType);
	if(ct.alignment<getAlignment(elementType))
		ct.alignment=getAlignment(elementType);
	}

void DataType::setStructureElementType(DataType::TypeID structureType,size_t elementIndex,DataType::TypeID elementType)
	{
	/* Ensure that the structure type is defined and is a structure, that the element index is valid, and that the element type is defined: */
	if(structureType>=NumAtomicTypes+compoundTypes.size())
		throw std::runtime_error("DataType::setStructureElementType: Undefined structure type");
	CompoundType& ct=compoundTypes[structureType-NumAtomicTypes];
	if(ct.type!=CompoundType::Structure)
		throw std::runtime_error("DataType::setStructureElementType: Structure type is not a structure");
	if(elementIndex>=ct.structure.numElements)
		throw std::runtime_error("DataType::setStructureElementType: Element index is out of bounds");
	if(elementType>=structureType)
		throw std::runtime_error("DataType::setStructureElementType: Undefined element type");
	
	/* Update the structure's fixed size flag: */
	ct.fixedSize=ct.fixedSize&&hasFixedSize(elementType);
	
	/* Set the structure element's type: */
	ct.structure.elements[elementIndex].type=elementType;
	
	/* Update the structure's sizes: */
	ct.minSize+=getMinSize(elementType);
	if(ct.alignment<getAlignment(elementType))
		ct.alignment=getAlignment(elementType);
	}

void DataType::setStructureElementMemOffset(DataType::TypeID structureType,size_t elementIndex,ptrdiff_t elementMemOffset)
	{
	/* Ensure that the structure type is defined and is a structure, and that the element index is valid: */
	if(structureType>=NumAtomicTypes+compoundTypes.size())
		throw std::runtime_error("DataType::setStructureElementMemOffset: Undefined structure type");
	CompoundType& ct=compoundTypes[structureType-NumAtomicTypes];
	if(ct.type!=CompoundType::Structure)
		throw std::runtime_error("DataType::setStructureElementMemOffset: Structure type is not a structure");
	if(elementIndex>=ct.structure.numElements)
		throw std::runtime_error("DataType::setStructureElementMemOffset: Element index is out of bounds");
	
	/* Set the structure element's memory offset: */
	ct.structure.elements[elementIndex].memOffset=elementMemOffset;
	}

void DataType::setStructureMemSize(DataType::TypeID structureType,size_t structureMemSize)
	{
	/* Ensure that the structure type is defined and is a structure: */
	if(structureType>=NumAtomicTypes+compoundTypes.size())
		throw std::runtime_error("DataType::setStructureSize: Undefined structure type");
	CompoundType& ct=compoundTypes[structureType-NumAtomicTypes];
	if(ct.type!=CompoundType::Structure)
		throw std::runtime_error("DataType::setStructureSize: Structure type is not a structure");
	
	/* Set the structure's memory size: */
	ct.memSize=structureMemSize;
	}

size_t DataType::calcDataTypeSize(void) const
	{
	/* The number of compound data types is written as a Misc::UInt8: */
	size_t result=sizeof(Misc::UInt8);
	
	for(std::vector<CompoundType>::const_iterator ctIt=compoundTypes.begin();ctIt!=compoundTypes.end();++ctIt)
		{
		/* The compound type type is written as a Misc::UInt8: */
		result+=sizeof(Misc::UInt8);
		
		switch(ctIt->type)
			{
			case CompoundType::Pointer:
				/* The pointer element type is written as a TypeID: */
				result+=sizeof(TypeID);
				
				break;
			
			case CompoundType::FixedArray:
				/* The number of array elements is written as a Misc::UInt16: */
				result+=sizeof(Misc::UInt16);
				
				/* The array element type is written as a TypeID: */
				result+=sizeof(TypeID);
				
				break;
			
			case CompoundType::Vector:
				/* The vector element type is written as a TypeID: */
				result+=sizeof(TypeID);
				
				break;
			
			case CompoundType::Structure:
				/* The number of structure elements is written as a Misc::UInt8: */
				result+=sizeof(Misc::UInt8);
				
				/* Each structure element type is written as a TypeID: */
				result+=ctIt->structure.numElements*sizeof(TypeID);
				
				break;
			
			default:
				/* Can't happen; just to make compiler happy: */
				;
			}
		}
	
	return result;
	}

MessageContinuation* DataType::read(NonBlockSocket& socket,MessageContinuation* continuation)
	{
	/* Embedded classes: */
	class Cont:public MessageContinuation
		{
		/* Embedded classes: */
		public:
		enum State // Enumerated type for reader states
			{
			ReadNumCompoundTypes, // Read number of compound types next
			ReadCompoundTypeType, // Read the type of the next compound type
			ReadPointer, // Read a complete pointer definition
			ReadFixedArray, // Read a complete fixed array definition
			ReadVector, // Read a complete vector definition
			ReadStructureNumElements, // Read the number of elements of a structure definition
			ReadStructureElements // Read a number of structure elements
			};
		
		/* Elements: */
		public:
		State state; // Reader state
		size_t numBytesNeeded; // Number of bytes needed in the socket buffer before being able to continue
		size_t numCompoundTypes; // Number of compound types in data type definition being read
		TypeID currentType; // Type of compound type currently being read
		CompoundType* ct; // Pointer to compound type currently being read
		size_t elementIndex; // Index of the next structure element to read
		
		/* Constructors and destructors: */
		Cont(void) // Creates a new read continuation
			:state(ReadNumCompoundTypes),
			 numBytesNeeded(sizeof(Misc::UInt8)),
			 currentType(NumAtomicTypes-1)
			{
			}
		
		/* Methods: */
		void readCompoundType(void) // Prepares to read another compound type definition
			{
			state=ReadCompoundTypeType;
			numBytesNeeded=sizeof(Misc::UInt8);
			++currentType;
			}
		};
	
	/* Access the continuation object: */
	Cont* cont=static_cast<Cont*>(continuation);
	if(cont==0)
		{
		/* Create a new continuation object: */
		cont=new Cont;
		
		/* Clear the compound type vector: */
		compoundTypes.clear();
		}
	
	/* Keep reading until done or there is not enough unread data to continue: */
	while(socket.getUnread()>=cont->numBytesNeeded)
		{
		switch(cont->state)
			{
			case Cont::ReadNumCompoundTypes:
				
				/* Read the number of compound types in the data type definition: */
				cont->numCompoundTypes=socket.read<Misc::UInt8>();
				
				/* Prepare the compound state vector: */
				compoundTypes.reserve(cont->numCompoundTypes);
				
				/* Read the first compound type: */
				cont->readCompoundType();
				
				break;
			
			case Cont::ReadCompoundTypeType:
				{
				/* Read the compound type type: */
				CompoundType::Type compoundTypeType=CompoundType::Type(socket.read<Misc::UInt8>());
				
				/* Add another compound type: */
				compoundTypes.push_back(CompoundType());
				cont->ct=&compoundTypes.back();
				cont->ct->type=compoundTypeType;
				
				/* Prepare to read the compound type definition: */
				switch(cont->ct->type)
					{
					case CompoundType::Pointer:
						
						/* Read a pointer definition next: */
						cont->state=Cont::ReadPointer;
						cont->numBytesNeeded=sizeof(TypeID);
						
						break;
					
					case CompoundType::FixedArray:
						
						/* Read a fixed array definition next: */
						cont->state=Cont::ReadFixedArray;
						cont->numBytesNeeded=sizeof(Misc::UInt16)+sizeof(TypeID);
						
						break;
					
					case CompoundType::Vector:
						
						/* Read a variable array definition next: */
						cont->state=Cont::ReadVector;
						cont->numBytesNeeded=sizeof(TypeID);
						
						break;
					
					case CompoundType::Structure:
						
						/* Initialize the new structure compound type in case of errors: */
						cont->ct->structure.numElements=0;
						cont->ct->structure.elements=0;
						
						/* Read the number of structure elements next: */
						cont->state=Cont::ReadStructureNumElements;
						cont->numBytesNeeded=sizeof(Misc::UInt8);
						
						break;
					
					default:
						throw std::runtime_error("DataType::read: Invalid compound type type");
					}
				
				break;
				}
			
			case Cont::ReadPointer:
				
				/* Read the pointer's element type: */
				cont->ct->pointer.elementType=socket.read<TypeID>();
				
				/* Pointers never have fixed sizes: */
				cont->ct->fixedSize=false;
				
				/* Calculate the pointer's sizes: */
				cont->ct->minSize=sizeof(WireBool);
				cont->ct->alignment=sizeof(void*);
				cont->ct->memSize=sizeof(void*);
				
				/* Read the next compound type: */
				cont->readCompoundType();
				
				break;
			
			case Cont::ReadFixedArray:
				{
				/* Read the array's number of elements and element type: */
				cont->ct->fixedArray.numElements=size_t(socket.read<Misc::UInt16>())+1U;
				cont->ct->fixedArray.elementType=socket.read<TypeID>();
				if(cont->ct->fixedArray.elementType>=cont->currentType)
					throw std::runtime_error("DataType::read: Undefined array element type");
				
				/* Check for potential overflow in the fixed array's memory size: */
				size_t elementSize=getMemSize(cont->ct->fixedArray.elementType);
				if(cont->ct->fixedArray.numElements>size_t(-1)/elementSize)
					throw std::runtime_error("DataType::read: Memory size overflow in fixed array");
				
				/* Fixed arrays have fixed sizes if their element types have fixed sizes: */
				cont->ct->fixedSize=hasFixedSize(cont->ct->fixedArray.elementType);
				
				/* Calculate the fixed array's sizes: */
				cont->ct->minSize=cont->ct->fixedArray.numElements*getMinSize(cont->ct->fixedArray.elementType);
				cont->ct->alignment=getAlignment(cont->ct->fixedArray.elementType);
				cont->ct->memSize=cont->ct->fixedArray.numElements*elementSize;
				
				/* Read the next compound type: */
				cont->readCompoundType();
				
				break;
				}
			
			case Cont::ReadVector:
				
				/* Read the vector's element type: */
				cont->ct->vector.elementType=socket.read<TypeID>();
				if(cont->ct->vector.elementType>=cont->currentType)
					throw std::runtime_error("DataType::read: Undefined vector element type");
				
				/* Pointers never have fixed sizes: */
				cont->ct->fixedSize=false;
				
				/* Calculate the vector's sizes: */
				cont->ct->minSize=atomicTypeMinSizes[VarInt];
				cont->ct->alignment=sizeof(void*);
				cont->ct->memSize=sizeof(Misc::VectorBase);
				
				/* Read the next compound type: */
				cont->readCompoundType();
				
				break;
			
			case Cont::ReadStructureNumElements:
				
				/* Read the structure's number of elements and allocate the element array: */
				cont->ct->structure.numElements=size_t(socket.read<Misc::UInt8>())+1U;
				cont->ct->structure.elements=new StructureElement[cont->ct->structure.numElements];
				
				/* Structures have fixed sizes if all their element types have fixed sizes: */
				cont->ct->fixedSize=true;
				
				/* Initialize the structure's sizes: */
				cont->ct->minSize=0;
				cont->ct->alignment=1;
				cont->ct->memSize=0;
				
				/* Read the structure's elements next: */
				cont->state=Cont::ReadStructureElements;
				cont->numBytesNeeded=sizeof(TypeID);
				cont->elementIndex=0;
				
				break;
			
			case Cont::ReadStructureElements:
				{
				/* Calculate how many structure elements can be read at once: */
				size_t numElements=Misc::min(socket.getUnread()/sizeof(TypeID),cont->ct->structure.numElements-cont->elementIndex);
				
				/* Read the available elements: */
				for(;numElements>0;--numElements,++cont->elementIndex)
					{
					/* Read and check the type of the next element: */
					StructureElement& se=cont->ct->structure.elements[cont->elementIndex];
					se.type=socket.read<TypeID>();
					if(se.type>=cont->currentType)
						throw std::runtime_error("DataType::read: Undefined structure element type");
					
					/* Update the structure's fixed-sizeness: */
					cont->ct->fixedSize=cont->ct->fixedSize&&hasFixedSize(se.type);
					
					/* Update the structure's minimum size: */
					cont->ct->minSize+=getMinSize(se.type);
					
					/* Update the structure's alignment: */
					size_t elementAlignment=getAlignment(se.type);
					cont->ct->alignment=Misc::max(cont->ct->alignment,elementAlignment);
					
					/* Align the next element while checking for memory size overflow: */
					{
					size_t newMemSize=cont->ct->memSize+(elementAlignment-cont->ct->memSize)%elementAlignment;
					if(newMemSize<cont->ct->memSize)
						throw std::runtime_error("DataType::read: Memory size overflow in structure");
					cont->ct->memSize=newMemSize;
					}
					
					/* Position the next element: */
					se.memOffset=cont->ct->memSize;
					
					/* Update the structure's memory size while checking for overflow: */
					{
					size_t newMemSize=cont->ct->memSize+getMemSize(se.type);
					if(newMemSize<cont->ct->memSize)
						throw std::runtime_error("DataType::read: Memory size overflow in structure");
					cont->ct->memSize=newMemSize;
					}
					}
				
				/* Check if the structure definition has been read completely: */
				if(cont->elementIndex==cont->ct->structure.numElements)
					{
					/* Pad the memory size of the structure to its own alignment while checking for overflow: */
					{
					size_t newMemSize=cont->ct->memSize+(cont->ct->alignment-cont->ct->memSize)%cont->ct->alignment;
					if(newMemSize<cont->ct->memSize)
						throw std::runtime_error("DataType::read: Memory size overflow in structure");
					cont->ct->memSize=newMemSize;
					}
					
					/* Read the next compound type: */
					cont->readCompoundType();
					}
				
				break;
				}
			}
		
		/* Check if the data type definition has been completely read: */
		if(cont->state==Cont::ReadCompoundTypeType&&compoundTypes.size()==cont->numCompoundTypes)
			{
			/* Check if all pointer types have valid element types: */
			TypeID typeId=NumAtomicTypes;
			for(std::vector<CompoundType>::iterator ctIt=compoundTypes.begin();ctIt!=compoundTypes.end();++ctIt,++typeId)
				if(ctIt->type==CompoundType::Pointer)
					if(ctIt->type==typeId||ctIt->type>=NumAtomicTypes+compoundTypes.size())
						throw std::runtime_error("DataType::read: Invalid pointer element type");
			
			/* Delete the continuation object and bail out: */
			delete cont;
			cont=0;
			break;
			}
		}
	
	return cont;
	}

std::vector<DataType::StructureElement> DataType::getStructureElements(DataType::TypeID type) const
	{
	/* Access the type's structure definition: */
	const CompoundType& ct=compoundTypes[type-NumAtomicTypes];
	
	/* Copy the structure definition's elements into a vector: */
	std::vector<StructureElement> result;
	result.reserve(ct.structure.numElements);
	for(size_t i=0;i<ct.structure.numElements;++i)
		result.push_back(ct.structure.elements[i]);
	
	return result;
	}

void* DataType::createObject(DataType::TypeID type) const
	{
	/* Allocate a memory block for the overall object: */
	void* object=malloc(getMemSize(type));
	
	/* Initialize the new object: */
	initObject(type,object);
	
	return object;
	}

void DataType::destroyObject(DataType::TypeID type,void* object) const
	{
	/* De-initialize the object: */
	deinitObject(type,object);
	
	/* Delete the object's memory block: */
	free(object);
	}

void DataType::print(std::ostream& os,DataType::TypeID type,const void* object) const
	{
	/* Check if the type is atomic: */
	if(type<NumAtomicTypes)
		{
		/* Print the object's value: */
		switch(type)
			{
			case Bool:
				os<<(*static_cast<const bool*>(object)?"true":"false");
				break;
			
			case Char:
				os<<'\''<<*static_cast<const char*>(object)<<'\'';
				break;
			
			case SInt8:
				os<<int(*static_cast<const Misc::SInt8*>(object));
				break;
			
			case SInt16:
				os<<*static_cast<const Misc::SInt16*>(object);
				break;
			
			case SInt32:
				os<<*static_cast<const Misc::SInt32*>(object);
				break;
			
			case SInt64:
				os<<*static_cast<const Misc::SInt64*>(object);
				break;
			
			case UInt8:
				os<<(unsigned int)(*static_cast<const Misc::UInt8*>(object));
				break;
			
			case UInt16:
				os<<*static_cast<const Misc::UInt16*>(object);
				break;
			
			case UInt32:
				os<<*static_cast<const Misc::UInt32*>(object);
				break;
			
			case UInt64:
				os<<*static_cast<const Misc::UInt64*>(object);
				break;
			
			case Float32:
				os<<*static_cast<const Misc::Float32*>(object);
				break;
			
			case Float64:
				os<<*static_cast<const Misc::Float64*>(object);
				break;
			
			case VarInt:
				os<<*static_cast<const Misc::UInt32*>(object);
				break;
			
			case String:
				os<<'"'<<*static_cast<const std::string*>(object)<<'"';
				break;
			
			default:
				/* Can't happen; just to make compiler happy: */
				;
			}
		}
	else
		{
		/* Access the compound type: */
		const CompoundType& ct=compoundTypes[type-NumAtomicTypes];
		switch(ct.type)
			{
			case CompoundType::Pointer:
				{
				os<<'(';
				
				/* Print the pointed-to element if the pointer is valid: */
				const void* target=*static_cast<const void* const*>(object);
				if(target!=0)
					print(os,ct.pointer.elementType,target);
				
				os<<')';
				
				break;
				}
			
			case CompoundType::FixedArray:
				{
				os<<'[';
				
				/* Print all array elements: */
				size_t elementSize=getMemSize(ct.fixedArray.elementType);
				const char* elementPtr=static_cast<const char*>(object);
				const char* elementEnd=elementPtr+ct.fixedArray.numElements*elementSize;
				if(elementPtr!=elementEnd)
					{
					/* Print the first element: */
					print(os,ct.fixedArray.elementType,elementPtr);
					
					/* Print the remaining elements: */
					for(elementPtr+=elementSize;elementPtr!=elementEnd;elementPtr+=elementSize)
						{
						os<<", ";
						print(os,ct.fixedArray.elementType,elementPtr);
						}
					}
				
				os<<']';
				
				break;
				}
			
			case CompoundType::Vector:
				{
				os<<'[';
				
				/* Print all vector elements: */
				const Misc::VectorBase& vec=*static_cast<const Misc::VectorBase*>(object);
				size_t elementSize=getMemSize(ct.vector.elementType);
				const char* elementPtr=static_cast<const char*>(vec.getElements());
				const char* elementEnd=elementPtr+vec.size()*elementSize;
				if(elementPtr!=elementEnd)
					{
					/* Print the first element: */
					print(os,ct.vector.elementType,elementPtr);
					
					/* Print the remaining elements: */
					for(elementPtr+=elementSize;elementPtr!=elementEnd;elementPtr+=elementSize)
						{
						os<<", ";
						print(os,ct.vector.elementType,elementPtr);
						}
					}
				
				os<<']';
				
				break;
				}
			
			case CompoundType::Structure:
				{
				os<<'{';
				
				/* Print all structure elements: */
				const char* elementPtr=static_cast<const char*>(object);
				const StructureElement* sePtr=ct.structure.elements;
				const StructureElement* seEnd=sePtr+ct.structure.numElements;
				if(sePtr!=seEnd)
					{
					/* Print the first element: */
					print(os,sePtr->type,elementPtr+sePtr->memOffset);
					
					/* Print the remaining elements: */
					for(++sePtr;sePtr!=seEnd;++sePtr)
						{
						os<<", ";
						print(os,sePtr->type,elementPtr+sePtr->memOffset);
						}
					}
				
				os<<'}';
				
				break;
				}
			
			default:
				/* Can't happen, just to make compiler happy: */
				;
			}
		}
	}

size_t DataType::calcSize(DataType::TypeID type,const void* object) const
	{
	/* Check if the type is atomic: */
	if(type<NumAtomicTypes)
		{
		if(type<VarInt)
			{
			/* Return the atomic type's fixed size: */
			return atomicTypeMinSizes[type];
			}
		else if(type==VarInt)
			{
			/* Return the wire size of the VarInt: */
			return Misc::getVarInt32Size(*static_cast<const Misc::UInt32*>(object));
			}
		else
			{
			/* Calculate the wire size of the string, which is a VarInt length tag followed by the string's characters: */
			Misc::UInt32 strLen(static_cast<const std::string*>(object)->length());
			return Misc::getVarInt32Size(strLen)+strLen*sizeof(WireChar);
			}
		}
	else
		{
		/* Access the compound type: */
		const CompoundType& ct=compoundTypes[type-NumAtomicTypes];
		
		/* Check if the compound type has a fixed size: */
		if(ct.fixedSize)
			{
			/* Return the compound type's fixed size: */
			return ct.minSize;
			}
		else
			{
			/* Calculate the compound object's size via recursion: */
			switch(ct.type)
				{
				case CompoundType::Pointer:
					{
					/* Calculate the wire size of the pointer by adding up the wire size of its valid flag and that of its target object: */
					size_t result=sizeof(WireBool);
					const void* target=*static_cast<const void* const*>(object);
					if(target!=0)
						result+=calcSize(ct.pointer.elementType,target);
					return result;
					}
					
				case CompoundType::FixedArray:
					{
					/* Calculate the wire size of the fixed array by adding up the wire sizes of its elements: */
					size_t result=0;
					size_t elementSize=getMemSize(ct.fixedArray.elementType);
					const char* elementEnd=static_cast<const char*>(object)+ct.fixedArray.numElements*elementSize;
					for(const char* elementPtr=static_cast<const char*>(object);elementPtr!=elementEnd;elementPtr+=elementSize)
						result+=calcSize(ct.fixedArray.elementType,elementPtr);
					return result;
					}
				
				case CompoundType::Vector:
					{
					/* Calculate the wire size of the vector by adding up the wire size of its size field and its elements: */
					const Misc::VectorBase& vec=*static_cast<const Misc::VectorBase*>(object);
					Misc::UInt32 vecLen=vec.size();
					size_t result=Misc::getVarInt32Size(vecLen);
					size_t elementSize=getMemSize(ct.vector.elementType);
					const char* elementEnd=static_cast<const char*>(vec.getElements())+vecLen*elementSize;
					for(const char* elementPtr=static_cast<const char*>(vec.getElements());elementPtr!=elementEnd;elementPtr+=elementSize)
						result+=calcSize(ct.vector.elementType,elementPtr);
					return result;
					}
				
				case CompoundType::Structure:
					{
					/* Calculate the wire size of the structure by adding up the wire sizes of its elements: */
					size_t result=0;
					const char* elementPtr=static_cast<const char*>(object);
					const StructureElement* seEnd=ct.structure.elements+ct.structure.numElements;
					for(const StructureElement* sePtr=ct.structure.elements;sePtr!=seEnd;++sePtr)
						result+=calcSize(sePtr->type,elementPtr+sePtr->memOffset);
					return result;
					}
				
				default:
					/* Can't happen, just to make compiler happy: */
					return 0;
				}
			}
		}
	}

MessageContinuation* DataType::prepareReading(DataType::TypeID type,void* object) const
	{
	/* Ensure that the given type is defined: */
	if(type>=NumAtomicTypes+compoundTypes.size())
		throw std::runtime_error("DataType::prepareReading: Undefined type");
	
	/* Create a new continuation object: */
	ReadObjectContinuation* cont=new ReadObjectContinuation(*this);
	
	/* Start reading the object: */
	cont->startSubObject(type,object);
	
	return cont;
	}

MessageContinuation* DataType::continueReading(NonBlockSocket& socket,MessageContinuation* continuation) const
	{
	/* Access the continuation object: */
	ReadObjectContinuation* cont=static_cast<ReadObjectContinuation*>(continuation);
	
	/* Keep reading until done or there is not enough unread data to continue: */
	while(cont->incomplete()&&socket.getUnread()>=cont->numBytesNeeded)
		{
		/* Act depending on the read state of the current sub-object: */
		switch(cont->top->state)
			{
			case ReadObjectContinuation::ReadFixedAtomic:
				
				/* Read a fixed-size atomic value based on its type: */
				switch(cont->top->type)
					{
					case Bool:
						*static_cast<bool*>(cont->top->object)=socket.read<WireBool>()!=WireBool(0);
						break;
					
					case Char:
						*static_cast<char*>(cont->top->object)=char(socket.read<WireChar>());
						break;
					
					case SInt8:
						socket.read(*static_cast<Misc::SInt8*>(cont->top->object));
						break;
					
					case SInt16:
						socket.read(*static_cast<Misc::SInt16*>(cont->top->object));
						break;
					
					case SInt32:
						socket.read(*static_cast<Misc::SInt32*>(cont->top->object));
						break;
					
					case SInt64:
						socket.read(*static_cast<Misc::SInt64*>(cont->top->object));
						break;
					
					case UInt8:
						socket.read(*static_cast<Misc::UInt8*>(cont->top->object));
						break;
					
					case UInt16:
						socket.read(*static_cast<Misc::UInt16*>(cont->top->object));
						break;
					
					case UInt32:
						socket.read(*static_cast<Misc::UInt32*>(cont->top->object));
						break;
					
					case UInt64:
						socket.read(*static_cast<Misc::UInt64*>(cont->top->object));
						break;
					
					case Float32:
						socket.read(*static_cast<Misc::Float32*>(cont->top->object));
						break;
					
					case Float64:
						socket.read(*static_cast<Misc::Float64*>(cont->top->object));
						break;
					}
				
				/* Finish the sub-object: */
				cont->finishSubObject();
				
				break;
			
			case ReadObjectContinuation::ReadVarIntFirst:
				
				/* Read the first byte of a VarInt and determine the number of bytes left to read: */
				cont->top->varInt.remaining=Misc::readVarInt32First(socket,cont->top->varInt.value);
				
				/* Check if there are additional bytes to read: */
				if(cont->top->varInt.remaining>0)
					{
					cont->top->state=ReadObjectContinuation::ReadVarInt;
					cont->numBytesNeeded=cont->top->varInt.remaining;
					}
				else
					{
					/* Store the one-byte value: */
					*static_cast<Misc::UInt32*>(cont->top->object)=cont->top->varInt.value;
					
					/* Finish the sub-object: */
					cont->finishSubObject();
					}
				
				break;
			
			case ReadObjectContinuation::ReadVarInt:
				
				/* Read the VarInt's remaining bytes: */
				Misc::readVarInt32Remaining(socket,cont->top->varInt.remaining,cont->top->varInt.value);
				
				/* Store the value: */
				*static_cast<Misc::UInt32*>(cont->top->object)=cont->top->varInt.value;
				
				/* Finish the sub-object: */
				cont->finishSubObject();
				
				break;
			
			case ReadObjectContinuation::ReadString:
				{
				/* Calculate the number of string characters that can be read at once: */
				Misc::UInt32 numCharacters=Misc::min(Misc::UInt32(socket.getUnread()),cont->top->string.remaining);
				cont->top->string.remaining-=numCharacters;
				
				/* Read characters: */
				std::string& string=*static_cast<std::string*>(cont->top->object);
				for(;numCharacters>0;--numCharacters)
					string.push_back(socket.read<WireChar>());
				
				/* Check if the string has been completely read: */
				if(cont->top->string.remaining==0)
					{
					/* Finish the sub-object: */
					cont->finishSubObject();
					}
				
				break;
				}
			
			case ReadObjectContinuation::ReadPointerValid:
				{
				/* Access the pointed-to object: */
				void* target=*static_cast<void**>(cont->top->object);
				TypeID elementType=compoundTypes[cont->top->type-NumAtomicTypes].pointer.elementType;
				
				/* Check if the pointer is valid: */
				if(socket.read<WireBool>()!=WireBool(0))
					{
					/* Create the target object if it does not exist: */
					if(target==0)
						*static_cast<void**>(cont->top->object)=target=createObject(elementType);
					
					/* Start reading into the target object: */
					cont->top->state=ReadObjectContinuation::ReadPointer;
					cont->startSubObject(elementType,target);
					}
				else
					{
					/* Delete the target object if it exists: */
					if(target!=0)
						{
						destroyObject(elementType,target);
						*static_cast<void**>(cont->top->object)=0;
						}
					
					/* Finish the sub-object: */
					cont->finishSubObject();
					}
				}
			
			default:
				; // Never reached; just to make compiler happy
			}
		}
	
	/* Check if the object has been completely read: */
	if(!cont->incomplete())
		{
		/* We're done here: */
		delete cont;
		cont=0;
		}
	
	return cont;
	}

void DataType::swapEndianness(DataType::TypeID type,MessageEditor& editor) const
	{
	static const char* errorMsg="DataType::swapEndianness: Buffer overflow";
	
	/* Check if the type is atomic: */
	if(type<NumAtomicTypes)
		{
		/* Byte-swap the atomic type: */
		void* editPtr=editor.getEditPtr();
		size_t editSize=atomicTypeMinSizes[type];
		if(editor.getUnedited()<editSize)
			throw std::runtime_error(errorMsg);
		switch(type)
			{
			case SInt16:
				Misc::swapEndianness(*static_cast<Misc::SInt16*>(editPtr));
				break;
			
			case SInt32:
				Misc::swapEndianness(*static_cast<Misc::SInt32*>(editPtr));
				break;
			
			case SInt64:
				Misc::swapEndianness(*static_cast<Misc::SInt64*>(editPtr));
				break;
			
			case UInt16:
				Misc::swapEndianness(*static_cast<Misc::UInt16*>(editPtr));
				break;
			
			case UInt32:
				Misc::swapEndianness(*static_cast<Misc::UInt32*>(editPtr));
				break;
			
			case UInt64:
				Misc::swapEndianness(*static_cast<Misc::UInt64*>(editPtr));
				break;
			
			case Float32:
				Misc::swapEndianness(*static_cast<Misc::Float32*>(editPtr));
				break;
			
			case Float64:
				Misc::swapEndianness(*static_cast<Misc::Float64*>(editPtr));
				break;
			
			case VarInt:
				{
				/* Read the VarInt's first byte to determine how many bytes to skip: */
				Misc::UInt32 value;
				editSize=Misc::readVarInt32First(editor,value);
				if(editor.getUnedited()<editSize)
					throw std::runtime_error(errorMsg);
				
				/* VarInts don't need to be endianness-swapped */
				
				break;
				}
			
			case String:
				{
				/* Read the string length's first byte to determine its size: */
				Misc::UInt32 strLen;
				size_t remaining=Misc::readVarInt32First(editor,strLen);
				if(editor.getUnedited()<remaining)
					throw std::runtime_error(errorMsg);
				
				/* Read the rest of the string length: */
				Misc::readVarInt32Remaining(editor,remaining,strLen);
				
				/* Strings don't need to be endianness-swapped */
				editSize=strLen*sizeof(WireChar);
				if(editor.getUnedited()<editSize)
					throw std::runtime_error(errorMsg);
				
				break;
				}
			
			default:
				/* No need to byte-swap these types */
				;
			}
		
		/* Advance the editing position: */
		editor.advanceEditPtr(editSize);
		}
	else
		{
		/* Access the compound type: */
		const CompoundType& ct=compoundTypes[type-NumAtomicTypes];
		
		switch(ct.type)
			{
			case CompoundType::Pointer:
				
				/* Check if the pointer points to an object: */
				if(editor.getUnedited()<sizeof(WireBool))
					throw std::runtime_error(errorMsg);
				if(editor.read<WireBool>()!=WireBool(0))
					{
					/* Endianness-swap the pointed-to object: */
					swapEndianness(ct.pointer.elementType,editor);
					}
				
				break;
			
			case CompoundType::FixedArray:
				{
				/* Endianness-swap the array's elements: */
				for(size_t i=0;i<ct.fixedArray.numElements;++i)
					swapEndianness(ct.fixedArray.elementType,editor);
				
				break;
				}
			
			case CompoundType::Vector:
				{
				/* Read the vector length's first byte to determine its size: */
				Misc::UInt32 vecLen;
				size_t remaining=Misc::readVarInt32First(editor,vecLen);
				if(editor.getUnedited()<remaining)
					throw std::runtime_error(errorMsg);
				
				/* Read the rest of the vector length: */
				Misc::readVarInt32Remaining(editor,remaining,vecLen);
				
				/* Endianness-swap the vector's elements: */
				for(Misc::UInt32 i=0;i<vecLen;++i)
					swapEndianness(ct.vector.elementType,editor);
				
				break;
				}
			
			case CompoundType::Structure:
				{
				/* Endianness-swap the structure's elements: */
				for(size_t i=0;i<ct.structure.numElements;++i)
					swapEndianness(ct.structure.elements[i].type,editor);
				
				break;
				}
			
			default:
				/* Can't happen; just to make compiler happy: */
				;
			}
		}
	}

void DataType::checkSerialization(DataType::TypeID type,MessageEditor& editor) const
	{
	static const char* errorMsg="DataType::checkSerialization: Buffer overflow";
	
	/* Check if the type is atomic: */
	if(type<NumAtomicTypes)
		{
		/* Check the atomic type: */
		size_t readSize=atomicTypeMinSizes[type];
		if(editor.getUnedited()<readSize)
			throw std::runtime_error(errorMsg);
		switch(type)
			{
			case VarInt:
				{
				/* Read the VarInt's first byte to determine how many bytes to skip: */
				Misc::UInt32 value;
				readSize=Misc::readVarInt32First(editor,value);
				if(editor.getUnedited()<readSize)
					throw std::runtime_error(errorMsg);
				
				/* VarInts don't need to be checked */
				
				break;
				}
			
			case String:
				{
				/* Read the string length's first byte to determine its size: */
				Misc::UInt32 strLen;
				size_t remaining=Misc::readVarInt32First(editor,strLen);
				if(editor.getUnedited()<remaining)
					throw std::runtime_error(errorMsg);
				
				/* Read the rest of the string length: */
				Misc::readVarInt32Remaining(editor,remaining,strLen);
				
				/* Strings don't need to be checked */
				readSize=strLen*sizeof(WireChar);
				if(editor.getUnedited()<readSize)
					throw std::runtime_error(errorMsg);
				
				break;
				}
			
			default:
				/* No need to check these types */
				;
			}
		
		/* Advance the reading position: */
		editor.advanceEditPtr(readSize);
		}
	else
		{
		/* Access the compound type: */
		const CompoundType& ct=compoundTypes[type-NumAtomicTypes];
		
		switch(ct.type)
			{
			case CompoundType::Pointer:
				
				/* Check if the pointer points to an object: */
				if(editor.getUnedited()<sizeof(WireBool))
					throw std::runtime_error(errorMsg);
				if(editor.read<WireBool>()!=WireBool(0))
					{
					/* Check the pointed-to object: */
					checkSerialization(ct.pointer.elementType,editor);
					}
				
				break;
			
			case CompoundType::FixedArray:
				{
				/* Check the array's elements: */
				for(size_t i=0;i<ct.fixedArray.numElements;++i)
					checkSerialization(ct.fixedArray.elementType,editor);
				
				break;
				}
			
			case CompoundType::Vector:
				{
				/* Read the vector length's first byte to determine its size: */
				Misc::UInt32 vecLen;
				if(editor.getUnedited()<sizeof(Misc::UInt8))
					throw std::runtime_error(errorMsg);
				size_t remaining=Misc::readVarInt32First(editor,vecLen);
				if(editor.getUnedited()<remaining)
					throw std::runtime_error(errorMsg);
				
				/* Read the rest of the vector length: */
				Misc::readVarInt32Remaining(editor,remaining,vecLen);
				
				/* Check the vector's elements: */
				for(Misc::UInt32 i=0;i<vecLen;++i)
					checkSerialization(ct.vector.elementType,editor);
				
				break;
				}
			
			case CompoundType::Structure:
				{
				/* Check the structure's elements: */
				for(size_t i=0;i<ct.structure.numElements;++i)
					checkSerialization(ct.structure.elements[i].type,editor);
				
				break;
				}
			
			default:
				/* Can't happen; just to make compiler happy: */
				;
			}
		}
	}

void DataType::printSerialization(std::ostream& os,DataType::TypeID type,MessageReader& reader) const
	{
	/* Check if the type is atomic: */
	if(type<NumAtomicTypes)
		{
		/* Print the atomic type: */
		switch(type)
			{
			case Bool:
				os<<(reader.read<WireBool>()!=WireBool(0)?"true":"false");
				break;
			
			case Char:
				os<<'\''<<reader.read<WireChar>()<<'\'';
				
			case SInt8:
				os<<int(reader.read<Misc::SInt8>());
				break;
				
			case SInt16:
				os<<reader.read<Misc::SInt16>();
				break;
			
			case SInt32:
				os<<reader.read<Misc::SInt32>();
				break;
			
			case SInt64:
				os<<reader.read<Misc::SInt64>();
				break;
			
			case UInt8:
				os<<(unsigned int)(reader.read<Misc::UInt8>());
				break;
			
			case UInt16:
				os<<reader.read<Misc::UInt16>();
				break;
			
			case UInt32:
				os<<reader.read<Misc::UInt32>();
				break;
			
			case UInt64:
				os<<reader.read<Misc::UInt64>();
				break;
			
			case Float32:
				os<<reader.read<Misc::Float32>();
				break;
			
			case Float64:
				os<<reader.read<Misc::Float64>();
				break;
			
			case VarInt:
				os<<Misc::readVarInt32(reader);
				break;
			
			case String:
				{
				/* Read the string's length: */
				Misc::UInt32 strLen=Misc::readVarInt32(reader);
				
				/* Print the string's characters: */
				os<<'"';
				for(Misc::UInt32 i=0;i<strLen;++i)
					os<<reader.read<WireChar>();
				os<<'"';
				
				break;
				}
			
			default:
				/* Can't happen; just to make compiler happy: */
				;
			}
		}
	else
		{
		/* Access the compound type: */
		const CompoundType& ct=compoundTypes[type-NumAtomicTypes];
		
		switch(ct.type)
			{
			case CompoundType::Pointer:
				
				os<<'(';
				
				/* Check if the pointer points to an object: */
				if(reader.read<WireBool>()!=WireBool(0))
					{
					/* Print the pointed-to object: */
					printSerialization(os,ct.pointer.elementType,reader);
					}
				
				os<<')';
				
				break;
			
			case CompoundType::FixedArray:
				{
				/* Print the array's elements: */
				os<<'[';
				printSerialization(os,ct.fixedArray.elementType,reader);
				for(size_t i=1;i<ct.fixedArray.numElements;++i)
					{
					os<<", ";
					printSerialization(os,ct.fixedArray.elementType,reader);
					}
				os<<']';
				
				break;
				}
			
			case CompoundType::Vector:
				{
				/* Read the vector's length: */
				Misc::UInt32 vecLen=Misc::readVarInt32(reader);
				
				/* Print the vector's elements: */
				os<<'[';
				if(vecLen>0U)
					{
					printSerialization(os,ct.vector.elementType,reader);
					for(Misc::UInt32 i=1;i<vecLen;++i)
						{
						os<<", ";
						printSerialization(os,ct.vector.elementType,reader);
						}
					}
				os<<']';
				
				break;
				}
			
			case CompoundType::Structure:
				{
				/* Print the structure's elements: */
				os<<'{';
				printSerialization(os,ct.structure.elements[0].type,reader);
				for(size_t i=1;i<ct.structure.numElements;++i)
					{
					os<<", ";
					printSerialization(os,ct.structure.elements[i].type,reader);
					}
				os<<'}';
				
				break;
				}
			
			default:
				/* Can't happen; just to make compiler happy: */
				;
			}
		}
	}

/************************************************
Force instantiation of standard template methods:
************************************************/

template void DataType::read<MessageReader>(MessageReader& source);
template void DataType::write<MessageWriter>(MessageWriter& sink) const;
template size_t DataType::read<MessageReader>(MessageReader& source,DataType::TypeID type,void* object) const;
template size_t DataType::write<MessageWriter>(DataType::TypeID type,const void* object,MessageWriter& sink) const;
