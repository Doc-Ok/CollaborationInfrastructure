/***********************************************************************
DataType - Class to define data types and data objects for automatic
transmission over binary pipes.
Copyright (c) 2019-2020 Oliver Kreylos
***********************************************************************/

#ifndef DATATYPE_INCLUDED
#define DATATYPE_INCLUDED

#include <stddef.h>
#include <vector>
#include <iostream>
#include <Misc/SizedTypes.h>

/* Forward declarations: */
class MessageReader;
class MessageEditor;
class NonBlockSocket;
class MessageContinuation;

class DataType
	{
	/* Embedded classes: */
	public:
	typedef Misc::UInt8 TypeID; // Type for IDs for pre-defined or user-defined types
	static const TypeID maxTypeId=TypeID(255U); // Maximum allowed type ID
	
	enum AtomicTypes // Enumerated type for pre-defined atomic types
		{
		Bool, // Boolean value with opaque representation
		Char, // Character value with opaque representation
		SInt8,SInt16,SInt32,SInt64, // Signed integer types of various sizes
		UInt8,UInt16,UInt32,UInt64,  // Unsigned integer types of various sizes
		Float32,Float64, // Floating-point types of various sizes
		VarInt, // A 32-bit unsigned integer represented as 1-5 bytes depending on value; memory representation is Misc::UInt32
		String, // C++ std::string of arbitrary length (up to 2^32-1 characters, to be precise)
		
		NumAtomicTypes
		};
	
	typedef Misc::UInt8 WireBool; // Wire representation for boolean values
	typedef Misc::UInt8 WireChar; // Wire representation for characters
	
	struct StructureElement // Structure representing a structure element
		{
		/* Elements: */
		public:
		TypeID type; // Type of structure element
		ptrdiff_t memOffset; // Offset of structure element from beginning of memory representation of structure in bytes
		
		/* Constructors and destructors: */
		StructureElement(void)
			:memOffset(0)
			{
			}
		StructureElement(TypeID sType,ptrdiff_t sMemOffset)
			:type(sType),memOffset(sMemOffset)
			{
			}
		};
	
	private:
	struct CompoundType // Structure representing a compound type
		{
		/* Embedded classes: */
		public:
		enum Type // Enumerated type for types of compound types
			{
			Pointer, // A pointer to an object
			FixedArray, // Array with fixed size
			Vector, // A Misc::Vector of arbitrary size
			Structure // Structure comprising an ordered set of other types
			};
		
		/* Elements: */
		public:
		Type type; // Type of this compound type
		bool fixedSize; // Flag whether the wire representation of this compound type has a fixed size
		union
			{
			struct
				{
				TypeID elementType; // Type of pointed-to element
				} pointer;
			struct
				{
				size_t numElements; // Number of elements in the fixed array
				TypeID elementType; // Type of array elements
				} fixedArray;
			struct
				{
				TypeID elementType; // Type of vector elements
				} vector;
			struct
				{
				size_t numElements; // Number of elements in the structure
				StructureElement* elements; // Array of structure element descriptors
				} structure;
			};
		size_t minSize; // Minimum size of the wire representation of this compound type
		size_t alignment; // Alignment granularity of memory representation of this compound type
		size_t memSize; // Memory size of this compound type
		};
	
	class ReadObjectContinuation;
	
	friend class ReadObjectContinuation;
	
	/* Elements: */
	private:
	static const size_t atomicTypeMinSizes[NumAtomicTypes]; // Array of minimum sizes of wire representations of atomic types in bytes
	static const size_t atomicTypeAlignments[NumAtomicTypes]; // Array of alignment granularities of memory representations of atomic types in bytes
	static const size_t atomicTypeMemSizes[NumAtomicTypes]; // Array of sizes of memory representations of atomic types in bytes
	std::vector<CompoundType> compoundTypes; // List of defined compound data types, with the first item having type ID NumAtomicTypes
	
	/* Private methods: */
	void initObject(TypeID type,void* object) const; // Initializes an object created by createObject()
	void deinitObject(TypeID type,void* object) const; // De-initializes an object created by createObject() before it is destroyed
	
	/* Constructors and destructors: */
	public:
	DataType(void) // Creates an "empty" data type definition with no user-defined types
		{
		}
	DataType(const DataType& source) // Copy constructor
		{
		*this=source;
		}
	DataType& operator=(const DataType& source); // Prohibit assignment operator
	template <class SourceParam>
	DataType(SourceParam& source) // Reads a data type definition from a binary source
		{
		read(source);
		}
	~DataType(void); // Destroys the data type definition
	
	/* Methods: */
	bool operator==(const DataType& other) const; // Returns true if two data types are equivalent
	
	/* Methods to define compound data types: */
	TypeID createPointer(void); // Defines a pointer to an unknown element type as a new data type; element type must be set before type can be used
	void setPointerElementType(TypeID pointerType,TypeID elementType); // Sets the element type of an existing undefined pointer type
	TypeID createPointer(TypeID elementType); // Defines a pointer to a known element type as a new data type
	TypeID createFixedArray(size_t numElements,TypeID elementType); // Defines a fixed-size array as a new data type
	TypeID createVector(TypeID elementType); // Defines a Misc::Vector as a new data type
	TypeID createStructure(size_t numElements,const StructureElement elements[],size_t memSize); // Defines a structure as a new data type
	TypeID createStructure(const std::vector<StructureElement> elements,size_t memSize); // Ditto
	TypeID createStructure(size_t numElements,const TypeID elementTypes[]); // Defines a structure as a new data type without assigning element memory offsets or a total memory size
	TypeID createStructure(size_t numElements); // Defines a structure as a new data type without assigning element types
	void setStructureElement(TypeID structureType,size_t elementIndex,TypeID elementType,ptrdiff_t elementMemOffset); // Sets an element type and offset of a previously-created structure
	void setStructureElementType(TypeID structureType,size_t elementIndex,TypeID elementType); // Sets an element type of a previously-created structure
	void setStructureElementMemOffset(TypeID structureType,size_t elementIndex,ptrdiff_t elementMemOffset); // Sets an element offset of a previously-created structure
	void setStructureMemSize(TypeID structureType,size_t structureMemSize); // Sets the total memory size of a previously-created structure, including padding, in bytes
	
	/* Methods to read/write data type definitions from/to binary sources/sinks: */
	static size_t getMinDataTypeSize(void) // Returns the minimum wire size of a data type definition
		{
		return sizeof(TypeID);
		}
	size_t calcDataTypeSize(void) const; // Calculates the wire size of the data type definition itself
	MessageContinuation* read(NonBlockSocket& socket,MessageContinuation* continuation); // Reads a data type definition from the given non-blocking socket; returns null if definition has been completely read
	template <class SourceParam>
	void read(SourceParam& source); // Reads a data type definition from a binary source
	template <class SinkParam>
	void write(SinkParam& sink) const; // Writes the data type definition to a binary sink
	
	/* Methods to query data types: */
	template <class TypeParam>
	static TypeID getAtomicType(void); // Returns the type identifier for an atomic C++ data type including std::string
	bool isDefined(TypeID type) const // Returns true if the given data type is defined
		{
		return type<NumAtomicTypes+compoundTypes.size();
		}
	bool isAtomic(TypeID type) const // Returns true if the given data type is atomic
		{
		return type<NumAtomicTypes;
		}
	bool isPointer(TypeID type) const // Returns true if the given data type is defined as a pointer
		{
		return type>=NumAtomicTypes&&size_t(type-NumAtomicTypes)<compoundTypes.size()&&compoundTypes[type-NumAtomicTypes].type==CompoundType::Pointer;
		}
	bool isPointerElementTypeKnown(TypeID type) const // Returns true if the element type of the given data type, assumed to be a pointer, has already been defined
		{
		return compoundTypes[type-NumAtomicTypes].pointer.elementType!=type;
		}
	TypeID getPointerElementType(TypeID type) const // Returns the element type of the given data type; assumes that given type is a pointer
		{
		return compoundTypes[type-NumAtomicTypes].pointer.elementType;
		}
	bool isFixedArray(TypeID type) const // Returns true if the given data type is defined as an array with an a-priori known size
		{
		return type>=NumAtomicTypes&&size_t(type-NumAtomicTypes)<compoundTypes.size()&&compoundTypes[type-NumAtomicTypes].type==CompoundType::FixedArray;
		}
	size_t getFixedArrayNumElements(TypeID type) const // Returns the fixed array size of the given data type; assumes that given type is a fixed array
		{
		return compoundTypes[type-NumAtomicTypes].fixedArray.numElements;
		}
	TypeID getFixedArrayElementType(TypeID type) const // Returns the element type of the given data type; assumes that given type is a fixed array
		{
		return compoundTypes[type-NumAtomicTypes].fixedArray.elementType;
		}
	bool isVector(TypeID type) const // Returns true if the given data type is defined as a Misc::Vector
		{
		return type>=NumAtomicTypes&&size_t(type-NumAtomicTypes)<compoundTypes.size()&&compoundTypes[type-NumAtomicTypes].type==CompoundType::Vector;
		}
	TypeID getVectorElementType(TypeID type) const // Returns the element type of the given data type; assumes that given type is a Misc::Vector
		{
		return compoundTypes[type-NumAtomicTypes].vector.elementType;
		}
	bool isStructure(TypeID type) const // Returns true if the given data type is defined as a structure
		{
		return type>=NumAtomicTypes&&size_t(type-NumAtomicTypes)<compoundTypes.size()&&compoundTypes[type-NumAtomicTypes].type==CompoundType::Structure;
		}
	size_t getStructureNumElements(TypeID type) const // Returns the number of structure elements of the given data type; assumes that given type is a structure
		{
		return compoundTypes[type-NumAtomicTypes].structure.numElements;
		}
	TypeID getStructureElementType(TypeID type,size_t elementIndex) const // Returns the element type of the given element of the given data type; assumes that given type is a structure and that structureIndex is smaller than the structure's number of elements
		{
		return compoundTypes[type-NumAtomicTypes].structure.elements[elementIndex].type;
		}
	static ptrdiff_t getOffset(const void* structure,const void* element) // Returns the offset of the given structure element inside the given structure
		{
		return static_cast<const char*>(element)-static_cast<const char*>(structure);
		}
	size_t getStructureElementMemOffset(TypeID type,size_t elementIndex) const // Returns the memory offset of the given element of the given data type; assumes that given type is a structure and that structureIndex is smaller than the structure's number of elements
		{
		return compoundTypes[type-NumAtomicTypes].structure.elements[elementIndex].memOffset;
		}
	std::vector<StructureElement> getStructureElements(TypeID type) const; // Returns a vector of the elements defining the given data type; assumes that given type is a structure
	bool hasFixedSize(TypeID type) const // Returns true if the given data type has an a-priori known size; assumes that given type is defined
		{
		if(type<NumAtomicTypes)
			{
			/* All atomic types except VarInt and String have fixed sizes: */
			return type<VarInt;
			}
		else
			return compoundTypes[type-NumAtomicTypes].fixedSize;
		}
	size_t getMinSize(TypeID type) const // Returns the minimum possible size of an object of the given data type in bytes; assumes that given type is defined
		{
		if(type<NumAtomicTypes)
			return atomicTypeMinSizes[type];
		else
			return compoundTypes[type-NumAtomicTypes].minSize;
		}
	size_t getAlignment(TypeID type) const // Returns the alignment size for the given data type's memory representation; assumes that given type is defined
		{
		if(type<NumAtomicTypes)
			return atomicTypeAlignments[type];
		else
			return compoundTypes[type-NumAtomicTypes].alignment;
		}
	size_t getMemSize(TypeID type) const // Returns the size of the given data type's memory representation in bytes; assumes that given type is defined
		{
		if(type<NumAtomicTypes)
			return atomicTypeMemSizes[type];
		else
			return compoundTypes[type-NumAtomicTypes].memSize;
		}
	
	/* Memory management methods: */
	void* createObject(TypeID type) const; // Creates an in-memory representation for an object of the given type
	void destroyObject(TypeID type,void* object) const; // Destroys an in-memory representation for an object of the given type that was previously created using createObject()
	
	/* Data object serialization methods: */
	void print(std::ostream& os,TypeID type,const void* object) const; // Prints the given object of the given data type to the given output stream
	size_t calcSize(TypeID type,const void* object) const; // Calculates the wire size of the given object of the given type when written to a binary sink
	MessageContinuation* prepareReading(TypeID type,void* object) const; // Prepares for reading an object of the given type into the given memory representation from a non-blocking socket
	MessageContinuation* continueReading(NonBlockSocket& socket,MessageContinuation* continuation) const; // Continues reading an object from the given non-blocking socket; returns null if object has been completely read
	template <class SourceParam>
	size_t read(SourceParam& source,TypeID type,void* object) const; // Reads an object of the given type from the given binary source; returns the number of bytes read
	template <class SinkParam>
	size_t write(TypeID type,const void* object,SinkParam& sink) const; // Writes an object of the given type to the given binary sink; returns the number of bytes written
	
	/* Methods working on serialized object representations: */
	void swapEndianness(TypeID type,MessageEditor& editor) const; // Swaps the endianness of the serialized object of the given type contained in the given message editor
	void checkSerialization(TypeID type,MessageEditor& editor) const; // Checks the validity of the serialized object of the given type contained in the given message editor (does not actually edit the object)
	void printSerialization(std::ostream& os,TypeID type,MessageReader& reader) const; // Prints the serialized object representation of the given type contained in the given message reader to the given output stream
	};

#endif
